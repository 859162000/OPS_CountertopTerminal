/*
 * opsOCP.c
 *
 *  Created on: 15.12.2011
 *      Author: Pavel
 */
#include "sdk30.h" // сначала этот хидер
#include "log.h"  // и строго только потом этот
#include "string.h"

#ifdef OFFLINE_ALLOWED

tTransactionInfo *traGetTransaction(tRSPackageInfo *RSPackage)
{
	return (tTransactionInfo *)RSPackage->Transaction;
}

// создать новую транзакцию
void traAllocate(tTransactionInfo **Tra)
{
	*Tra = memAllocate(sizeof(tTransactionInfo));
	memset(*Tra, 0, sizeof(tTransactionInfo));

	(*Tra)->CardRangeIndex = -1;

	rspInit(&(*Tra)->RSPackage, *Tra);
}

// освобождает память, выделенную фискальному чеку
static void fisFree(tFiscalReceiptInfo *Fiscal)
{
	// уничтожаем артикулы
	int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
	{
		tFiscalArticleInfo *Article = Fiscal->Articles[i];

		memFree(Article->GoodsCode);
		memFree(Article->GoodsName);
		memFree(Article->MeasureUnit);
		if (Article->Limits != NULL)
			lstFreeItems(Article->Limits);
		Article->Limits = NULL;
		memFree(Fiscal->Articles[i]);
	}
	Fiscal->ArticlesCount = 0;
	memFree(Fiscal->Articles);
	memset(Fiscal, 0, sizeof(tFiscalReceiptInfo)); // это нужно чтобы обнулить все указатели
}

// освобождает память, выделенную фискальному чеку
void rspFree(tRSPackageInfo *RS)
{
	memFree(RS->RetailScreen);
	memFree(RS->RetailPrinter);
	memFree(RS->Track1);
	memFree(RS->Track2);

	fisFree(&RS->Fiscal);
	memset(RS, 0, sizeof(tRSPackageInfo)); // это нужно чтобы обнулить все указатели
}

// освобождает память, выделенную транзакции. Всегда возвращает NULL в Tra
void traFree(tTransactionInfo **Tra)
{
	rspFree(&((*Tra)->RSPackage));
	(*Tra)->TerminalPrinter = memFree((*Tra)->TerminalPrinter);

	memset(*Tra, 0, sizeof(tTransactionInfo)); // это нужно чтобы обнулить все указатели
	*Tra = memFree(*Tra);
}

// Article functions

int64 artPrice(tFiscalArticleInfo *Article) { return Article->PriceWithoutDiscount.Current - Article->DiscountForPrice.Current; }
int64 artAmount(tFiscalArticleInfo *Article) { return Article->AmountWithoutDiscount.Current - Article->DiscountForAmount.Current; }
int64 artAmountRounded(tFiscalArticleInfo *Article) { return clcRoundAmount(artAmount(Article)); }
int64 artAmountWithoutDiscountRounded(tFiscalArticleInfo *Article) { return clcRoundAmount(Article->AmountWithoutDiscount.Current); }
int64 artQuantityRounded(tFiscalArticleInfo *Article) { return clcRoundValue(Article->Quantity.Current, 3 - Article->QuantityPrecision); }

int64 artCalculateCost(tFiscalArticleInfo *Article, int64 discountForPrice)
{
	return clcNormalizePriceQuantity(Article->Quantity.Current * (Article->PriceWithoutDiscount.Current - discountForPrice));
}

// "Вычисляет" текущее значение типа оплаты для переданной позиции. Если всё успешно, то устанавливает его у артикула (чтобы потом не вычислять) и возвращает TRUE, после чего значение можно прочитать в поле PaymentType
// Если не получено - возвращает FALSE
Bool artChangePaymentType(tFiscalArticleInfo *Article, tFiscalReceiptInfo *Fiscal)
{
	switch (Article->PaymentType)
	{
		case ptUnknown:
			Article->PaymentType = payGetSinglePaymentType(&Fiscal->Payments, ptUnknown, ptUnknown);

	        if (Article->PaymentType != ptUnknown)
	        	return TRUE;
	        else
	        	return FALSE;//PrintRefusal8859("При смешанной оплате ККМ обязана передавать тип оплаты для каждой позиции фискального чека");
		case ptAuto:
          	Article->PaymentType = payAutoDetectPaymentType(&Fiscal->Payments, Fiscal);

          	if (Article->PaymentType == ptAuto)
            	return FALSE; //throw new Exception(string.Format("Не удалось автоматически выбрать тип платежа товарной позиции с кодом {0}", GoodsCode));
            else
            	return TRUE;
		default:
			return TRUE; // изменения не требуются
	}
}

// рассчитывает бонусы по позиции, на основании переданной скидки
void artCalculateBonuses(tFiscalArticleInfo *Article, int64 DiscountPerItem)
{
	dblSetInt642(&Article->Bonuses, clcRoundAmount(clcNormalizePriceQuantity(Article->Quantity.Current * DiscountPerItem)));
}

int64 artBonusesRounded(tFiscalArticleInfo *Article) { return clcRoundAmount(Article->Bonuses.Current); }

Bool artIsAnalog(tFiscalArticleInfo *ThisArticle, tFiscalReceiptInfo *ThisFiscal, tFiscalArticleInfo *AnalogArticle, tFiscalReceiptInfo *AnalogFiscal)
{
	if (artChangePaymentType(ThisArticle, ThisFiscal) && artChangePaymentType(AnalogArticle, AnalogFiscal))
		return strcmp(ThisArticle->GoodsCode, AnalogArticle->GoodsCode) == 0 && ThisArticle->PaymentType == AnalogArticle->PaymentType;
	else
		return FALSE;
}

static void artCombine(tFiscalArticleInfo *Destination, tFiscalArticleInfo *Source)
{
    if (Destination != NULL && Source != NULL)
    {
      if (Source->Quantity.Current != 0) Destination->Quantity = Source->Quantity;
      if (Source->PriceWithoutDiscount.Current != 0) Destination->PriceWithoutDiscount = Source->PriceWithoutDiscount;
      if (Source->DiscountForPrice.Current != 0) Destination->DiscountForPrice = Source->DiscountForPrice;
      if (Source->AmountWithoutDiscount.Current != 0) Destination->AmountWithoutDiscount = Source->AmountWithoutDiscount;
      if (Source->DiscountForAmount.Current != 0) Destination->DiscountForAmount = Source->DiscountForAmount;
      if (Source->Bonuses.Current != 0) Destination->Bonuses = Source->Bonuses;
      if (Source->Flags.Current != fafNone) Destination->Flags = Source->Flags;
      if (Source->PaymentType != ptUnknown) Destination->PaymentType = Source->PaymentType;
    }
}

// Payment functions

int64 payAmount(tFiscalPaymentsInfo *Payments) { return Payments->AmountCash.Current + Payments->AmountBankingCard.Current + Payments->AmountBonuses.Current + Payments->AmountCredit.Current + Payments->AmountPrepaidAccount.Current; }
int64 payAmountRounded(tFiscalPaymentsInfo *Payments) { return clcRoundAmount(payAmount(Payments)); }

void payIncreaseAmountByPaymentType(tFiscalPaymentsInfo *Payments, ocpPaymentTypes PaymentType, int64 Value)
{
	switch (PaymentType)
	{
		case ptCash: Payments->AmountCash.Current += Value; break;
		case ptBankingCard: Payments->AmountBankingCard.Current += Value; break;
		case ptBonuses: Payments->AmountBonuses.Current += Value; break;
		case ptCredit: Payments->AmountCredit.Current += Value; break;
		case ptPrepaidAccount: Payments->AmountPrepaidAccount.Current += Value; break;
		default: printS("IncreaseAmountByPaymentType: Unknown payment type: %d\n", PaymentType);
	}
}

int64 payGetAmountByPaymentType(tFiscalPaymentsInfo *Payments, ocpPaymentTypes PaymentType)
{
	switch (PaymentType)
	{
		case ptCash: return Payments->AmountCash.Current;
		case ptBankingCard: return Payments->AmountBankingCard.Current;
		case ptBonuses: return Payments->AmountBonuses.Current;
		case ptCredit: return Payments->AmountCredit.Current;
		case ptPrepaidAccount: return Payments->AmountPrepaidAccount.Current;
		default: printS("IncreaseAmountByPaymentType: Unknown payment type: %d\n", PaymentType);
	}
	return 0;
}

