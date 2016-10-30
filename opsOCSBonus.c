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

	strAdd(&oftGet()->TerminalPrinter, "������ ���������� ������\n");
    strAdd(&oftGet()->TerminalPrinter, "��� ���������� �������:\n");
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
	displayLS8859(INV(CNTR(0)), "����� ���ר��");
	tRadioGroup BonusCalcModes[] =
	{
		{ TRUE, "\xBF\xDE\xD7\xD6\xD5\x20\xDD\xD0\x20\xE1\xD5\xE0\xD2\xD5\xE0\xD5" }, // ����� �� �������
		{ FALSE, sNow }, // ������ (x%)
		{ 0, 0 }
	};
	int rgResult = rgSelectEx(1, TRUE, BonusCalcModes);
	if (rgResult >= 0)
		DoNotChargeBonuses = (Bool)rgResult;
	else
		ocsErrorAdd("������������ ���������\n�� ���������� ����������");

	dspSetState(&ds);

	return ocsResult();
}

Bool ocsBonusChangeReceipt()
{
	tFiscalReceiptInfo *Fiscal = offisGet();
	if (Fiscal->Payments.AmountBonuses.Current > 0)
		ocsErrorAdd("������ �������� ��\n�������������� �\n��������� ������\n");
	if (DoNotChargeBonuses) // ���� �� ��������� ������, ��
	{
		strAdd(&oftGet()->TerminalPrinter, "������ ������� � ������\n����� ������� �� �����\n��������� �������.\n");
	}
	else
	{
		if (ocsErrorIsEmpty())
		{
			int64 DiscountPercent = ocsGetCardDiscount(); // ���������� ������ ��� ������� �������
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
						artCalculateBonuses(Article, Discount); // ��������� ����� ��������� �������
					}
					else
						ocsErrorAdd("��� ������\n�� ��������������\n� ��������� ������");
				}
				else
					ocsErrorAdd("���������� ����������\n��� ������ ��������\n�������.");
			}
		}

		// ��������� ���� ��, ������� ������� ��� ������ �������������, ����� �� ���� ��������� �� ��
		offisGet()->Flags.Current |= frfWithoutRecalculation;
	}


	return ocsResult();
}

Bool ocsBonusPurchase()
{
	tFiscalReceiptInfo *Fiscal = offisGet();
	tFiscalPaymentsInfo *Payments = &Fiscal->Payments;

	if (Payments->AmountCash.Current != 0) ocsTerminalPrinterAddJustifiedAmount("����� (���.):", Payments->AmountCash.Current);
	if (Payments->AmountBankingCard.Current != 0) ocsTerminalPrinterAddJustifiedAmount("����� (����.):", Payments->AmountBankingCard.Current);
	if (Payments->AmountBonuses.Current != 0) ocsTerminalPrinterAddJustifiedAmount("����� (���.):", Payments->AmountBonuses.Current);
	if (Payments->AmountCredit.Current != 0) ocsTerminalPrinterAddJustifiedAmount("����� (����.):", Payments->AmountCredit.Current);
	if (Payments->AmountPrepaidAccount.Current != 0) ocsTerminalPrinterAddJustifiedAmount("����� (����.):", Payments->AmountPrepaidAccount.Current);
	ocsTerminalPrinterAddJustifiedAmount("�����:", payAmountRounded(Payments));

	strAdd(&oftGet()->TerminalPrinter, "------------------------\n");
	strAdd(&oftGet()->TerminalPrinter, "������ �� �������:\n");
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


