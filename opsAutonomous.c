/*
 * opsAutonomous.c
 *
 *  Created on: 12.10.2012
 *      Author: Pavel
 *      Код для поддержки автономного режима работы терминального ПО (режим без ККМ)
 */

#include "sdk30.h"
#include "string.h"
#include "stdlib.h"
#include "log.h"

// печатает чек изменения цены текущего автономного товара (текущий делается с помощью mapMove)
static void autChangePriceReceipt(card NewPrice)
{
	char Name[lenOCPGoodsName], sOldPrice[lenOCPPrice], sNewPrice[lenOCPPrice];
	char sReceipt[prtW*3 + 1]; // 3 строки на чеке
	mapGet(gdsName, Name, lenOCPGoodsName);

	// форматирование
	Name[prtW] = 0; // ограничиваем длину названия поля
	strFormatAmount(mapGetCardValue(gdsPrice), 2, sOldPrice);
	strFormatAmount(NewPrice, 2, sNewPrice);

	// формируем чек
	sprintf(sReceipt, "%s\n%s -> %s\n", Name, sOldPrice, sNewPrice);
	dsAllocate(dslReceipt, sReceipt, strlen(sReceipt)+1);
	traUpdateDateTime();

	PrintReceipt(rloChangePrice, 1); // печатаем чек
	dsFree(dslReceipt); // очищаем память
}

