/*
 * opsAutonomous.c
 *
 *  Created on: 12.10.2012
 *      Author: Pavel
 *      ��� ��� ��������� ����������� ������ ������ ������������� �� (����� ��� ���)
 */

#include "sdk30.h"
#include "string.h"
#include "stdlib.h"
#include "log.h"

// �������� ��� ��������� ���� �������� ����������� ������ (������� �������� � ������� mapMove)
static void autChangePriceReceipt(card NewPrice)
{
	char Name[lenOCPGoodsName], sOldPrice[lenOCPPrice], sNewPrice[lenOCPPrice];
	char sReceipt[prtW*3 + 1]; // 3 ������ �� ����
	mapGet(gdsName, Name, lenOCPGoodsName);

	// ��������������
	Name[prtW] = 0; // ������������ ����� �������� ����
	strFormatAmount(mapGetCardValue(gdsPrice), 2, sOldPrice);
	strFormatAmount(NewPrice, 2, sNewPrice);

	// ��������� ���
	sprintf(sReceipt, "%s\n%s -> %s\n", Name, sOldPrice, sNewPrice);
	dsAllocate(dslReceipt, sReceipt, strlen(sReceipt)+1);
	traUpdateDateTime();

	PrintReceipt(rloChangePrice, 1); // �������� ���
	dsFree(dslReceipt); // ������� ������
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
		case 1: // ����
			if (enterAmt(1, sPrice, 92) == kbdVAL)
			{
				Price = atoi(sPrice);
				if (Price != mapGetCardValue(gdsPrice)) // ���� ���� ������ ����������
				{
					autChangePriceReceipt(Price);

					mapPutCard(gdsPrice, Price);
					mapPutCard(gdsDateTime, utGetDateTime());
				}
			}
			break;
		case 2: // ��� ������
			if (enterTxtRus(1, Code, lenOCPGoodsCode-1, FALSE) == kbdVAL)
			{
				mapPut(gdsCode, Code, strlen(Code));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		case 3: // �������� ������
			if (enterTxtRus(1, Name, lenOCPGoodsName-1, TRUE) == kbdVAL)
			{
				mapPut(gdsName, Name, strlen(Name));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		case 4: // �������� ���������� ������
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
		case 5: // ������� ���������
			if (enterTxtRus(1, MeasureUnit, lenOCPMeasureUnit-1, TRUE) == kbdVAL)
			{
				mapPut(gdsMeasureUnit, MeasureUnit, strlen(MeasureUnit));
				mapPutCard(gdsDateTime, utGetDateTime());
			}
			break;
		default: // ����������� ����
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

// ���������� ���� �������. ���������� ����� ���������� �������� ��� -1 � ������ ������
// Header: �������� ����
// MenuType: 0 - ������ ���������, 1 - ���� ��������
// ExceptEmptyEntries: true. ������ ��� MenuType = 0 - �� ���������� ������������� �������� (��� �����, ���� ��� � ������� �����)
// arg0 - ����������-��������� ��������, ������� ��������� � ��������, ��������:
//     - ���� ��� MenuType = 1 ������� �������� 1, �� ���������� �������� ���� (������ ����)
//     - ���� ��� MenuType = 2 ������� �������� 1 � ����� ���������� ������ �������� ��������, �� "�������" �������������� ������ �� ���� � ��������, ���� ������ ���� = 0, �� ����� ������� �� ��������� ������
// ���� SelectOnce - TRUE, �� ���� ������������ ������ ���� ���, � ��������� ������ ����� ������� ������ Select() ���� ����� ����� ��������
int autDisplayGoodsMenu(char *Header, SelectItemHandler Select, byte MenuType, Bool SkipEmptyEntries, Bool SelectOnce, Bool SkipZeroPrices, Bool ShowOnlyPriceMenuItem)
{
	word state = 0xFFFF; //menu state, it is (upper item)*10 + (current item)
    char mnu[__MNUMAX__][dspW + 1]; // the final menu array prepared to mnuSelect
	char* mnuptr[__MNUMAX__];   // header + all menu item
	int mnuitmindex[__MNUMAX__];   // �������� ������� �������
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
					strcpy(Name, "\x3C\xBF\xE3\xE1\xE2\xDE\x3E"); // �����
				if (strlen(Name) > dspW - 3) // cut name till display widtch minus 3 (for numbering 'xx.')
					Name[dspW - 3 - 1] = 0;

				sprintf(mnu[mnuindex], "%02d.%s", mnuindex, Name);
				if (strlen(mnu[mnuindex]) > dspW)
					mnu[mnuindex][dspW-1] = 0;
				mnuptr[mnuindex] = mnu[mnuindex];
				mnuitmindex[mnuindex] = i; // ��������� ������ ��������
				mnuindex++;

				if (mnuindex > __MNUMAX__) break;
			}
			//nvmRelease(0);
			break;
		}
		case 1: case 2:
		{
			// ����, ��� ������, �������� ������, ��������, ������� ���������
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

// ���������� ������ �� ��������������� ����� ������ ��� ������� rgSelectEx.
// ����� ������������� ����� �������� ������
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
	tRadioGroup *Result = memAllocate(ResultSize); // �������� ������ ��� 5 ����� ������ + ����������� �������
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

// sPromptQuantity � sPromptAmount
// ���������� 0 ���� ������ ������� ������, 1 - ���� ������� ����������, 2 - ���� ������� �����
static int EnterAmountOrQuantity(byte locPrompt, byte locEnter, char *sPromptQuantity, char *sPromptAmount, byte QuantityPrecision, byte AmountPrecision, int64 *Value)
{
	int Result = -1, key = 0, CurrentState = aemQuantity; // ���������: 1 - ���� ����������, 2 - ���� �����
	char sValue[lenOCPQuantity]; memclr(sValue, sizeof(sValue));
	*Value = 0;

	while (Result < 0)
	{
		displayLS8859(locPrompt, CurrentState == aemQuantity ? sPromptQuantity : sPromptAmount);
		displayLS8859(INV(dspLastRow), CurrentState == aemQuantity ? "���: �����,  F: �����" : "���: �����, F: ���-��");
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
	if (traCardNumberIsEmpty()) // ������ ����� ����� ������ ���� ����� ������. ���������!
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
		// ����� ���� ������
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
			displayLS(loc, "%s (%s)", Name, Code); loc++; // Name � Code ��� � ISO8859-5 (�������������� �� �����)
			displayLS(loc, "\xBE\xDF\xDB\xD0\xE2\xD0\x3A\x20%s", PaymentTypes[rgIndexByValue(PaymentType, PaymentTypes)].DisplayString); loc++; // ��� ������
			displayLS8859(loc, "����: %s", sPrice); loc++; // ����

			// ���� ���������� ��� �����
			int ValueType = EnterAmountOrQuantity(loc, loc+1, "����������:", "�����:", QuantityPrecision, 2, &Value);
			if (ValueType > 0) // ������ ���������� � ���������	���������
			{
				if (ValueType == aemQuantity)
				{
					Quantity = Value;
					/* �� ���� �������� �������� QuantityRounded ������ ��� ����������� � ���������� �� ������ Quantity
					 * ��� ���������������� ����������, �.�. ������ ������ ����� ������ ����� ���-�� ������ �� ��������� */
					if (Quantity == 0)
					{
						usrInfo(infQuantityCannotBeAZero);
						goto lblStartOfWizard;
					}
					Amount = clcNormalizePriceQuantity(Price * Quantity); // ���������� Amount
				}
				else // ���� Amount
				{
					Quantity = clcDivideAmountPriceForQuantity(Value, Price, QuantityPrecision); // ���������� Quantity
					Amount = clcNormalizePriceQuantity(Price * Quantity); // ���������� Amount
				}

				clcValueToString(Amount, 2, sAmount);
				dspLS(loc+1, ""); // ������� ������
				clcValueToString(Quantity, 3, sQuantity);
				displayLS8859(loc, "����������: %s%s", sQuantity, ValueType == aemQuantity ? "" : "?"); loc++; // ����������� ����������
				displayLS8859(loc, "�����: %s%s", sAmount, ValueType == aemAmount ? "" : "?"); loc++; // ����������� ���������

				displayLS8859(INV(dspLastRow), "�/�/� - �����/���/���");
				int key = kbdWait(120);

				if (key == kbdCOR) goto lblStartOfWizard;
				else if (key == kbdVAL)
				{
					if (Amount == 0)
					{
						usrInfo(infAmountCannotBeAZero);
						goto lblStartOfWizard;
					}

					// ��������� fake-������ �� ��� �� ���������� ����������
					// ��������� ��������� ����� ������
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

					// ��������� ��������� �������� �������
					tlvWriter ArticleWriter;
					tlvwInit(&ArticleWriter);
					byte ArticleFlags = fafNone;

					if (ValueType == aemAmount && mapGetByteValue(cfgAMAmountDiscountByQuantity) != 0) // ���� �������, ��� ��� ����� ����� ����� �������� ���� "������ �������", �� �������� ��� ����
						ArticleFlags |= fafDiscountByQuantity;

					// ������������ Name � Code � cp866
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

					// ��������� ��������� ����������� ����
					tlvWriter FiscalWriter;
					tlvwInit(&FiscalWriter);
					byte FiscalFlags = frfNone;

					if (ValueType == aemAmount && mapGetByteValue(cfgAMAmountPaymentPriority) != 0) // ���� �������, ��� ��� ����� ����� ����� �������� ���� "������ �������", �� �������� ��� ����
						FiscalFlags |= frfPaymentsPriority;

					tlvwAppend(&FiscalWriter, rftPayments, PaymentsWriter.DataSize, PaymentsWriter.Data);
					tlvwAppendCard(&FiscalWriter, rftAmountWithoutDiscount, Amount);
					if (FiscalFlags != frfNone) tlvwAppendByte(&FiscalWriter, rftFlags, FiscalFlags);
					tlvwAppend(&FiscalWriter, rftArticle, ArticleWriter.DataSize, ArticleWriter.Data);

					tlvwFree(&PaymentsWriter);
					tlvwFree(&ArticleWriter);

					// ����� �� ���
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

					// ���������� ����� �� ��� � ����-����
					dsAllocate(dslRSPackage, PackageWriter.Data, PackageWriter.DataSize);
					tlvwFree(&PackageWriter);

					mapPutByte(traResponse, crCommonError);
					// ���������� ����� �� ��������� �������
					opsCommunication("\xC1\xDE\xD5\xD4\xD8\xDD\xD5\xDD\xD8\xD5", lssStartAutonomousTransaction, rssNotConnected);
					// �������� ������������� ��������, ���� ���������
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
	traReset(); // ������� ����������
	memFree(PaymentTypes);

	return Result;
}

// ������� ����������� �������
void autClearGoods()
{
	//nvmHold(0);
	mapReset(gdsBeg);
	//nvmRelease(0);
}

// ������� ���������� ����������� ������� � ����������� �������
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

// ���������� ��������� ����������� ���������� ������� �� ������, ������� �������� ���������� ���.
// ���������� TRUE � ������ ������ � FALSE ���� ��� �� ������.
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

// ���������� TRUE, ���� ������ �� ���������� ������� ������
// ������ �������� ��� � ��� � �������� ����������� � ���� ����� ����
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

// ���������� ��������� ����������� ���������� ������� �� ������ ������ ������
// ���������� TRUE � ������ ������ � FALSE ���� ��� �� ������.
static Bool autMoveToFirstUnused()
{
	int i;
	Bool Result = FALSE;
	//nvmHold(0);
	for (i = 0; i < dimGoods; i++)
	{
		if (autIsEmpty(i)) // ������� �� ������� - ���
		{
			Result = TRUE;
			break;
		}
	}
	//nvmRelease(0);
	return Result;
}

// ��������� ������ � ������ ���������� ������ ������
// � ������ ������� - �������� ������
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
		sprintf(error, "��� ���������� ����� � ����������� ��� �������� ������ '%s' (%s)\n", Name, Code);
		PrintRefusal8859(error);
	}
}

void autParseGoods(tBuffer *buf)
{
	//START_PERFOMANCE("ParseGoods");
	dspClear();
	displayLS8859(0, "���������...");

	if (mapGetByteValue(traLoadType) == agltChangeList) // ������� ������� �������, ���� � ��� ������ ������
		autClearGoods();
	AutonomousGoodsLoadOperations Operation;
	card Received = 0, Ignored = 0, Inserted = 0, Updated = 0;

	// ������ ������ ��������� �� �������
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


// ������ ���������� ����� � ��������� ��� � �� � ����������� �� �������� ���� ��������
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
    Bool Founded = autMoveToGoodsCode(Code); // ���� ����� ��� - ������������ � ��������� ������ �����������
    switch (LoadType)
    {
		case agltUpdatePrices:
			if (Founded)
			{
				mapPutCard(gdsPrice, Price); // ���� ������� ������, �� ��������� ����
				*Operation = agloUpdate;
			}
			break;
		case agltCombineLists:
			if (Founded) // ���� ������� ������, �� ��������� ����, ��������, �������� � ������� ���������
			{
				mapPutCard(gdsPrice, Price);
				mapPut(gdsName, Name, lenOCPGoodsName);
				mapPutByte(gdsQuantityPrecision, QuantityPrecision);
				mapPut(gdsMeasureUnit, MeasureUnit, lenOCPMeasureUnit);
				*Operation = agloUpdate;
			}
			else // ���� �� ������ - ��������� ����� ������
			{
				autInsertRecord(Code, Name, Price, QuantityPrecision, MeasureUnit);
				*Operation = agloInsert;
			}
			break;
		case agltChangeList:
			if (!Founded) // ���� ������� �� ������, �� ��������� ����� ������
			{
				autInsertRecord(Code, Name, Price, QuantityPrecision, MeasureUnit);
				*Operation = agloInsert;
			}
			else
				PrintRefusal8859("��������! �������� ������� � ����������� ����������. ������ ����� ���� ����������.\n"); // ���� ������ - �� ����� ��������� ����������
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
    		displayLS8859(locResult, "����������: %s", sQuantity);
		}

    	Result = TRUE;
    }
    return Result;
}

// �������� ������ ������� ��� ������ ��������� � ���������� ������
void autDownloadGoods(Bool ShowInfoAfterLoading)
{
	if (IsTerminalConfigured())
	{
		// ������� ������� ����� ��� ���
		traReset();

		card WasCount = autGoodsCount();

		static const tRadioGroup LoadTypes[] =
		{
			{ agltUpdatePrices, "\xBE\xD1\xDD\xDE\xD2\xD8\xE2\xEC\x20\xE6\xD5\xDD\xEB" }, // �������� ����
			{ agltCombineLists, "\xBE\xD1\xEA\xD5\xD4\xD8\xDD\xD8\xE2\xEC\x20\xE1\xDF\xD8\xE1\xDA\xD8" }, // ���������� ������
			{ agltChangeList, "\xB7\xD0\xDC\xD5\xDD\xD8\xE2\xEC\x20\xE1\xDF\xD8\xE1\xDE\xDA" }, // �������� ������
			{ 0, 0 }
		};
		int LoadType = mapGetByteValue(traLoadType);
		if (autGoodsCount() == 0) // ���� ������� ���
			LoadType = agltChangeList;
		else
		{
			dspClear();
			displayLS8859(0, "����� ������ ��������");
			LoadType = rgSelectEx(1, (byte)LoadType, LoadTypes);
		}
		if (LoadType >= 0)
		{
			mapPutByte(traLoadType, (byte)LoadType);

			LoadType = mapGetByteValue(traLoadType);

			byte lt;
			mapGetByte(traLoadType, lt);

			mapPutByte(traResponse, crCommonError);
			opsCommunication("\xC1\xDE\xD5\xD4\xD8\xDD\xD5\xDD\xD8\xD5", lssStartLoadAutonomousGoods, rssNotConnected); // ���������� ������������ / ����������

			if (mapGetByteValue(traResponse) == crSuccess)
			{
				if (ShowInfoAfterLoading)
				{
					// ���������� ���������� ��������
					dspClear();
					displayLS8859(CNTR(INV(0)), "���������� ��������");
					displayLS8859(1, "����     : %u", WasCount);
					displayLS8859(2, "�������� : %u", mapGetCardValue(traLoadResultReceived));
					displayLS8859(3, "���������: %u", mapGetCardValue(traLoadResultIgnored));
					displayLS8859(4, "���������: %u", mapGetCardValue(traLoadResultUpdated));
					displayLS8859(5, "���������: %u", mapGetCardValue(traLoadResultInserted));
					displayLS8859(6, "�������� : %u", dimGoods - autGoodsCount());
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