ocpPaymentTypes payGetAnyPaymentTypeForFlags(ocpPaymentTypeFlags Flags, ocpPaymentTypes EmptyFlagsPaymentType)
{
	if (HAS_FLAG(Flags, ptfByCash)) return ptCash;
	if (HAS_FLAG(Flags, ptfByBankingCard)) return ptBankingCard;
	if (HAS_FLAG(Flags, ptfByBonuses)) return ptBonuses;
	if (HAS_FLAG(Flags, ptfByCredit)) return ptCredit;
	if (HAS_FLAG(Flags, ptfByPrepaidAccount)) return ptPrepaidAccount;
	else return EmptyFlagsPaymentType;
}

// Возвращает первый попавшийся ненулевой тип оплаты. Если нет платежей, то возвращает ZeroPaymentType
ocpPaymentTypes payGetAnyPaymentType(tFiscalPaymentsInfo *Payments, ocpPaymentTypes ZeroPaymentType)
{
  if (Payments->AmountCash.Current != 0) return ptCash;
  else if (Payments->AmountBankingCard.Current != 0) return ptBankingCard;
  else if (Payments->AmountBonuses.Current != 0) return ptBonuses;
  else if (Payments->AmountCredit.Current != 0) return ptCredit;
  else if (Payments->AmountPrepaidAccount.Current != 0) return ptPrepaidAccount;

  return ZeroPaymentType;
}

// Вощвращает тип оплаты, который является единственным в переданной структуре. Если платежи не переданы, то возвращает ZeroPaymentType. Если нет единого платежа, то возвращает MixedPaymentType
ocpPaymentTypes payGetSinglePaymentType(tFiscalPaymentsInfo *Payments, ocpPaymentTypes ZeroPaymentType, ocpPaymentTypes MixedPaymentType)
{
  int64 A = payAmount(Payments);
  if (A == 0) return ZeroPaymentType;
  else if (A == Payments->AmountCash.Current) return ptCash; // если сумма оплаты совпадает с суммой оплаты деньгами - значит и позицию фискального чека оплатили деньгами
  else if (A == Payments->AmountBankingCard.Current) return ptBankingCard;
  else if (A == Payments->AmountBonuses.Current) return ptBonuses;
  else if (A == Payments->AmountCredit.Current) return ptCredit;
  else if (A == Payments->AmountPrepaidAccount.Current) return ptPrepaidAccount;

  return MixedPaymentType;
}

// Пытается автоматически определить наиболее часто используемый тип оплаты. Если не может - возвращает Auto
ocpPaymentTypes payAutoDetectPaymentType(tFiscalPaymentsInfo *Payments, tFiscalReceiptInfo *Fiscal)
{
	ocpPaymentTypes Result = ptAuto;
	if (payAmount(Payments) != 0) // сначала анализируем платежи ФЧ
		Result = payGetAnyPaymentType(Payments, ptAuto);

	if (Result == ptAuto && ocsGet() != NULL) // затем разрешенные типы оплаты для типа карты
		Result = payGetAnyPaymentTypeForFlags(ocsGet()->GetPaymentTypes(), ptAuto);

	return Result;
}

static Bool payChangePaymentTypeForDiscount(tFiscalPaymentsInfo *Payments, tFiscalReceiptInfo *Fiscal)
{
    switch (Payments->PaymentTypeForDiscount)
    {
    	case ptUnknown:
    		Payments->PaymentTypeForDiscount = payGetSinglePaymentType(Payments, ptUnknown, ptUnknown);
    		if (Payments->PaymentTypeForDiscount == ptUnknown)
    		{
    			PrintRefusal8859("Не удаётся автоматически определить тип платежа, с которого списать скидку фискального чека. Необходимо передать поле PaymentTypeForDiscount");
    			return FALSE;
    		}
    		else
    			return TRUE;
    	case ptAuto:
           	Payments->PaymentTypeForDiscount = payAutoDetectPaymentType(Payments, Fiscal);

    		if (Payments->PaymentTypeForDiscount == ptAuto)
    		{
    			PrintRefusal8859("Не удалось автоматически выбрать тип платежа, с которого будет списана скидка фискального чека");
    			return FALSE;
    		}
            return TRUE;
    	default:
    		return TRUE;
    }
}

static void payInitializeFromArticles(tFiscalPaymentsInfo *Payments, tFiscalReceiptInfo *Fiscal)
{
	dblSetInt642(&Payments->AmountCash, fisArticlesAmountByPaymentType(Fiscal, ptCash));
	dblSetInt642(&Payments->AmountBankingCard, fisArticlesAmountByPaymentType(Fiscal, ptBankingCard));
	dblSetInt642(&Payments->AmountBonuses, fisArticlesAmountByPaymentType(Fiscal, ptBonuses));
	dblSetInt642(&Payments->AmountCredit, fisArticlesAmountByPaymentType(Fiscal, ptCredit));
	dblSetInt642(&Payments->AmountPrepaidAccount, fisArticlesAmountByPaymentType(Fiscal, ptPrepaidAccount));
}

// возвращает true, если переданный тип оплаты находится среди разрешенных флагов оплаты
Bool payPaymentTypeInFlags(ocpPaymentTypes PT, ocpPaymentTypeFlags PTF)
{
	switch (PT)
	{
		case ptCash: return HAS_FLAG(PTF, ptfByCash);
		case ptBankingCard: return HAS_FLAG(PTF, ptfByBankingCard);
		case ptBonuses: return HAS_FLAG(PTF, ptfByBonuses);
		case ptCredit: return HAS_FLAG(PTF, ptfByCredit);
		case ptPrepaidAccount: return HAS_FLAG(PTF, ptfByPrepaidAccount);
		default: return FALSE;
	}
}

static void payCombine(tFiscalPaymentsInfo *Destination, tFiscalPaymentsInfo *Source)
{
    if (Destination != NULL && Source != NULL)
    {
      if (Source->AmountCash.Current != 0) Destination->AmountCash = Source->AmountCash;
      if (Source->AmountBankingCard.Current != 0) Destination->AmountBankingCard = Source->AmountBankingCard;
      if (Source->AmountBonuses.Current != 0) Destination->AmountBonuses = Source->AmountBonuses;
      if (Source->AmountCredit.Current != 0) Destination->AmountCredit = Source->AmountCredit;
      if (Source->AmountPrepaidAccount.Current != 0) Destination->AmountPrepaidAccount = Source->AmountPrepaidAccount;
      if (Source->PaymentTypeForDiscount != ptUnknown) Destination->PaymentTypeForDiscount = Source->PaymentTypeForDiscount;
    }
}

// Fiscal functions

void fisInit(tFiscalReceiptInfo *Fiscal, tRSPackageInfo *RSPackage)
{
	memset(Fiscal, 0, sizeof(tFiscalReceiptInfo));
	Fiscal->RSPackage = RSPackage; // указатель на родительский пакет от ККМ
}

// create new article and return his index
int fisCreateArticle(tFiscalReceiptInfo *Fiscal)
{
	int Index = Fiscal->ArticlesCount;
	Fiscal->ArticlesCount++;
	Fiscal->Articles = memReallocate(Fiscal->Articles, Fiscal->ArticlesCount * sizeof(tFiscalArticleInfo *)); // изменяем размер массива артикулов
	Fiscal->Articles[Index] = memAllocate(sizeof(tFiscalArticleInfo)); // выделяем память под новый артикул
	tFiscalArticleInfo *Article = Fiscal->Articles[Index];
	Article->QuantityPrecision = 3;

	return Index;
}

int64 fisArticlesAmount(tFiscalReceiptInfo *Fiscal)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += artAmount(Fiscal->Articles[i]);
	return Result;
}

int64 fisArticlesAmountRounded(tFiscalReceiptInfo *Fiscal)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += artAmountRounded(Fiscal->Articles[i]);
	return Result;
}