static int autItemMenuSelectHandler(int ItemIndex, int MenuIndex, char *Item, Bool SkipEmptyEntries, Bool SelectOnce, Bool SkipZeroPrices, Bool ShowOnlyPriceMenuItem)
{
	char Code[lenOCPGoodsCode], Name[lenOCPGoodsName], MeasureUnit[lenOCPMeasureUnit];
	card Price = mapGetCardValue(gdsPrice);
	char sPrice[lenOCPPrice];
	int QuantityPrecision = mapGetByteValue(gdsQuantityPrecision);
	char sQuantityPrecision[lenOCPQuantityPrecision];
	mapGet(gdsCode, Code, sizeof(Code));
	mapGet(gdsName, Name, sizeof(Name));
	mapGet(gdsMeasureUnit, MeasureUnit, sizeof(MeasureUnit));
	//clcValueToString(Price, 2, sPrice);
	sprintf(sPrice, "%u", (unsigned int) Price);
	sprintf(sQuantityPrecision, "%u", (unsigned char) QuantityPrecision);

	dspClear();
	displayLS(0, Item);

	switch (MenuIndex)
	{
		case 1: // цена
			if (enterAmt(1, sPrice, 92) == kbdVAL)
			{
				Price = atoi(sPrice);
				if (Price != mapGetCardValue(gdsPrice)) // если цена товара изменилась
				{
					autChangePriceReceipt(Price);

					mapPutCard(gdsPrice, Price);
					mapPutCard(gdsDateTime, utGetDateTime());
				}
			}
			break;
		case 2: // код товара
			if (enterTxtRus(1, Code, lenOCPGoodsCode-1, FALSE) == kbdVAL)
			{
				mapPut(gdsCode, Code, strlen(Code));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		case 3: // название товара
			if (enterTxtRus(1, Name, lenOCPGoodsName-1, TRUE) == kbdVAL)
			{
				mapPut(gdsName, Name, strlen(Name));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		case 4: // точность количества товара
			if (enterStr(1, sQuantityPrecision, 2) == kbdVAL)
			{
				QuantityPrecision = atoi(sQuantityPrecision);
				if (QuantityPrecision >= 0 && QuantityPrecision <= 3)
				{
					mapPutByte(gdsQuantityPrecision, QuantityPrecision);
					mapPutCard(gdsDateTime, utGetDateTime());
				}
				else
					usrInfo(infInvalidQuantityPrecision);
			}
			break;
		case 5: // единица измерения
			if (enterTxtRus(1, MeasureUnit, lenOCPMeasureUnit-1, TRUE) == kbdVAL)
			{
				mapPut(gdsMeasureUnit, MeasureUnit, strlen(MeasureUnit));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		default: // неизвестный итем
			usrInfo(infUnknownMenuItem);
			break;
	}

	return ItemIndex;
}

// MenuIndex is 1-based, but item index is zero-based
int autGoodsMenuSelectHandler(int ItemIndex, int MenuIndex, char *Item, Bool SkipEmptyEntries, Bool SelectOnce, Bool SkipZeroPrices, Bool ShowOnlyPriceMenuItem)
{
	mapMove(gdsBeg, ItemIndex); // move to record selected
    while (autDisplayGoodsMenu(Item, autItemMenuSelectHandler, 1, SkipEmptyEntries, SelectOnce, SkipZeroPrices, ShowOnlyPriceMenuItem) > 0);
	return 1;
}

// Показывает меню товаров. Возвращает номер выбранного элемента или -1 в случае отмены
// Header: название меню
// MenuType: 0 - список продуктов, 1 - меню продукта
// ExceptEmptyEntries: true. Только для MenuType = 0 - не отображать ненастроенные продукты (без имени, кода или с нулевой ценой)
// arg0 - контекстно-зависимый аргумент, который передаётся в селектор, например:
//     - если для MenuType = 1 передан аргумент 1, то отображаем неполное меню (только цены)
//     - если для MenuType = 2 передан аргумент 1 и нужно отобразить только непустые элементы, то "пустота" контролируется только по коду и названию, если только цена = 0, то такой элемент не считается пустым
// Если SelectOnce - TRUE, то меню показывается только один раз, в противном случае после каждого вызова Select() меню снова будет показано
int autDisplayGoodsMenu(char *Header, SelectItemHandler Select, byte MenuType, Bool SkipEmptyEntries, Bool SelectOnce, Bool SkipZeroPrices, Bool ShowOnlyPriceMenuItem)
{
	word state = 0xFFFF; //menu state, it is (upper item)*10 + (current item)
    char mnu[__MNUMAX__][dspW + 1]; // the final menu array prepared to mnuSelect
	char* mnuptr[__MNUMAX__];   // header + all menu item
	int mnuitmindex[__MNUMAX__];   // реальные индексы товаров
	char Name[lenOCPGoodsName], Code[lenOCPGoodsCode]; // current item
	int ret, i, mnuindex = 0;
	card Price = 0;

lblShowAgain:
	mnuindex = 0;
	memset(mnu, 0, sizeof(mnu));
	memset(mnuptr, 0, sizeof(mnuptr));
	memset(Name, 0, sizeof(Name));

	strcpy(mnu[mnuindex], Header); // it is a menu header
	if (strlen(mnu[mnuindex]) > dspW)
		mnu[mnuindex][dspW-1] = 0; // trim string to allowed length
	mnuptr[mnuindex] = mnu[mnuindex];
	mnuindex++;

	// fill other items
	switch (MenuType)
	{
		case 0:
		{
			//nvmHold(0);
			for (i = 0; i < dimGoods; i++)
			{
				mapMove(gdsBeg, i/* + 1*/);
				mapGet(gdsName, Name, lenOCPGoodsName);

				if (SkipEmptyEntries)
				{
					mapGet(gdsCode, Code, lenOCPGoodsCode);
					Price = mapGetCardValue(gdsPrice);

					Bool SkipElement = strlen(Name) == 0 || strlen(Code) == 0;
					if (SkipZeroPrices) SkipElement |= Price == 0;
					if (SkipElement) continue;
				}

				if (strlen(Name) == 0)
					strcpy(Name, "\x3C\xBF\xE3\xE1\xE2\xDE\x3E"); // пусто
				if (strlen(Name) > dspW - 3) // cut name till display widtch minus 3 (for numbering 'xx.')
					Name[dspW - 3 - 1] = 0;

				sprintf(mnu[mnuindex], "%02d.%s", mnuindex, Name);
				if (strlen(mnu[mnuindex]) > dspW)
					mnu[mnuindex][dspW-1] = 0;
				mnuptr[mnuindex] = mnu[mnuindex];
				mnuitmindex[mnuindex] = i; // сохраняем индекс элемента
				mnuindex++;

				if (mnuindex > __MNUMAX__) break;
			}
			//nvmRelease(0);
			break;
		}
		case 1: case 2:
		{
			// цена, код товара, название товара, точность, единица измерения
			static const char *GoodsMenuItems[] = { "\xC6\xD5\xDD\xD0\x20\xE2\xDE\xD2\xD0\xE0\xD0", "\xBA\xDE\xD4\x20\xE2\xDE\xD2\xD0\xE0\xD0", "\xBD\xD0\xD7\xD2\xD0\xDD\xD8\xD5\x20\xE2\xDE\xD2\xD0\xE0\xD0", "\xC2\xDE\xE7\xDD\xDE\xE1\xE2\xEC\x20\xDA\xDE\xDB\xD8\xE7\xD5\xE1\xE2\xD2\xD0", "\xB5\xD4\xD8\xDD\xD8\xE6\xD0\x20\xD8\xD7\xDC\xD5\xE0\xD5\xDD\xD8\xEF" };
			int Count = ShowOnlyPriceMenuItem ? 1 : NUMBER_OF_ITEMS(GoodsMenuItems);
			for (i = 0; i < Count; i++)
			{
				strcpy(mnu[mnuindex], GoodsMenuItems[i]);
				mnuptr[mnuindex] = mnu[mnuindex];
				mnuitmindex[mnuindex] = i;
				mnuindex++;

				if (mnuindex > __MNUMAX__) break;
			}
			break;
		}
	}

	if (mnuindex == 1) return -3; // no menu items, except header

	state = state == 0xFFFF ? 0 : state; // initial menu state
    do
    {
        ret = mnuSelect((Pchar *)mnuptr, state, 0); // perform user dialog
        if (ret <= 0) return -1; // timeout or aborted - nothing to do
        state = ret;
        int index = state % __MNUMAX__; // sta % __MNUMAX__ is the current item selected. Index is One-based
        if (Select != NULL)
        	ret = Select(mnuitmindex[index], index, mnu[index], SkipEmptyEntries, SelectOnce, SkipZeroPrices, ShowOnlyPriceMenuItem);
        else
        	ret = -2; // null function
        if (ret < 0) return ret;

        if (!SelectOnce)
        	goto lblShowAgain;
        else
        	return ameOK;
    } while (state);

    return -1;
}

static void autProcessPeriodic()
{
	if (mapGetByteValue(cfgPeriodicAllowAfterAT) != 0)
		ProcessPeriodic();
}

typedef struct
{
	int order;
	int value;
} TIntegerSort;

int CompareTIntegerSort (const void *a, const void *b)
{
  const TIntegerSort *da = (const TIntegerSort *) a;
  const TIntegerSort *db = (const TIntegerSort *) b;

  return da->order - db->order;
}

static const char PaymentTypeAutoString[] = "\xB0\xD2\xE2\xDE\xD2\xEB\xD1\xDE\xE0\x00";
static const char PaymentTypeCashString[] = "\xBD\xD0\xDB\xD8\xE7\xDD\xEB\xD5\x00";
static const char PaymentTypeBankingCardString[] = "\xB1\xD0\xDD\xDA\x2E\x20\xDA\xD0\xE0\xE2\xD0\x00";
static const char PaymentTypeBonusesString[] = "\xB1\xDE\xDD\xE3\xE1\xEB\x00";
static const char PaymentTypeCreditString[] = "\xBA\xE0\xD5\xD4\xD8\xE2\x00";
static const char PaymentTypePrepaidAccountString[] = "\xC1\xE7\xF1\xE2\x20\xDA\xDB\xD8\xD5\xDD\xE2\xD0\x00";

// Возвращает список из отсортированных типов оплаты для функции rgSelectEx.
// После использования нужно очистить память
static tRadioGroup* GetOrderedPaymentTypes()
{
	int PaymentTypesCount = 6;
	TIntegerSort Sort[PaymentTypesCount];
	Sort[0].value = ptCash; Sort[0].order = mapGetByteValue(cfgAMPaymentTypeCashOrder);
	Sort[1].value = ptBankingCard; Sort[1].order = mapGetByteValue(cfgAMPaymentTypeBankingCardOrder);
	Sort[2].value = ptBonuses; Sort[2].order = mapGetByteValue(cfgAMPaymentTypeBonusesOrder);
	Sort[3].value = ptCredit; Sort[3].order = mapGetByteValue(cfgAMPaymentTypeCreditOrder);
	Sort[4].value = ptPrepaidAccount; Sort[4].order = mapGetByteValue(cfgAMPaymentTypePrepaidAccountOrder);
	Sort[5].value = ptAuto; Sort[5].order = mapGetByteValue(cfgAMPaymentTypeAutoOrder);

	qsort(Sort, PaymentTypesCount, sizeof(TIntegerSort), CompareTIntegerSort);

	int ResultSize = sizeof(tRadioGroup)*(PaymentTypesCount + 1);
	tRadioGroup *Result = memAllocate(ResultSize); // выделяем память для 5 типов оплаты + завершающий элемент
	memset(Result, 0, ResultSize);
	int i, j = 0;
	for (i = 0; i < PaymentTypesCount; i++)
	{
		if (Sort[i].order > 0)
		{
			Result[j].Value = Sort[i].value;
			switch (Result[j].Value)
			{
				case ptCash: Result[j].DisplayString = PaymentTypeCashString; break;
				case ptBankingCard: Result[j].DisplayString = PaymentTypeBankingCardString; break;
				case ptBonuses: Result[j].DisplayString = PaymentTypeBonusesString; break;
				case ptCredit: Result[j].DisplayString = PaymentTypeCreditString; break;
				case ptPrepaidAccount: Result[j].DisplayString = PaymentTypePrepaidAccountString; break;
				case ptAuto: Result[j].DisplayString = PaymentTypeAutoString; break;
				default: Result[j].DisplayString = NULL; break;
			}
			j++;
		}
	}
	return Result;
}

typedef enum
{
	aemQuantity = 1,
	aemAmount = 2
} autEnterMode;

// sPromptQuantity и sPromptAmount
// возвращает 0 если нажата красная кнопка, 1 - если введено количество, 2 - если введена сумма
static int EnterAmountOrQuantity(byte locPrompt, byte locEnter, char *sPromptQuantity, char *sPromptAmount, byte QuantityPrecision, byte AmountPrecision, int64 *Value)
{
	int Result = -1, key = 0, CurrentState = aemQuantity; // состояние: 1 - ввод количества, 2 - ввод суммы
	char sValue[lenOCPQuantity]; memclr(sValue, sizeof(sValue));
	*Value = 0;

	while (Result < 0)
	{
		displayLS8859(locPrompt, CurrentState == aemQuantity ? sPromptQuantity : sPromptAmount);
		displayLS8859(INV(dspLastRow), CurrentState == aemQuantity ? "ЗЕЛ: Далее,  F: Сумма" : "ЗЕЛ: Далее, F: Кол-во");
		if (CurrentState == aemQuantity) key = enterAmt(locEnter, sValue, 90 + QuantityPrecision);
		else if (CurrentState == aemAmount) key = enterAmt(locEnter, sValue, 90 + AmountPrecision);

		if (key == kbdANN) Result = 0;
		else if (key == kbdVAL) Result = CurrentState;
		else if (key == kbdINI /*F*/) CurrentState = CurrentState == aemQuantity ? aemAmount : aemQuantity;
	}
	if (Result != 0)
		*Value = clcNaturalToValue(atoi(sValue),  CurrentState == aemQuantity ? 3 - QuantityPrecision : 2 - AmountPrecision);
	return Result;
}

int autTransactionWizard(int ItemIndex, int MenuIndex, char* Item, Bool SkipEmptyEntries, Bool SelectOnce, Bool SkipZeroPrices, Bool ShowOnlyPriceMenuItem)
{
	int Result = 1;
	tRadioGroup *PaymentTypes = NULL;
	traReset2(TRUE);
	if (traCardNumberIsEmpty()) // читаем номер карты только если карта пустая. ПРОВЕРИТЬ!
		Result = opsOpenReaderAndGetCardNumber(TRUE);

	if (Result > 0)
	{
	lblStartOfWizard: {};
		mapMove(gdsBeg, ItemIndex); // move to record selected
		// get goods data
		char Name[lenOCPGoodsName], Code[lenOCPGoodsCode], Name866[lenOCPGoodsName], Code866[lenOCPGoodsCode],
			 sPrice[lenOCPPrice], sQuantity[lenOCPQuantity], sAmount[lenOCPPrice],
			 MeasureUnit[lenOCPMeasureUnit], MeasureUnit866[lenOCPMeasureUnit];
		memset(Name866, 0, sizeof(Name866));
		memset(Code866, 0, sizeof(Code866));
		memset(MeasureUnit866, 0, sizeof(MeasureUnit866));

		mapGet(gdsCode, Code, sizeof(Code));
		mapGet(gdsName, Name, sizeof(Name));
		mapGet(gdsMeasureUnit, MeasureUnit, sizeof(MeasureUnit));
		int64 Price = mapGetCardValue(gdsPrice), Quantity = 0, Amount = 0, Value = 0;
		byte QuantityPrecision = mapGetCardValue(gdsQuantityPrecision);
		clcValueToString(Price, 2, sPrice);
		strcpy(sQuantity, "0");

		dspClear();
		// выбор типа оплаты
		displayLS(0, "\xC2\xD8\xDF\x20\xDE\xDF\xDB\xD0\xE2\xEB");
		PaymentTypes = GetOrderedPaymentTypes();
		if (rgCount(PaymentTypes) == 0)
		{
			memFree(PaymentTypes);
			usrInfo(infNoAllowedPaymentTypes);
			goto lblKO;
		}

		ocpPaymentTypes PaymentType = PaymentTypes[0].Value;
		int rgResult = rgSelectEx(1, PaymentType, PaymentTypes);

		if (rgResult >= 0)
		{
			dspClearLines(0, dspLastRow-1);
			PaymentType = (ocpPaymentTypes)rgResult;
			// display goods data
			byte loc = 0;
			displayLS(loc, "%s (%s)", Name, Code); loc++; // Name и Code уже в ISO8859-5 (перекодировать не нужно)
			displayLS(loc, "\xBE\xDF\xDB\xD0\xE2\xD0\x3A\x20%s", PaymentTypes[rgIndexByValue(PaymentType, PaymentTypes)].DisplayString); loc++; // тип оплаты
			displayLS8859(loc, "Цена: %s", sPrice); loc++; // цена

			// ввод количества или суммы
			int ValueType = EnterAmountOrQuantity(loc, loc+1, "Количество:", "Сумма:", QuantityPrecision, 2, &Value);
			if (ValueType > 0) // вводим количество с требуемой	точностью
			{
				if (ValueType == aemQuantity)
				{
					Quantity = Value;
					/* Не стал выделять отдельно QuantityRounded только для отображения и отправлять на сервер Quantity
					 * для самостоятельного округления, т.к. кассир должен сразу видеть какое кол-во товара он отпускает */
					if (Quantity == 0)
					{
						usrInfo(infQuantityCannotBeAZero);
						goto lblStartOfWizard;
					}
					Amount = clcNormalizePriceQuantity(Price * Quantity); // вычисление Amount
				}
				else // ввод Amount
				{
					Quantity = clcDivideAmountPriceForQuantity(Value, Price, QuantityPrecision); // вычисление Quantity
					Amount = clcNormalizePriceQuantity(Price * Quantity); // вычисление Amount
				}

				clcValueToString(Amount, 2, sAmount);
				dspLS(loc+1, ""); // очистка строки
				clcValueToString(Quantity, 3, sQuantity);
				displayLS8859(loc, "Количество: %s%s", sQuantity, ValueType == aemQuantity ? "" : "?"); loc++; // отображение количества
				displayLS8859(loc, "Сумма: %s%s", sAmount, ValueType == aemAmount ? "" : "?"); loc++; // отображение стоимости

				displayLS8859(INV(dspLastRow), "З/Ж/К - Далее/Ред/Отм");
				int key = kbdWait(120);

				if (key == kbdCOR) goto lblStartOfWizard;
				else if (key == kbdVAL)
				{
					if (Amount == 0)
					{
						usrInfo(infAmountCannotBeAZero);
						goto lblStartOfWizard;
					}

					// формируем fake-запрос от ККМ на проведение транзакции
					// формируем структуру типов оплаты
					tlvWriter PaymentsWriter;
					tlvwInit(&PaymentsWriter);

					if (PaymentType != ptAuto)
					{
						byte tag;
						switch (PaymentType)
						{
							case ptCash: tag = fptAmountCash; break;
							case ptBankingCard: tag = fptAmountBankingCard; break;
							case ptBonuses: tag = fptAmountBonuses; break;
							case ptCredit: tag = fptAmountCredit; break;
							case ptPrepaidAccount: tag = fptAmountPrepaidAccount; break;
							default: usrInfo(infUnknownPaymentType); Result = ameUnknownPaymentType; goto lblKO;
						}
						tlvwAppendCard(&PaymentsWriter, tag, Amount);
					}
					if (ValueType == aemAmount)
						tlvwAppendByte(&PaymentsWriter, fptPaymentTypeForDiscount, PaymentType);

					// формируем структуру товарной позиции
					tlvWriter ArticleWriter;
					tlvwInit(&ArticleWriter);
					byte ArticleFlags = fafNone;

					if (ValueType == aemAmount && mapGetByteValue(cfgAMAmountDiscountByQuantity) != 0) // если указано, что при вводе суммы нужно включать флаг "скидка товаром", то включаем это флаг
						ArticleFlags |= fafDiscountByQuantity;

					// конвертируем Name и Code в cp866
					strTranslate(Code866, Code, cpISO8859, cp866);
					strTranslate(Name866, Name, cpISO8859, cp866);
					strTranslate(MeasureUnit866, MeasureUnit, cpISO8859, cp866);

					tlvwAppendString(&ArticleWriter, fatGoodsCode, Code866);
					tlvwAppendString(&ArticleWriter, fatGoodsName, Name866);
					tlvwAppendCard(&ArticleWriter, fatPriceWithoutDiscount, Price);
					tlvwAppendCard(&ArticleWriter, fatQuantity, Quantity);
					tlvwAppendCard(&ArticleWriter, fatAmountWithoutDiscount, Amount);
					if (ArticleFlags != fafNone) tlvwAppendByte(&ArticleWriter, fatFlags, ArticleFlags);
					tlvwAppendByte(&ArticleWriter, fatPaymentType, PaymentType);
					tlvwAppendByte(&ArticleWriter, fatQuantityPrecision, QuantityPrecision);
					if (strlen(MeasureUnit866) != 0) tlvwAppendString(&ArticleWriter, fatMeasureUnit, MeasureUnit866);

					// формируем структуру фискального чека
					tlvWriter FiscalWriter;
					tlvwInit(&FiscalWriter);
					byte FiscalFlags = frfNone;

					if (ValueType == aemAmount && mapGetByteValue(cfgAMAmountPaymentPriority) != 0) // если указано, что при вводе суммы нужно включать флаг "скидка товаром", то включаем это флаг
						FiscalFlags |= frfPaymentsPriority;

					tlvwAppend(&FiscalWriter, rftPayments, PaymentsWriter.DataSize, PaymentsWriter.Data);
					tlvwAppendCard(&FiscalWriter, rftAmountWithoutDiscount, Amount);
					if (FiscalFlags != frfNone) tlvwAppendByte(&FiscalWriter, rftFlags, FiscalFlags);
					tlvwAppend(&FiscalWriter, rftArticle, ArticleWriter.DataSize, ArticleWriter.Data);

					tlvwFree(&PaymentsWriter);
					tlvwFree(&ArticleWriter);

					// пакет от ККМ
					char CardNumber[lenCardNumber];
					mapGet(traCardNumber, CardNumber, lenCardNumber);

					tlvWriter PackageWriter;
					tlvwInit(&PackageWriter);

					tlvwAppendByte(&PackageWriter, rstPackageID, rspQueryCloseReceipt);
					tlvwAppendCard(&PackageWriter, rstDateTime, mapGetCardValue(traDateTimeUnix));
					tlvwAppendWord(&PackageWriter, rstFlags, rsfWaitForAnswer);
					tlvwAppendString(&PackageWriter, rstCardID, CardNumber);
					tlvwAppend(&PackageWriter, rstFiscalReceipt, FiscalWriter.DataSize, FiscalWriter.Data);
					tlvwAppendCard(&PackageWriter, rstAmount, Amount);

					tlvwFree(&FiscalWriter);

					// записываем пакет от ККМ в дата-слот
					dsAllocate(dslRSPackage, PackageWriter.Data, PackageWriter.DataSize);
					tlvwFree(&PackageWriter);

					mapPutByte(traResponse, crCommonError);
					// отправляем пакет на обработку серверу
					opsCommunication("\xC1\xDE\xD5\xD4\xD8\xDD\xD5\xDD\xD8\xD5", lssStartAutonomousTransaction, rssNotConnected);
					// проводим периодическое действие, если разрешено
					autProcessPeriodic();

					Result = mapGetCardValue(traResponse) == crSuccess;
				}
			}
		}
		opsWaitForEjectCardAndCloseReader();
	}

	goto lblEnd; // goto finally

lblKO: // catch analog
	goto lblEnd;

lblEnd: // finally analog
	traReset(); // очищаем транзакцию
	memFree(PaymentTypes);

	return Result;
}

// очистка справочника товаров
void autClearGoods()
{
	//nvmHold(0);
	mapReset(gdsBeg);
	//nvmRelease(0);
}

// подсчёт количества заполненных записей в справочнике товаров
card autGoodsCount()
{
	card Result = 0;
	int i;
	//nvmHold(0);
	for (i = 0; i < dimGoods; i++)
		if (!autIsEmpty(i))
			Result++;
	//nvmRelease(0);
	return Result;
}

// Перемещает указатель справочника автономных товаров на запись, которая содержит переданный код.
// Возвращает TRUE в случае успеха и FALSE если код не найден.
Bool autMoveToGoodsCode(char *CodeToSearch)
{
	int i;
	Bool Result = FALSE;
	char Code[lenOCPGoodsCode];
	//nvmHold(0);
	for (i = 0; i < dimGoods; i++)
	{
		mapMove(gdsBeg, i);
		mapGet(gdsCode, Code, lenOCPGoodsCode);
		if (strcmp(Code, CodeToSearch) == 0)
		{
			Result = TRUE;
			break;
		}

	}
	//nvmRelease(0);
	return Result;
}

// Возвращает TRUE, если запись по переданноу индексу пустая
// Пустая означает что И код И название незаполнены И цена равна нулю
Bool autIsEmpty(int Index)
{
	char Code[lenOCPGoodsCode], Name[lenOCPGoodsName];
	card Price = 0;

	mapMove(gdsBeg, Index);
	mapGet(gdsCode, Code, lenOCPGoodsCode);
	mapGet(gdsName, Name, lenOCPGoodsName);
	mapGetCard(gdsPrice, Price);

	return strlen(Name) == 0 && strlen(Code) == 0 && Price == 0;
}

// Перемещает указатель справочника автономных товаров на первую пустую запись
// Возвращает TRUE в случае успеха и FALSE если код не найден.
static Bool autMoveToFirstUnused()
{
	int i;
	Bool Result = FALSE;
	//nvmHold(0);
	for (i = 0; i < dimGoods; i++)
	{
		if (autIsEmpty(i)) // переход на позицию - тут
		{
			Result = TRUE;
			break;
		}
	}
	//nvmRelease(0);
	return Result;
}

// вставляет запись в первую попавшуюся пустую строку
// в случае неудали - печатает ошибку
static void autInsertRecord(char *Code, char *Name, card Price, byte QuantityPrecision, char *MeasureUnit)
{
	if (autMoveToFirstUnused())
	{
		mapPut(gdsCode, Code, lenOCPGoodsCode);
		mapPut(gdsName, Name, lenOCPGoodsName);
		mapPutCard(gdsPrice, Price);
		mapPutByte(gdsQuantityPrecision, QuantityPrecision);
		mapPut(gdsMeasureUnit, MeasureUnit, lenOCPMeasureUnit);
	}
	else
	{
		char error[260];
		sprintf(error, "Нет свободного места в справочнике для загрузки товара '%s' (%s)\n", Name, Code);
		PrintRefusal8859(error);
	}
}

void autParseGoods(tBuffer *buf)
{
	//START_PERFOMANCE("ParseGoods");
	dspClear();
	displayLS8859(0, "Обработка...");

	if (mapGetByteValue(traLoadType) == agltChangeList) // очищаем таблицу товаров, если у нас замена списка
		autClearGoods();
	AutonomousGoodsLoadOperations Operation;
	card Received = 0, Ignored = 0, Inserted = 0, Updated = 0;

	// заново парсим сообщение от сервера
	tlvReader tr;
    tlvrInit(&tr, buf->ptr, buf->dim);
    byte tag, *value;
    card len;
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	if (tag == srtGoodsInfo)
    	{
        	value = memAllocate(len + 1); // +1 for strings
        	tlvrNextValue(&tr, len, value);
			Received++;
			autParseGoodsItem(value, len, &Operation);

			if (Operation == agloInsert) Inserted++;
			else if (Operation == agloUpdate) Updated++;
			else Ignored++;
        	value = memFree(value); // FREE MEMORY OF VALUE
    	}
    	else
    		tlvrMoveToNextTag(&tr, len);
    }

    mapPutCard(traLoadResultReceived, Received);
    mapPutCard(traLoadResultIgnored, Ignored);
    mapPutCard(traLoadResultInserted, Inserted);
    mapPutCard(traLoadResultUpdated, Updated);

    mapPutByte(traResponse, crSuccess);
	//STOP_PERFOMANCE("ParseGoods");
}


// парсим автономный товар и сохраняем его в БД в зависимости от текущего типа загрузки
void autParseGoodsItem(byte *ptr, card size, byte *Operation)
{
	char Code[lenOCPGoodsCode], Name[lenOCPGoodsName], MeasureUnit[lenOCPMeasureUnit];
	card Price = 0;
	byte QuantityPrecision = mapGetByteValue(dfltQuantityPrecision);
	memset(Code, 0, sizeof(Code));
	memset(Name, 0, sizeof(Name));
	memset(MeasureUnit, 0, sizeof(MeasureUnit));

	tlvReader tr;
    tlvrInit(&tr, ptr, size);
    byte tag, *value;
    card len;
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case 0x01: tlv2string(value, len, Code); break;
			case 0x02: tlv2string(value, len, Name); break;
			case 0x03: Price = tlv2card(value); break;
			case 0x04: QuantityPrecision = tlv2byte(value); break;
			case 0x05: tlv2string(value, len, MeasureUnit); break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }

    *Operation = agloIgnore;
    AutonomousGoodsLoadType LoadType = mapGetByteValue(traLoadType);
    Bool Founded = autMoveToGoodsCode(Code); // если нашли код - перемещаемся к указанной строке справочника
    switch (LoadType)
    {
		case agltUpdatePrices:
			if (Founded)
			{
				mapPutCard(gdsPrice, Price); // если элемент найден, то обновляем цену
				*Operation = agloUpdate;
			}
			break;
		case agltCombineLists:
			if (Founded) // если элемент найден, то обновляем цену, название, точность и единицу измерения
			{
				mapPutCard(gdsPrice, Price);
				mapPut(gdsName, Name, lenOCPGoodsName);
				mapPutByte(gdsQuantityPrecision, QuantityPrecision);
				mapPut(gdsMeasureUnit, MeasureUnit, lenOCPMeasureUnit);
				*Operation = agloUpdate;
			}
			else // если не найден - вставляем новую запись
			{
				autInsertRecord(Code, Name, Price, QuantityPrecision, MeasureUnit);
				*Operation = agloInsert;
			}
			break;
		case agltChangeList:
			if (!Founded) // если элемент не найден, то вставляем новую запись
			{
				autInsertRecord(Code, Name, Price, QuantityPrecision, MeasureUnit);
				*Operation = agloInsert;
			}
			else
				PrintRefusal8859("Внимание! Загрузка товаров в неочищенный справочник. Данные могут быть повреждены.\n"); // если найден - то плохо почистили справочник
			break;
		default:
			PrintRefusal8859("This AutonomousGoodsLoadType is not supported\n");
			break;
    }
}

Bool autEnterQuantity(byte locPrompt, const char *Prompt, byte locEnter, word mapKey, Bool ShowResult, byte locResult, byte QuantityPrecision)
{
lblStartAgain: {}
	Bool Result = FALSE;
	char sQuantity[lenOCPQuantity];
    memset(sQuantity, 0, lenOCPQuantity);
    card Quantity = 0;

    displayLS(locPrompt, Prompt);
    if (enterAmt(locEnter, sQuantity, 90 + QuantityPrecision) == kbdVAL)
    {
    	Quantity = clcNaturalToValue(atoi(sQuantity), 3 - QuantityPrecision);

		if (Quantity == 0)
		{
			tDisplayState ds;
			dspGetState(&ds);
			usrInfo(infQuantityCannotBeAZero);
			dspSetState(&ds);
			goto lblStartAgain;
		}
		mapPutCard(mapKey, Quantity);

		if (ShowResult)
		{
    		displayLS(locPrompt, " ");
    		displayLS(locEnter, " ");
			memset(sQuantity, 0, sizeof(sQuantity));
			strFormatAmount(Quantity, 3, sQuantity);
    		displayLS8859(locResult, "Количество: %s", sQuantity);
		}

    	Result = TRUE;
    }
    return Result;
}

// загрузка списка товаров для работы терминала в автономном режиме
void autDownloadGoods(Bool ShowInfoAfterLoading)
{
	if (IsTerminalConfigured())
	{
		// неважно закрыта смена или нет
		traReset();

		card WasCount = autGoodsCount();

		static const tRadioGroup LoadTypes[] =
		{
			{ agltUpdatePrices, "\xBE\xD1\xDD\xDE\xD2\xD8\xE2\xEC\x20\xE6\xD5\xDD\xEB" }, // Обновить цены
			{ agltCombineLists, "\xBE\xD1\xEA\xD5\xD4\xD8\xDD\xD8\xE2\xEC\x20\xE1\xDF\xD8\xE1\xDA\xD8" }, // Объединить списки
			{ agltChangeList, "\xB7\xD0\xDC\xD5\xDD\xD8\xE2\xEC\x20\xE1\xDF\xD8\xE1\xDE\xDA" }, // Заменить список
			{ 0, 0 }
		};
		int LoadType = mapGetByteValue(traLoadType);
		if (autGoodsCount() == 0) // если товаров нет
			LoadType = agltChangeList;
		else
		{
			dspClear();
			displayLS8859(0, "Выбор режима загрузки");
			LoadType = rgSelectEx(1, (byte)LoadType, LoadTypes);
		}
		if (LoadType >= 0)
		{
			mapPutByte(traLoadType, (byte)LoadType);

			LoadType = mapGetByteValue(traLoadType);

			byte lt;
			mapGetByte(traLoadType, lt);

			mapPutByte(traResponse, crCommonError);
			opsCommunication("\xC1\xDE\xD5\xD4\xD8\xDD\xD5\xDD\xD8\xD5", lssStartLoadAutonomousGoods, rssNotConnected); // Обновление конфигурации / Соединение

			if (mapGetByteValue(traResponse) == crSuccess)
			{
				if (ShowInfoAfterLoading)
				{
					// показываем результаты загрузки
					dspClear();
					displayLS8859(CNTR(INV(0)), "Результаты загрузки");
					displayLS8859(1, "Было     : %u", WasCount);
					displayLS8859(2, "Получено : %u", mapGetCardValue(traLoadResultReceived));
					displayLS8859(3, "Пропущено: %u", mapGetCardValue(traLoadResultIgnored));
					displayLS8859(4, "Обновлено: %u", mapGetCardValue(traLoadResultUpdated));
					displayLS8859(5, "Добавлено: %u", mapGetCardValue(traLoadResultInserted));
					displayLS8859(6, "Свободно : %u", dimGoods - autGoodsCount());
					opsPressAnyKeyToContinue();
				}
			}
		}
	}
	else
		usrInfo(infTerminalIDIsNotConfigured);
}

void autEditPrices()
{
	if (!opsIsAutonomousModeAllowed()) return;

	if (autDisplayGoodsMenu("\xC2\xDE\xD2\xD0\xE0\xEB\x20\xD8\x20\xE3\xE1\xDB\xE3\xD3\xD8", autGoodsMenuSelectHandler, 0, TRUE, FALSE, FALSE, TRUE) == ameNoItemsForDisplay)
		usrInfo(infNoConfiguredGoods);
}
