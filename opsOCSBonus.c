/*
 * opsOCSBonus.c
 *
 *  Created on: 06.03.2014
 *      Author: Pavel
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

#ifdef OFFLINE_ALLOWED

Bool ocsBonusInitializeCard()
{
	return ocsResult();
}

Bool ocsBonusCardInformation()
{
	char sCurrent[prtW*2+1], sDummy[prtW*2+1];

	strAdd(&oftGet()->TerminalPrinter, "Скидка аварийного режима\n");
    strAdd(&oftGet()->TerminalPrinter, "для начисления бонусов:\n");
    clcValueToStringAmount(ocsGetCardDiscount(), sDummy);
    strcat(sDummy, " %");
    strFormatJustifySS(sCurrent, prtW, "", sDummy);
    strAddLine(&oftGet()->TerminalPrinter, sCurrent);

	if (ocdHasLimits())
		PrintOfflineLimits();

	strAdd(&ofrspGet()->RetailScreen, oftGet()->TerminalPrinter);
	return ocsResult();
}

Bool DoNotChargeBonuses = TRUE;

Bool ocsBonusDialog()
{
	char sNow[dspW+1], sPercent[dspW+1];
	clcValueToStringAmount(ocsGetCardDiscount(), sPercent);
	sprintf(sNow, "\xC1\xD5\xD9\xE7\xD0\xE1\x20(%s%%)", sPercent);

	tDisplayState ds;
	dspGetState(&ds);

	dspClear();
	displayLS8859(INV(CNTR(0)), "МЕТОД РАСЧЁТА");
	tRadioGroup BonusCalcModes[] =
	{
		{ TRUE, "\xBF\xDE\xD7\xD6\xD5\x20\xDD\xD0\x20\xE1\xD5\xE0\xD2\xD5\xE0\xD5" }, // Позже на сервере
		{ FALSE, sNow }, // Сейчас (x%)
		{ 0, 0 }
	};
	int rgResult = rgSelectEx(1, TRUE, BonusCalcModes);
	if (rgResult >= 0)
		DoNotChargeBonuses = (Bool)rgResult;
	else
		ocsErrorAdd("Пользователь отказался\nот завершения транзакции");

	dspSetState(&ds);

	return ocsResult();
}

Bool ocsBonusChangeReceipt()
{
	tFiscalReceiptInfo *Fiscal = offisGet();
	if (Fiscal->Payments.AmountBonuses.Current > 0)
		ocsErrorAdd("Оплата бонусами не\nподдерживается в\nаварийном режиме\n");
	if (DoNotChargeBonuses) // если не начисляем бонусы, то
	{
		strAdd(&oftGet()->TerminalPrinter, "Расчёт бонусов и скидок\nбудет проведён до конца\nотчётного периода.\n");
	}
	else
	{
		if (ocsErrorIsEmpty())
		{
			int64 DiscountPercent = ocsGetCardDiscount(); // используем скидку для расчёта бонусов
			int i;
			for (i = 0; i < Fiscal->ArticlesCount; i++)
			{
				tFiscalArticleInfo *Article = Fiscal->Articles[i];
				if (artChangePaymentType(Article, Fiscal))
				{
					ocpPaymentTypes PaymentType = Article->PaymentType;
					if (PaymentType == ptCash || PaymentType == ptBankingCard)
					{
						int64 Discount = clcNormalizePrice(Article->PriceWithoutDiscount.Current * DiscountPercent);
						artCalculateBonuses(Article, Discount); // вычисляем сумму аварийных бонусов
					}
					else
						ocsErrorAdd("Тип оплаты\nне поддерживается\nв аварийном режиме");
				}
				else
					ocsErrorAdd("Невозможно определить\nтип оплаты товарной\nпозиции.");
			}
		}

		// добавляем флаг ФЧ, который говорит что расчёт окончательный, чтобы не было пересчёта на ПЦ
		offisGet()->Flags.Current |= frfWithoutRecalculation;
	}


	return ocsResult();
}

Bool ocsBonusPurchase()
{
	tFiscalReceiptInfo *Fiscal = offisGet();
	tFiscalPaymentsInfo *Payments = &Fiscal->Payments;

	if (Payments->AmountCash.Current != 0) ocsTerminalPrinterAddJustifiedAmount("Сумма (нал.):", Payments->AmountCash.Current);
	if (Payments->AmountBankingCard.Current != 0) ocsTerminalPrinterAddJustifiedAmount("Сумма (банк.):", Payments->AmountBankingCard.Current);
	if (Payments->AmountBonuses.Current != 0) ocsTerminalPrinterAddJustifiedAmount("Сумма (бнс.):", Payments->AmountBonuses.Current);
	if (Payments->AmountCredit.Current != 0) ocsTerminalPrinterAddJustifiedAmount("Сумма (кред.):", Payments->AmountCredit.Current);
	if (Payments->AmountPrepaidAccount.Current != 0) ocsTerminalPrinterAddJustifiedAmount("Сумма (счёт.):", Payments->AmountPrepaidAccount.Current);
	ocsTerminalPrinterAddJustifiedAmount("Итого:", payAmountRounded(Payments));

	strAdd(&oftGet()->TerminalPrinter, "------------------------\n");
	strAdd(&oftGet()->TerminalPrinter, "Бонусы за покупку:\n");
	ocsTerminalPrinterAddJustifiedAmount("", fisBonusesRounded(Fiscal));

	strAdd(&ofrspGet()->RetailScreen, oftGet()->TerminalPrinter);
	return ocsResult();
}

Bool ocsBonusRefund()
{
	return ocsResult();
}

/*Bool ocsBonusWriteCardData()
{
	return ocsResult();
}*/

ocpPaymentTypeFlags ocsBonusGetPaymentTypes()
{
	return ptfByCash | ptfByBankingCard | ptfByBonuses;
}

#endif