int64 fisAmount(tFiscalReceiptInfo *Fiscal) { return Fiscal->AmountWithoutDiscount.Current - Fiscal->DiscountForAmount.Current; }
int64 fisAmountRounded(tFiscalReceiptInfo *Fiscal) { return clcRoundAmount(fisAmount(Fiscal)); }

int64 fisBonusesRounded(tFiscalReceiptInfo *Fiscal)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += artBonusesRounded(Fiscal->Articles[i]);
	return Result;
}

int64 fisQuantity(tFiscalReceiptInfo *Fiscal)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += Fiscal->Articles[i]->Quantity.Current;
	return Result;
}

int64 fisArticlesAmountByPaymentType(tFiscalReceiptInfo *Fiscal, ocpPaymentTypes pt)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		if (artChangePaymentType(Fiscal->Articles[i], Fiscal))
			if (Fiscal->Articles[i]->PaymentType == pt)
				Result += artAmount(Fiscal->Articles[i]);
	return Result;

}

int64 fisArticlesAmountWithoutDiscountRounded(tFiscalReceiptInfo *Fiscal)
{
	int64 Result = 0; int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += artAmountWithoutDiscountRounded(Fiscal->Articles[i]);
	return Result;
}

Bool fisSetQuantityToZero(tFiscalReceiptInfo *Fiscal)
{
	int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
	{
		Fiscal->Articles[i]->Quantity.Initial = 0;
		Fiscal->Articles[i]->Quantity.Current = 0;
		Fiscal->Articles[i]->Quantity.Flags = dtfEmpty;
	}
	return TRUE;
}

tFiscalArticleInfo *fisFindAnalog(tFiscalReceiptInfo *Fiscal, tFiscalArticleInfo *ArticleToFind, tFiscalReceiptInfo *FiscalToFind)
{
	int i;
    for (i = 0; i < Fiscal->ArticlesCount; i++)
    {
    	tFiscalArticleInfo *Article = Fiscal->Articles[i];
    	if (Article != NULL && artIsAnalog(Article, Fiscal, ArticleToFind, FiscalToFind))
    		return Article;
    }
    return NULL;
}

static void fisCombine(tFiscalReceiptInfo *Destination, tFiscalReceiptInfo *Source)
{
	if (Destination != NULL && Source != NULL)
	{
	    if (Source->Flags.Current != frfNone) Destination->Flags = Source->Flags;
	    if (Source->AmountWithoutDiscount.Current != 0) Destination->AmountWithoutDiscount = Source->AmountWithoutDiscount;
	    if (Source->DiscountForAmount.Current != 0) Destination->DiscountForAmount = Source->DiscountForAmount;

	    payCombine(&Destination->Payments, &Source->Payments);

	    // теперь объединяем артикулы: пробегаем по всем артикулам переданного чека
	    int i;
	    for (i = 0; i < Source->ArticlesCount; i++)
	    {
	      tFiscalArticleInfo *Analog = NULL, *Article = Source->Articles[i];
	      if (Article->GoodsCode != NULL && strlen(Article->GoodsCode) != 0 && (artChangePaymentType(Article, Source) && Article->PaymentType != ptUnknown)) // если переданы код и тип оплаты - ищем товар
	        Analog = fisFindAnalog(Destination, Article, Source); // ищем позицию-аналог в чеке-результате

	      if (Analog == NULL)
	    	Analog = Destination->Articles[i]; // если код или тип оплаты не переданы - получаем аналог по индексу

	      if (Analog != NULL)
	    	  artCombine(Analog, Article); //  если что-то нашли, то объединяем результирующую позицию и текущую
	    }
	}
}

static const ocpPaymentTypes ValidPaymentTypes[] = { ptCash, ptBankingCard, ptBonuses, ptCredit, ptPrepaidAccount };
static const int ValidPaymentTypesCount = sizeof(ValidPaymentTypes) / sizeof(ocpPaymentTypes);

static void artCalculate(tFiscalArticleInfo *Article, tFiscalReceiptInfo *Fiscal);

void fisPaymentPriorityCalculate(tFiscalReceiptInfo *Fiscal)
{
	word PaymentArticlesCount = 0; tFiscalArticleInfo *PaymentArticles[Fiscal->ArticlesCount]; double PaymentAmount = 0.0;
	word FlaggedArticlesCount = 0; tFiscalArticleInfo *FlaggedArticles[Fiscal->ArticlesCount]; double FlaggedAmount = 0.0;

	int i, j, k;
	for (i = 0; i < ValidPaymentTypesCount; i++)
	{
		ocpPaymentTypes PaymentType = ValidPaymentTypes[i];
		double CurrentPayment = clcValueToNaturalAmount(payGetAmountByPaymentType(&Fiscal->Payments, PaymentType));
		if (CurrentPayment != 0.0)
		{
			// ищем PaymentArticles
			PaymentArticlesCount = 0; PaymentAmount = 0.0;
			FlaggedArticlesCount = 0; FlaggedAmount = 0.0;
			for (j = 0; j < Fiscal->ArticlesCount; j++)
			{
				tFiscalArticleInfo *Article = Fiscal->Articles[j];

				if (Article->PaymentType == PaymentType && artPrice(Article) != 0 && Article->Quantity.Current != 0)
				{
					PaymentArticles[PaymentArticlesCount] = Article;
					PaymentArticlesCount++;
					PaymentAmount += clcValueToNaturalAmount(artAmount(Article)); // суммарная стоимость всех позиций для текущего типа платежа

					// ищем FlaggedArticles
					if (HAS_FLAG(Article->Flags.Current, fafDiscountByQuantity))
					{
						FlaggedArticles[FlaggedArticlesCount] = Article;
						FlaggedArticlesCount++;
						FlaggedAmount += clcValueToNaturalAmount(artAmount(Article)); // суммарная стоимость всех позиций с установленным флагом
					}
				}
			}

			if (PaymentArticlesCount > 0)
			{
				if (FlaggedArticlesCount > 0)
				{
					double NonFlaggedAmount = PaymentAmount - FlaggedAmount; // суммарная стоимость всех позиций без установленного флага
					double BalanceAmount = CurrentPayment - NonFlaggedAmount; // сумма платежа без сумм тех позиций, которые нельзя трогать при распределении (баланс)
					double DistributionAmount = BalanceAmount - FlaggedAmount; // сумма для распределения между позициями с установленным флагом
					if (BalanceAmount >= 0.0)
					{
		                if (DistributionAmount != 0 && FlaggedAmount != 0) // по идее, если сумма для распределения равна нулю, то распределять нечего и можно просто проигнорировать
		                {
		                	for (k = 0; k < FlaggedArticlesCount; k++)
		                	{
		                		tFiscalArticleInfo *Article = FlaggedArticles[k];

		                		double Delta = (DistributionAmount * (clcValueToNaturalAmount(artAmount(Article)) / FlaggedAmount)) / clcValueToNaturalAmount(artPrice(Article)); // вычисляем и округляем дельту
			                    dblSetInt642(&Article->Quantity, Article->Quantity.Current + clcNaturalToValueQuantity(Delta)); // изменили Quantity
			                    dblSetInt642(&Article->Quantity, artQuantityRounded(Article)); // округлили Quantity
			                    artCalculate(Article, Fiscal); // пересчитали позицию чека
		                	}
		                }
					}
					else
						printS("Балансовая сумма %f по типу оплаты %d меньше нуля. Клиент должен заплатить отрицительную сумму, что неприемлимо\n", BalanceAmount, PaymentType);
				}
				else
					printS("Не удаётся найти среди позиций чека с типом оплаты %d те, у которых был бы установлен флаг DiscountByQuantity\n", PaymentType);
			}
			else
	            printS("Не удаётся найти ненулевые позиции чека с типом платежа %d, хотя в Payments для этого типа оплаты передано значение %l\n", PaymentType, PaymentAmount);
		}
	}
}

