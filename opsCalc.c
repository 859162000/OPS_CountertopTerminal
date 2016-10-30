/*
 * opsCalc.c
 *
 *  Created on: 20.12.2011
 *      Author: Pavel
 */
#include "string.h"
#include "stdio.h"
#include "log.h"

// ������� ��������� ����������
#ifdef OFFLINE_ALLOWED

#endif

// �������� �����
int64 clcTruncValue(int64 Value, int Divider)
{
	return Value / Divider;
}

// ��������� ��������� �������� ����� ������� �������� �� ������ ��������
int64 clcNormalizeValue(int64 Value, int Divider)
{
	return Value >= 0 ? (Value + (Divider / 2)) / Divider : (Value - (Divider / 2)) / Divider;
}

// ��������� ��������� ��������� ���� �� ������ ���� ��� ������
int64 clcNormalizePrice(int64 Value)
{
	return clcNormalizeValue(Value, 10000);
}

// ��������� ��������� ��������� ���� �� ������ ���� ��� ������
int64 clcNormalizePriceQuantity(int64 Value)
{
	return clcNormalizeValue(Value, 1000);
}

// ������������� Value � ����������� �� ������� �������� ����������: (�������� ��� ���������, ������ �������, ��������� ������� �� �������������� ��������)
int64 clcRoundAmount(int64 Value)
{
	byte Mode = mapGetByteValue(trmeRoundingMode), Digits = mapGetByteValue(trmeRoundingDigits);

    int Divider = stdPow(10, Digits);
    switch (Mode)
    {
      case rmTrunc: return clcTruncValue(Value, Divider) * Divider;
      case rmRound: return clcNormalizeValue(Value, Divider) * Divider;
      default: return Value;
    }
}

int64 clcRoundValue(int64 Value, byte RoundingDigits)
{
	int Divider = stdPow(10, RoundingDigits);
    return clcNormalizeValue(Value, Divider) * Divider;
}

int64 clcRoundQuantity(int64 Value, byte QuantityPrecision)
{
    return clcRoundValue(Value, 3 - QuantityPrecision);
}

// ������� ������� ��������� �� ���� ��� ��������� ����
int64 clcDivideAmountPriceForPrice(int64 Amount, int64 Price)
{
	byte Precision = 2; // ��������� ������ �� ���� ������ ����� �������
	if (Price <= 0) return -2;
	int64 Result = Amount * 100000 / Price; // �������� �� 100000, � �� �� 1000 ��� ����������� ������ ��� ����������, ������� ���������� �����
	card Divider = stdPow(10, 2 - Precision);
	return clcNormalizeValue(Result, 1000 * Divider) * Divider;
}

// ������� ������� ��������� �� ���� ��� ��������� ���������� � ����������� �� ���������� ���������� ��������
int64 clcDivideAmountPriceForQuantity(int64 Amount, int64 Price, byte Precision)
{
	if (Precision > 3) return -1;
	if (Price <= 0) return -2;
	int64 Result = Amount * 100000 / Price; // �������� �� 100000, � �� �� 1000 ��� ����������� ������ ��� ����������, ������� ���������� �����
	card Divider = stdPow(10, 3 - Precision);
	return clcNormalizeValue(Result, 100 * Divider) * Divider;
}

// ������� ������� ��������� �� ���������� ��� ��������� ����
int64 clcDivideAmountQuantityForPrice(int64 Amount, int64 Quantity)
{
	if (Quantity == 0)
	{
		PrintRefusal8859("���������� �� ����� ���� ���� ��� ������ ��������");
		return 0;
	}
	else
	{
		int64 Result = (Amount * 10 * 100000) / Quantity; // �������� �� 100000 ��� ����������� ������ ��� ����������, ������� ���������� �����
		return clcNormalizeValue(Result, 1000);
	}
}

double clcValueToNatural(int64 Value, byte DecPos)
{
	return (double)Value / (double)stdPow(10, DecPos);
}

double clcValueToNaturalQuantity(int64 Quantity) // QuantityToDouble
{
	return clcValueToNatural(Quantity, 3);
}

double clcValueToNaturalAmount(int64 Amount) // AmountToDouble
{
	return clcValueToNatural(Amount, 2);
}

void clcValueToString(int64 Value, byte Digits, char *Result)
{
	char fmt[10];
	int Divider = stdPow(10, Digits);
	switch (Digits)
	{
		case 0: strcpy(fmt, "%d"); break;
		case 1: strcpy(fmt, "%d.%d"); break;
		case 2: strcpy(fmt, "%d.%02d"); break;
		case 3: strcpy(fmt, "%d.%03d"); break;
	}

	if (Digits > 0)
		sprintf(Result, fmt, (int)(Value / Divider), (int)(Value % Divider));
	else
		sprintf(Result, fmt, (int)(Value / Divider));
}

void clcValueToStringAmount(int64 Value, char *Result)
{
	clcValueToString(Value, 2, Result);
}

void clcValueToStringQuantity(int64 Value, char *Result)
{
	clcValueToString(Value, 3, Result);
}

int64 clcNaturalToValue(double value, byte DecPos)
{
	return value * stdPow(10, DecPos); // ���� ����� ��������. ����� round � ��������� ���� ���������� (�������������� ��� ������������ ����������)
	// return Convert.ToInt64(Math.Round(value * Convert.ToDecimal(Math.Pow(10, DecPos)), MiddlePointRounding));
}

int64 clcNaturalToValueAmount(double value)
{
	return clcNaturalToValue(value, 2);
}

int64 clcNaturalToValueQuantity(double value)
{
	return clcNaturalToValue(value, 3);
}

byte clcGetPurseDecimalPosition(byte PurseType)
{
    switch (PurseType) // � ����������� �� ���� �������� ����������� �������� ���� � ������ �����, ���� � ����������
    {
      case ptyProduct:
      case ptyProductGroup:
    	  return 3;
      case ptyCurrency:
    	  return 2;
      default:
    	  printS("Alert! Unknown PurseType in clcGetPurseDecimalPosition()\n");
    	  return 0;
    }
}

double clcLimitToNatural(byte LimitPurseType, int64 Limit)
{
	return clcValueToNatural(Limit, clcGetPurseDecimalPosition(LimitPurseType));
}

// ���������� ��������� ����� ����������������� �������� ������
// ������ ������ - 2 �����
void clcGetLimitDurationSign(byte Duration, char *DurationSign)
{
	switch (Duration)
	{
		case cldDaily: strcpy(DurationSign, "�"); break;
		case cldWeek: strcpy(DurationSign, "�"); break;
		case cldMonth: strcpy(DurationSign, "�"); break;
		case cldQuarter: strcpy(DurationSign, "�"); break;
		case cldYear: strcpy(DurationSign, "�"); break;
		case cldNonRenewable: strcpy(DurationSign, "�"); break;
		default: strcpy(DurationSign, "?"); break;
	}
}

// ���������� ��������� ����������������� �������� ������
// ������ ������ - 15 ����
void clcGetLimitDurationString(byte Duration, char *DurationString)
{
	switch (Duration)
	{
		case cldDaily: strcpy(DurationString, "��������"); break;
		case cldWeek: strcpy(DurationString, "���������"); break;
		case cldMonth: strcpy(DurationString, "��������"); break;
		case cldQuarter: strcpy(DurationString, "�����������"); break;
		case cldYear: strcpy(DurationString, "�������"); break;
		case cldNonRenewable: strcpy(DurationString, "��������������"); break;
		default: strcpy(DurationString, "?"); break;
	}
}