// Применение выравнивающих скидок к позициям чека для приведения сумм чека к суммам платежей.
// После вызова этой функции нельзя вызывать функцию Article.Calculate(), т.к. будет очищена выравнивающая скидка
static void fisEqualizationByArticlesDiscount(tFiscalReceiptInfo *Fiscal)
{
	word PaymentArticlesCount = 0;
	tFiscalArticleInfo *PaymentArticles[Fiscal->ArticlesCount];
	int64 PaymentAmount = 0;

	int i, j;
	for (i = 0; i < ValidPaymentTypesCount; i++)
	{
		ocpPaymentTypes PaymentType = ValidPaymentTypes[i];
		int64 CurrentPayment = payGetAmountByPaymentType(&Fiscal->Payments, PaymentType);
		if (CurrentPayment != 0)
		{
			// ищем PaymentArticles
			PaymentArticlesCount = 0; PaymentAmount = 0;
			for (j = 0; j < Fiscal->ArticlesCount; j++)
			{
				tFiscalArticleInfo *Article = Fiscal->Articles[j];

				if (Article->PaymentType == PaymentType && artPrice(Article) != 0 && Article->Quantity.Current != 0)
				{
					PaymentArticles[PaymentArticlesCount] = Article;
					PaymentArticlesCount++;
					PaymentAmount += artAmount(Article); // суммарная стоимость всех позиций для текущего типа платежа
				}
			}

			int64 Discount = PaymentAmount - CurrentPayment;
			Bool DiscountWasApplied = FALSE;
			for (j = 0; j < PaymentArticlesCount; j++) // ищем позицию чека, к которой мы применим выравнивающую скидку
			{
				tFiscalArticleInfo *Article = PaymentArticles[j];
				if (artAmount(Article) >= Discount) // условие - итоговая стоимость позиции чека должна не меньше скидки
				{
					int64 NewArticlePrice = clcDivideAmountQuantityForPrice(artAmount(Article) - Discount, Article->Quantity.Current); // получаем новую цену товара с учётом новой скидки
					dblSetInt642(&Article->DiscountForPrice, Article->DiscountForPrice.Current + artPrice(Article) - NewArticlePrice); // корректируем скидку на цену товара для подгонки под новую цену
					artCalculate(Article, Fiscal);

					DiscountWasApplied = TRUE;
					break;
				}
			}
			if (!DiscountWasApplied)
				printS("Выравнивающая скидка в размере '%d' не была применена к типу оплаты '%d': нет позиций в чеке со стоимостью не менее выравнивающей скидки\n", Discount, PaymentType);
		}
	}

}

// calculations

static void payCalculate(tFiscalPaymentsInfo *Payments, tFiscalReceiptInfo *Fiscal)
{
	if (!HAS_FLAG(Fiscal->Flags.Current, frfWithoutRecalculation))
	{
		int64 AmountForCompare = payAmount(Payments);

		// изменяем суммы оплаты, суммируя позиции фискального чека, разбитые по типам оплаты
		dblSetInt642(&Payments->AmountCash, fisArticlesAmountByPaymentType(Fiscal, ptCash));
		dblSetInt642(&Payments->AmountBankingCard, fisArticlesAmountByPaymentType(Fiscal, ptBankingCard));
		dblSetInt642(&Payments->AmountBonuses, fisArticlesAmountByPaymentType(Fiscal, ptBonuses));
		dblSetInt642(&Payments->AmountCredit, fisArticlesAmountByPaymentType(Fiscal, ptCredit));
		dblSetInt642(&Payments->AmountPrepaidAccount, fisArticlesAmountByPaymentType(Fiscal, ptPrepaidAccount));

		// если на фискальный чек предоставлена скидка, учитываем её в платежах
		if (Fiscal->DiscountForAmount.Current != 0)
		{
			if (payChangePaymentTypeForDiscount(Payments, Fiscal))
				payIncreaseAmountByPaymentType(Payments, Payments->PaymentTypeForDiscount, -Fiscal->DiscountForAmount.Current);
		}

		// округляем все суммы оплаты c учётом предоставленной скидки (=> в Amount у нас лежит округлённая сумма)
		// округлить сразу нельзя из-за предоставления суммарной скидки на чек
		dblSetInt642(&Payments->AmountCash, clcRoundAmount(Payments->AmountCash.Current));
		dblSetInt642(&Payments->AmountBankingCard, clcRoundAmount(Payments->AmountBankingCard.Current));
		dblSetInt642(&Payments->AmountBonuses, clcRoundAmount(Payments->AmountBonuses.Current));
		dblSetInt642(&Payments->AmountCredit, clcRoundAmount(Payments->AmountCredit.Current));
		dblSetInt642(&Payments->AmountPrepaidAccount, clcRoundAmount(Payments->AmountPrepaidAccount.Current));

		// если округлённая итоговая сумма платежа не совпадает с итоговой суммой фискального чека (сумма фискального чека уже должна быть округлена с помощью скидки)
		if (payAmountRounded(Payments) != fisAmount(Fiscal))
		{
			if (payChangePaymentTypeForDiscount(Payments, Fiscal))
				payIncreaseAmountByPaymentType(Payments, Payments->PaymentTypeForDiscount, fisAmount(Fiscal) - payAmountRounded(Payments)); // корректируем сумму на разницу в суммах
		}

		if (AmountForCompare != payAmount(Payments)) // если были изменения относительно изначального Amount
		  Fiscal->Flags.Current |= frfPaymentsWasChanged;
	}
}

static void artCalculate(tFiscalArticleInfo *Article, tFiscalReceiptInfo *Fiscal)
{
	if (!HAS_FLAG(Fiscal->Flags.Current, frfWithoutRecalculation))
	{
		if (Article->Quantity.Current != 0)
		{
			dblSetInt642(&Article->Quantity, artQuantityRounded(Article)); // округляем Quantity
			dblSetInt642(&Article->AmountWithoutDiscount, artCalculateCost(Article, 0)); // Пересчитываем стоимость чека без скидки (кто его знает что прислала ККМ)


			// корректируем DFA чтобы связать DFP и DFA (нельзя использовать AmountWithoutDiscount)
			dblSetInt642(&Article->DiscountForAmount, Article->DiscountForAmount.Current + artAmount(Article) - artCalculateCost(Article, Article->DiscountForPrice.Current));
			// см. (1)
			dblSetInt642(&Article->DiscountForAmount, Article->DiscountForAmount.Current + artAmount(Article) - artAmountRounded(Article));
	  	    // (1) Теоретически можно не не округлять Amount позиции чека - все позиции чека будут округлены в FiscalReceipt предоставление суммовой скидки,
		    // НО при сохранении чека в Oracle у нас не полнюстью повторяется структура чека: чек суммируется как sum(Receipt.AmountRounded) - Fiscal.Discount.
		    // В итоге у нас позиция чека ДОЛЖНА быть обязательно округлена
		}
		else
		{
			dblSetInt642(&Article->AmountWithoutDiscount, 0);
			dblSetInt642(&Article->DiscountForAmount, 0);
		}
	}
}

static void fisCalculate(tFiscalReceiptInfo *Fiscal)
{
	int i;
	if (!HAS_FLAG(Fiscal->Flags.Current, frfWithoutRecalculation) && fisQuantity(Fiscal) > 0) //  если не установлен флаг ненужности пересчёта И "если в чеке ещё осталось количество..." или "если в чеке есть данные"
	{
        // если Payments не передан ИЛИ передан с нулевой суммой, то мы пытаемся инициализировать суммарные платежи из артикулов
        if (payAmount(&Fiscal->Payments) == 0)
          payInitializeFromArticles(&Fiscal->Payments, Fiscal);

		for (i = 0; i < Fiscal->ArticlesCount; i++)
			artCalculate(Fiscal->Articles[i], Fiscal); // производим пересчёт каждой позиции чека

		dblSetInt642(&Fiscal->AmountWithoutDiscount, fisArticlesAmount(Fiscal));
		dblSetInt642(&Fiscal->DiscountForAmount, fisAmount(Fiscal) - fisAmountRounded(Fiscal));

		// PaymentsPriority не поддерживается? Видимо да - потому что эти пересчёты будут потом
		if (!HAS_FLAG(Fiscal->Flags.Current, frfPaymentsPriority))
		{
			payCalculate(&Fiscal->Payments, Fiscal);
		}
		else if (fisAmount(Fiscal) != payAmount(&Fiscal->Payments)) // если суммы чека и платежа не совпадают, начинаем процедуру выравнивания
		{
			fisPaymentPriorityCalculate(Fiscal); // корректируем количество товаров по используемым видам оплаты

			dblSetInt642(&Fiscal->AmountWithoutDiscount, fisArticlesAmount(Fiscal));  // теперь корректируем сам фискальный чек: заново рассчитываем сумму без скидки
			dblSetInt642(&Fiscal->DiscountForAmount, 0); // обнуляем скидку на сумму чека

            // и выравниваем итоговую сумму фискального чека с помощью скидки
            if (HAS_FLAG(Fiscal->Flags.Current, frfEqualizationByDiscountForAmount))
            	dblSetInt642(&Fiscal->DiscountForAmount, Fiscal->AmountWithoutDiscount.Current - payAmount(&Fiscal->Payments)); // суммовая скидка на весь чек (разница между суммой всех товарных позиций и суммой платежа
            else
            {
              fisEqualizationByArticlesDiscount(Fiscal); // Предоставление индивидуальной скидки на каждую позицию фискального чека для сведения сумм по каждому виду оплаты
              dblSetInt642(&Fiscal->AmountWithoutDiscount, fisArticlesAmount(Fiscal));  // теперь корректируем сам фискальный чек: заново рассчитываем сумму без скидки
              dblSetInt642(&Fiscal->DiscountForAmount, Fiscal->AmountWithoutDiscount.Current - payAmount(&Fiscal->Payments)); //... и скидку на сумму фискального чека (если есть разница между суммой чека и суммой оплаты)
            }
		}
	}
}

void rspInit(tRSPackageInfo *RSPackage, tTransactionInfo *Transaction)
{
	memset(RSPackage, 0, sizeof(tRSPackageInfo));

	RSPackage->Transaction = Transaction; // указатель на родительскую транзакцию
	fisInit(&RSPackage->Fiscal, RSPackage);
}

int64 rspAmountRounded(tRSPackageInfo *RSPackage)
{
	return fisAmountRounded(&RSPackage->Fiscal);
}

void rspCalculate(tRSPackageInfo *RSPackage)
{
	fisCalculate(&RSPackage->Fiscal);
	dblSetInt642(&RSPackage->Amount, fisAmount(&RSPackage->Fiscal));
	RSPackage->WasCalculated = TRUE;
}

Bool rspCombine(tRSPackageInfo *Destination, tRSPackageInfo *Source)
{
    if (Destination != NULL && Source != NULL)
    {
      if (Source->Amount.Current != 0) Destination->Amount = Source->Amount;
      if (Source->Flags.Current != rsfNone) Destination->Flags = Source->Flags;
      if (Source->RSTime != 0) Destination->RSTime = Source->RSTime;
      if (Source->SelectedBehaviourID != 0) Destination->SelectedBehaviourID = Source->SelectedBehaviourID;
      fisCombine(&Destination->Fiscal, &Source->Fiscal);
    }
    return TRUE;
}

/*// преобразует дату OCP в Unix-дату
static card OCPValueToDate(byte *date, card len)
{
	char sDateValue[lenDatTim];
	BCDToString(date, len, sDateValue);
	if (strlen(sDateValue) < 14)
		return 0;
	else
		return UTADateTimeToUnix(sDateValue);
}*/

static void ocpReadArticle(tBuffer *buf, tFiscalArticleInfo *Article)
{
	tlvReader tr;
    byte tag;
    card len;
    byte *value;
    tlvrInit(&tr, buf->ptr, buf->dim);
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
    	value = memAllocate(len + 1); // +1 for strings
    	value[len] = 0; // trailing zero for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case fatGoodsCode:
				memFree(Article->GoodsCode);
				Article->GoodsCode = memAllocate(len + 1);
				tlv2string(value, len, Article->GoodsCode);
				strTranslate(Article->GoodsCode, Article->GoodsCode, rsCodePage(), cpDefault);
				break;
			case fatQuantity: dblSetInt64(&Article->Quantity, tlv2card(value)); break;
			case fatPriceWithoutDiscount: dblSetInt64(&Article->PriceWithoutDiscount, tlv2card(value)); break;
			case fatAmountWithoutDiscount: dblSetInt64(&Article->AmountWithoutDiscount, tlv2card(value)); break;
			case fatGoodsName:
				memFree(Article->GoodsName);
				Article->GoodsName = memAllocate(len + 1);
				tlv2string(value, len, Article->GoodsName);
				strTranslate(Article->GoodsName, Article->GoodsName, rsCodePage(), cpDefault);
				break;
			case fatFlags: dblSetByte(&Article->Flags, value[0]); break;
			case fatDiscountForPrice: dblSetInt64(&Article->DiscountForPrice, tlv2card(value)); break;
			case fatDiscountForAmount: dblSetInt64(&Article->DiscountForAmount, tlv2card(value)); break;
			case fatBonuses: dblSetInt64(&Article->Bonuses, tlv2card(value)); break;
			case fatPaymentType: Article->PaymentType = value[0]; break;
			case fatMeasureUnit:
				memFree(Article->MeasureUnit);
				Article->MeasureUnit = memAllocate(len + 1);
				tlv2string(value, len, Article->MeasureUnit);
				strTranslate(Article->MeasureUnit, Article->MeasureUnit, rsCodePage(), cpDefault);
				break;
			case fatQuantityPrecision: Article->QuantityPrecision = value[0]; break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
	}
}

static void ocpReadPayments(tBuffer *buf, tFiscalPaymentsInfo *Payments)
{
	tlvReader tr;
    byte tag;
    card len;
    byte *value;
    tlvrInit(&tr, buf->ptr, buf->dim);
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case fptAmountCash: dblSetInt64(&Payments->AmountCash, tlv2card(value)); break;
			case fptAmountBankingCard: dblSetInt64(&Payments->AmountBankingCard, tlv2card(value)); break;
			case fptAmountBonuses: dblSetInt64(&Payments->AmountBonuses, tlv2card(value)); break;
			case fptAmountCredit: dblSetInt64(&Payments->AmountCredit, tlv2card(value)); break;
			case fptAmountPrepaidAccount: dblSetInt64(&Payments->AmountPrepaidAccount, tlv2card(value)); break;
			case fptPaymentTypeForDiscount: Payments->PaymentTypeForDiscount = value[0]; break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
	}
}

// очистить фискальный чек перед чтением партии товаров
static void ocpClearFiscal(tFiscalReceiptInfo *Fiscal)
{
	Fiscal->ArticlesCount = 0;
	memset(&Fiscal->Articles, 0, sizeof(Fiscal->Articles));
}

static void ocpReadFiscal(tBuffer *buf, tFiscalReceiptInfo *Fiscal)
{
	ocpClearFiscal(Fiscal); // очищаем чек перед началом разбора (удаляем все позиции). 02/03/2015 так сделано, чтобы не очищать мусор в Fiscal.Articles
	// при реальном использовании лучше использовать fisFree()

	tlvReader tr;
    byte tag;
    card len;
    byte *value;
    tlvrInit(&tr, buf->ptr, buf->dim);
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case rftArticle:
				{   // товарная позиция
					int Index = fisCreateArticle(Fiscal);
					tBuffer articleBuf;
					tlvBufferInit(&articleBuf, value, len);
					ocpReadArticle(&articleBuf, Fiscal->Articles[Index]);
				}
				break;
			case rftFlags: dblSetByte(&Fiscal->Flags, value[0]); break;
			case rftAmountWithoutDiscount: dblSetInt64(&Fiscal->AmountWithoutDiscount, tlv2card(value)); break;
			case rftDiscountForAmount: dblSetInt64(&Fiscal->DiscountForAmount, tlv2card(value)); break;
			case rftPayments:
				{	// payments
					tBuffer paymentsBuf;
					tlvBufferInit(&paymentsBuf, value, len);
					ocpReadPayments(&paymentsBuf, &(Fiscal->Payments));
				}
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
	}
}


// читает буфер из системы управления и размещает его данные в транзакции
void ocpReadRSPackage(tBuffer *buf, tRSPackageInfo *RSPackage)
{
	if (!RSPackage) return;

	RSPackage->FiscalReceived = FALSE;
	tTransactionInfo *Transaction = (tTransactionInfo*)RSPackage->Transaction;

	tlvReader tr;
    byte tag, *value;
    card len;
    tlvrInit(&tr, buf->ptr, buf->dim);
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
    	value = memAllocate(len + 1); // +1 for strings
    	value[len] = 0; // trailing zero for string
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case rstPackageID: dblSetByte(&RSPackage->PackageID, value[0]); break;
			case rstResponse: dblSetByte(&RSPackage->Response, value[0]); break;
			case rstAmount: dblSetInt64(&RSPackage->Amount, tlv2card(value)); break;
			case rstFlags: dblSetUInt16(&RSPackage->Flags, tlv2word(value)); break;
			case rstDateTime: RSPackage->RSTime = tlv2card(value); break;
			case rstScreenMessage:
				memFree(RSPackage->RetailScreen);
				RSPackage->RetailScreen = memAllocate(len + 1);
				tlv2string(value, len, RSPackage->RetailScreen); // screen message в cp866
				break;
			case rstPrinterMessage:
				memFree(RSPackage->RetailPrinter);
				RSPackage->RetailPrinter = memAllocate(len + 1);
				tlv2string(value, len, RSPackage->RetailPrinter); // printer message в cp866
				break;
			case rstFiscalReceipt: // fiscal receipt
				{
					// уничтожаем Tra->Fiscal, если раньше он был прислан
					RSPackage->FiscalReceived = TRUE;
					RSPackage->WasCalculated = FALSE; // отменяем пересчёт при получении нового фискального чека
					fisFree(&RSPackage->Fiscal); // освобождаем память, занятую предыдущим фискальным чеком

					tBuffer fiscalBuf;
					tlvBufferInit(&fiscalBuf, value, len);
					ocpReadFiscal(&fiscalBuf, &RSPackage->Fiscal);
				}
				break;
			case rstPurchaseID: // ID транзакции (покупки). Не должно присылаться из ККМ (здесь только для offline-транзакций)
				ocpBCDToTransactionID(RSPackage->PurchaseID, value, len);
				break;
			case rstCancellationID: // ID транзакции отмены Не должно присылаться из ККМ (здесь только для offline-транзакций)
				ocpBCDToTransactionID(RSPackage->CancellationID, value, len);
				break;
			case rstOriginalID: // ID отменяемой транзакции
				ocpBCDToTransactionID(RSPackage->OriginalID, value, len);
				break;
			case rstCardID:
				if (Transaction != NULL)
				{
					tlv2string(value, len, Transaction->CardNumber);
					strLeftPad(Transaction->CardNumber, '0', lenCardNumberPrint);
				}
				break; // номер карты
			case rstReceiptNumber: RSPackage->ReceiptNumber = tlv2card(value); break;
			case rstECRID: RSPackage->ECRID = tlv2card(value); break;
			case rstShopID: RSPackage->ShopID = tlv2card(value); break;
			case rstCardType: /*RSPackage->CardType = value[0];*/ break; // Это не должно присылаться от СУ ККМ | Добавлено 18/06/2014: Но может быть прочитано при загрузке транзакции из пакета.
			case rstSelectedBehaviourID: RSPackage->SelectedBehaviourID = value[0]; break;
			case rstAllowedPaymentTypes: /*RSPackage->AllowedPaymentTypes = tlv2word(value);*/ break;
			case rstOperation: RSPackage->Operation = value[0]; break;
			case rstRRN: tlv2string(value, len, RSPackage->RRN); break;
			case rstAuthCode: tlv2string(value, len, RSPackage->AuthCode); break;
			case rstTrack1:
				RSPackage->Track1 = memAllocate(len + 1);
				tlv2string(value, len, RSPackage->Track1);
				break;
			case rstTrack2:
				RSPackage->Track2 = memAllocate(len + 1);
				tlv2string(value, len, RSPackage->Track2);
				break;
			default:
				printS("Tag %04X not implemented in ocpReadRSPackage()\n", tag);
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
	}
}

// читает транзакцию из буфера
void ocpReadTransaction(tBuffer *buf, tTransactionInfo *Transaction)
{
	if (!Transaction) return;
	tlvReader tr;
    byte tag, *value;
    card len;
    tlvrInit(&tr, buf->ptr, buf->dim);
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
    	value = memAllocate(len + 1); // +1 for strings
    	value[len] = 0; // trailing zero for string
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case srtPackageID: Transaction->PackageID = value[0]; break;
			case srtFlags: Transaction->Flags = tlv2word(value); break;
			case srtBehaviorFlags: Transaction->BehaviorFlags = tlv2word(value); break;
			case srtTerminalID: Transaction->TerminalID = tlv2card(value); break;
			case srtCardID:
				BCDToString(value, len, Transaction->CardNumber);
				strLeftPad(Transaction->CardNumber, '0', lenCardNumberPrint);
				break;
			case srtTerminalDateTime: Transaction->TerminalTime = tlv2card(value); break;
			case srtTransactionType: Transaction->Type = value[0]; break;
			case srtRetailSystemPackage:
				{
					// уничтожаем Tra->RSPackage, если раньше он был прислан
					rspFree(&Transaction->RSPackage); // освобождаем память, занятую предыдущим пакетом из СУ ККМ

					tBuffer rsBuf;
					tlvBufferInit(&rsBuf, value, len);
					ocpReadRSPackage(&rsBuf, &Transaction->RSPackage);
				}
				break;

    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
	}
}
// =========================================== size functions ==============================================
/*
#define CMPADDITIONALBYTES 4 // 1 (tag) + 3 (avg size)

// возвращает размер структуры cmpFiscalArticle в памяти. Включает постоянную и переменные части.
// если forCMP == TRUE, то к размеру добавляются накладные расходы на представление в виде протокола CMP
card cmpFiscalArticleSize(cmpFiscalArticle *Article, Bool forCMP)
{
	card Result = sizeof(cmpFiscalArticle);
	if (Article->GoodsCode) Result += strlen(Article->GoodsCode) + 1;
	if (Article->GoodsName) Result += strlen(Article->GoodsName) + 1;
	if (Article->MeasureUnit) Result += strlen(Article->MeasureUnit) + 1;

	if (forCMP) // накладные расходы CMP - на каждое поле дополнительно CMPAdditionalBytes байтов
		Result += CMPFISCALARTICLE_FIELDS*CMPADDITIONALBYTES;

	return Result;
}

// возвращает размер структуры cmpFiscalPayments в памяти. Включает постоянную и переменные части.
// если forCMP == TRUE, то к размеру добавляются накладные расходы на представление в виде протокола CMP
card cmpFiscalPaymentsSize(cmpFiscalPayments *Payments, Bool forCMP)
{
	card Result = sizeof(cmpFiscalPayments);
	if (forCMP) // накладные расходы CMP - на каждое поле дополнительно CMPAdditionalBytes байтов
		Result += CMPFISCALPAYMENTS_FIELDS*CMPADDITIONALBYTES;
	return Result;
}

// возвращает размер структуры cmpFiscalReceipt в памяти. Включает постоянную и переменные части.
// если forCMP == TRUE, то к размеру добавляются накладные расходы на представление в виде протокола CMP
card cmpFiscalReceiptSize(cmpFiscalReceipt *Fiscal, Bool forCMP)
{
	card Result = sizeof(cmpFiscalReceipt) + cmpFiscalPaymentsSize(&(Fiscal->Payments), forCMP);

	int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
		Result += cmpFiscalArticleSize(Fiscal->Articles[i], forCMP);

	if (forCMP) // накладные расходы CMP - на каждое поле дополнительно CMPAdditionalBytes байтов
		Result += CMPFISCALRECEIPT_FIELDS*CMPADDITIONALBYTES;

	return Result;
}

// возвращает размер структуры cmpTransactionInfo в памяти. Включает постоянную и переменные части.
// если forCMP == TRUE, то к размеру добавляются накладные расходы на представление в виде протокола CMP
card cmpRSPackageSize(cmpRSPackageInfo *RSPackage, Bool forCMP)
{
	card Result = sizeof(cmpRSPackageInfo) + cmpFiscalReceiptSize(&RSPackage->Fiscal, forCMP);

	if (RSPackage->RetailScreen) Result += strlen(RSPackage->RetailScreen) + 1;
	if (RSPackage->RetailPrinter) Result += strlen(RSPackage->RetailPrinter) + 1;
	if (RSPackage->Track1) Result += strlen(RSPackage->Track1) + 1;
	if (RSPackage->Track2) Result += strlen(RSPackage->Track2) + 1;

	if (forCMP) // накладные расходы CMP - на каждое поле дополнительно CMPAdditionalBytes байтов
		Result += CMPRSPACKAGE_FIELDS*CMPADDITIONALBYTES;
	return Result;
}

// возвращает размер структуры cmpTransactionInfo в памяти. Включает постоянную и переменные части.
// если forCMP == TRUE, то к размеру добавляются накладные расходы на представление в виде протокола CMP
card cmpTransactionSize(cmpTransactionInfo *Tra, Bool forCMP)
{
	card Result = sizeof(cmpTransactionInfo) + cmpRSPackageSize(&Tra->RSPackage, forCMP);

	if (forCMP) // накладные расходы CMP - на каждое поле дополнительно CMPAdditionalBytes байтов
		Result += CMPTRANSACTIONINFO_FIELDS*CMPADDITIONALBYTES;
	return Result;
}*/

// =========================================== write functions ==============================================

static void ocpAppendRSString(tlvWriter *tw, byte Tag, const char *string)
{
	if (string)
		if (strlen(string) > 0)
		{
			char *tmp = NULL;
			strAllocate(&tmp, string);
			strTranslate(tmp, tmp, cpDefault, rsCodePage());
			tlvwAppend(tw, Tag, strlen(tmp), tmp);
			tmp = memFree(tmp);
		}
}

static void ocpWriteArticle(tlvWriter *tw, tFiscalArticleInfo *Article, DoubleValueSelector Selector)
{
	ocpAppendRSString(tw, fatGoodsCode, Article->GoodsCode);

	tlvwAppendCard(tw, fatQuantity, dblGetInt64(&Article->Quantity, Selector));
	tlvwAppendCard(tw, fatPriceWithoutDiscount, dblGetInt64(&Article->PriceWithoutDiscount, Selector));
	tlvwAppendCard(tw, fatAmountWithoutDiscount, dblGetInt64(&Article->AmountWithoutDiscount, Selector));

	ocpAppendRSString(tw, fatGoodsName, Article->GoodsName);

	if (dblGetByte(&Article->Flags, Selector) != 0) tlvwAppendByte(tw, fatFlags, dblGetByte(&Article->Flags, Selector));
	if (dblGetInt64(&Article->DiscountForPrice, Selector) != 0) tlvwAppendCard(tw, fatDiscountForPrice, dblGetInt64(&Article->DiscountForPrice, Selector));
	if (dblGetInt64(&Article->DiscountForAmount, Selector) != 0) tlvwAppendCard(tw, fatDiscountForAmount, dblGetInt64(&Article->DiscountForAmount, Selector));
	if (dblGetInt64(&Article->Bonuses, Selector) != 0) tlvwAppendCard(tw, fatBonuses, dblGetInt64(&Article->Bonuses, Selector));
	if (Article->PaymentType != ptUnknown) tlvwAppendByte(tw, fatPaymentType, Article->PaymentType);

	ocpAppendRSString(tw, fatMeasureUnit, Article->MeasureUnit);
}

static void ocpWritePayments(tlvWriter *tw, tFiscalPaymentsInfo *Payments, DoubleValueSelector Selector)
{
	if (dblGetInt64(&Payments->AmountCash, Selector) != 0) tlvwAppendCard(tw, fptAmountCash, dblGetInt64(&Payments->AmountCash, Selector));
	if (dblGetInt64(&Payments->AmountBankingCard, Selector) != 0) tlvwAppendCard(tw, fptAmountBankingCard, dblGetInt64(&Payments->AmountBankingCard, Selector));
	if (dblGetInt64(&Payments->AmountBonuses, Selector) != 0) tlvwAppendCard(tw, fptAmountBonuses, dblGetInt64(&Payments->AmountBonuses, Selector));
	if (dblGetInt64(&Payments->AmountCredit, Selector) != 0) tlvwAppendCard(tw, fptAmountCredit, dblGetInt64(&Payments->AmountCredit, Selector));
	if (dblGetInt64(&Payments->AmountPrepaidAccount, Selector) != 0) tlvwAppendCard(tw, fptAmountPrepaidAccount, dblGetInt64(&Payments->AmountPrepaidAccount, Selector));
	if (Payments->PaymentTypeForDiscount != ptUnknown) tlvwAppendByte(tw, fptPaymentTypeForDiscount, Payments->PaymentTypeForDiscount);
}

static void ocpWriteFiscal(tlvWriter *tw, tFiscalReceiptInfo *Fiscal, DoubleValueSelector Selector)
{
	int i;
	for (i = 0; i < Fiscal->ArticlesCount; i++)
	{
		tlvWriter arttw; tlvwInit(&arttw);
		ocpWriteArticle(&arttw, Fiscal->Articles[i], Selector);
		tlvwAppend(tw, rftArticle, arttw.DataSize, arttw.Data);
		tlvwFree(&arttw);
	}

	if (dblGetByte(&Fiscal->Flags, Selector) != 0) tlvwAppendByte(tw, rftFlags, dblGetByte(&Fiscal->Flags, Selector));
	if (dblGetInt64(&Fiscal->AmountWithoutDiscount, Selector) != 0) tlvwAppendCard(tw, rftAmountWithoutDiscount, dblGetInt64(&Fiscal->AmountWithoutDiscount, Selector));
	if (dblGetInt64(&Fiscal->DiscountForAmount, Selector) != 0) tlvwAppendCard(tw, rftDiscountForAmount, dblGetInt64(&Fiscal->DiscountForAmount, Selector));

	if (payAmount(&Fiscal->Payments) != 0)
	{
		tlvWriter paytw; tlvwInit(&paytw);
		ocpWritePayments(&paytw, &Fiscal->Payments, Selector);
		tlvwAppend(tw, rftPayments, paytw.DataSize, paytw.Data);
		tlvwFree(&paytw);
	}
}

// Writes all transaction into DataSlot (dslPacketForRS)
Bool ocpWriteRSPackage(byte DataSlot, tRSPackageInfo *RSPackage, DoubleValueSelector Selector, Bool SaveFiscal)
{
	Bool Result = TRUE;
	tTransactionInfo *Transaction = (tTransactionInfo *)RSPackage->Transaction;
	if (!RSPackage || !Transaction) return FALSE;

	// в зависимости от пакета ПЦ определяем список тегов ККМ, которые нужно сериализовать
	// список тегов задаётся в виде указателя на массив байтов, в котором последний элемент: 0
	byte FullTags[] = { rstResponse, rstScreenMessage, rstPrinterMessage, rstAmount, rstFlags, rstFiscalReceipt, rstPurchaseID, rstOriginalID, rstCancellationID, rstCardType, rstCardID, rstAllowedPaymentTypes, rstTrack1, rstTrack2, rstDateTime, rstReceiptNumber, rstECRID, rstShopID, 0 };
	byte ExternalApplicationTags[] = { rstFlags, rstScreenMessage, rstPrinterMessage, rstPurchaseID, rstCardID, rstTrack1, rstTrack2, rstDateTime, rstReceiptNumber, rstECRID, rstShopID, 0 };
	byte RefillAccountTags[] = { rstAmount, rstScreenMessage, rstPrinterMessage, rstPurchaseID, rstTrack1, rstTrack2, rstDateTime, rstReceiptNumber, rstECRID, rstShopID, 0 };
	byte CopyOfReceiptTags[] = { rstPurchaseID, rstScreenMessage, rstPrinterMessage, rstTrack1, rstTrack2, rstDateTime, rstReceiptNumber, rstECRID, rstShopID, 0};
	byte ErrorMessageTags[] = { rstResponse, rstScreenMessage, rstPrinterMessage, rstTrack1, rstTrack2, rstDateTime, rstReceiptNumber, rstECRID, rstShopID, 0 };

    byte *Tags = NULL; // набор тегов для передачи в ККМ
    // может быть давать Full если dvsInitial
	switch (Transaction->PackageID)
	{
		case pcpConnectionFromExternalApplication: Tags = ExternalApplicationTags; break;
		case pcpRefillAccount: Tags = RefillAccountTags; break;
		case pcpCopyOfReceipt: Tags = CopyOfReceiptTags; break;
		case pcpErrorMessage: Tags = ErrorMessageTags; break;
		default: Tags = FullTags;
	}

	tlvWriter tw; tlvwInit(&tw);
	tlvwAppendByte(&tw, rstPackageID, dblGetByte(&RSPackage->PackageID, Selector)); // always add PackageID
	int i;
	for (i = 0;;i++) // сериализуем
	{
		byte Tag = Tags[i];
		if (Tag == 0) break; // выходим. когда наткнёмся на пустой тег
		switch (Tag)
		{
			case rstResponse:
				if (dblGetByte(&RSPackage->Response, Selector) != 0)
					tlvwAppendByte(&tw, rstResponse, dblGetByte(&RSPackage->Response, Selector));
				break;
			case rstScreenMessage:
				ocpAppendRSString(&tw, rstScreenMessage, RSPackage->RetailScreen);
				break;
			case rstPrinterMessage:
				ocpAppendRSString(&tw, rstPrinterMessage, RSPackage->RetailPrinter);
				break;
			case rstAmount:
				if (dblGetInt64(&RSPackage->Amount, Selector) != 0)
					tlvwAppendCard(&tw, rstAmount, dblGetInt64(&RSPackage->Amount, Selector));
				break;
			case rstFlags:
				if (dblGetUInt16(&RSPackage->Flags, Selector) != 0)
					tlvwAppendWord(&tw, rstFlags, dblGetUInt16(&RSPackage->Flags, Selector));
				break;
			case rstFiscalReceipt:
				if (SaveFiscal && RSPackage->Fiscal.ArticlesCount > 0)
				{
					tlvWriter fistw; tlvwInit(&fistw);
					ocpWriteFiscal(&fistw, &RSPackage->Fiscal, Selector);
					tlvwAppend(&tw, rstFiscalReceipt, fistw.DataSize, fistw.Data);
					tlvwFree(&fistw);
				}
				break;
			case rstPurchaseID:
				tlvwAppendBCDString(&tw, rstPurchaseID, RSPackage->PurchaseID);
				break;
			case rstCancellationID:
				tlvwAppendBCDString(&tw, rstCancellationID, RSPackage->CancellationID);
				break;
			case rstOriginalID:
				tlvwAppendBCDString(&tw, rstOriginalID, RSPackage->OriginalID);
				break;
			case rstCardType:
				if (Transaction->CardType != ctUnknown)
					tlvwAppendByte(&tw, rstCardType, Transaction->CardType);
				break;
			case rstCardID:
				tlvwAppendString(&tw, rstCardID, Transaction->CardNumber);
				break;
			case rstAllowedPaymentTypes:
				if (ocsGet() != NULL) // по идее нужно взять список из текущего скрипта обслуживания
					tlvwAppendWord(&tw, rstAllowedPaymentTypes, (word)ocsGet()->GetPaymentTypes());
				break;
			case rstSelectedBehaviourID:
				if (RSPackage->SelectedBehaviourID != 0) tlvwAppendByte(&tw, rstSelectedBehaviourID, RSPackage->SelectedBehaviourID);
				break;
			case rstOperation:
				if (RSPackage->Operation != ipsoUnknown) tlvwAppendByte(&tw, rstOperation, RSPackage->Operation);
				break;
			case rstRRN:
				tlvwAppendString(&tw, rstRRN, RSPackage->RRN);
				break;
			case rstAuthCode:
				tlvwAppendString(&tw, rstAuthCode, RSPackage->AuthCode);
				break;
			case rstTrack1:
				if (Selector == dvsInitial && RSPackage->Track1 && strlen(RSPackage->Track1) > 0) tlvwAppendString(&tw, rstTrack1, RSPackage->Track1);
				break;
			case rstTrack2:
				if (Selector == dvsInitial && RSPackage->Track2 && strlen(RSPackage->Track2) > 0) tlvwAppendString(&tw, rstTrack2, RSPackage->Track2);
				break;
			case rstDateTime:
				if (Selector == dvsInitial && RSPackage->RSTime != 0) tlvwAppendCard(&tw, rstDateTime, RSPackage->RSTime);
				break;
			case rstReceiptNumber:
				if (Selector == dvsInitial && RSPackage->ReceiptNumber != 0) tlvwAppendCard(&tw, rstReceiptNumber, RSPackage->ReceiptNumber);
				break;
			case rstECRID:
				if (Selector == dvsInitial && RSPackage->ECRID != 0) tlvwAppendCard(&tw, rstECRID, RSPackage->ECRID);
				break;
			case rstShopID:
				if (Selector == dvsInitial && RSPackage->ShopID != 0) tlvwAppendCard(&tw, rstShopID, RSPackage->ShopID);
				break;
			default:
				printS("Alert! Unknown tag %02X in ocpWriteRSPackage()\n", Tag);
				break;
		}
	}
	dsAllocate(DataSlot, tw.Data, tw.DataSize);
	tlvwFree(&tw);
	return Result;
}

#endif

void ocpBCDToTransactionID(char *ID, const byte *BCD, int lenBCD)
{
	if (lenBCD != 0 && ID && BCD)
	{
		BCDToString(BCD, lenBCD, ID);
		ocpCompleteTransactionIDDefault(ID);
	}
}

// выделяет из ID транзакции его части
void ocpExtractFromTransactionID(char *ID, char *Source, char *PCOwnerID, char *Unique)
{
    if (strlen(ID) < lenIDFull)
    {
        printS("Alert! Invalid size of transactionID=%s (%d). Should be: %d\n", ID, strlen(ID), lenIDFull);
        return;
    }
    strncpy(Source, ID, lenIDSource);
    strncpy(PCOwnerID, ID+lenIDSource, lenIDPCOwner);
    strncpy(Unique, ID+lenIDSource+lenIDPCOwner, lenIDUnique);
}

// выделяет из ID транзакции ID владельца ПЦ
card ocpExtractPCOwnerIDFromTransactionID(char *ID)
{
        char sSource[lenIDSource+1], sPCOwnerID[lenIDPCOwner+1], sUnique[lenIDUnique];
        ocpExtractFromTransactionID(ID, sSource, sPCOwnerID, sUnique);

        return atoi(sPCOwnerID);
}

// Дополняет переданный ID транзакции до нужной длины
void ocpCompleteTransactionID(char *ID, char Source, card PCOwnerID)
{
	char tmpID[lenID];
	// начинаем дополнять ID различной информацией
	if (strlen(ID) < lenIDUnique) // если наше ID меньше уникальной части ID, то дополняем до неё нулями
		strLeftPad(ID, '0', lenIDUnique);

	if (strlen(ID) < lenIDUnique + lenIDPCOwner) // если длина ID меньше длины без указания на владельца ПЦ
	{   // дополняем PCOwner'ом
		strcpy(tmpID, ID);
		char sPCOwner[lenIDPCOwner + 1];
		sprintf(sPCOwner, "%06u", (unsigned int)PCOwnerID);
		sprintf(ID, "%s%s", sPCOwner, tmpID);
	}

	if (strlen(ID) < lenIDFull)
	{   // дополняем TransactionSource
		strcpy(tmpID, ID);
		sprintf(ID, "%c%s", Source, tmpID); // источник транзакции (offline, online)
	}
}

// Дополняет переданный ID транзакции значениями по умолчанию
void ocpCompleteTransactionIDDefault(char *ID)
{
	ocpCompleteTransactionID(ID, pcInOffline() ? '3' : '1', mapGetCardValue(trmePCOwnerID));
}
