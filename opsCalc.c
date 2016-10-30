/*
 * opsCalc.c
 *
 *  Created on: 20.12.2011
 *      Author: Pavel
 */
#include "string.h"
#include "stdio.h"
#include "log.h"

// функции численных вычислений
#ifdef OFFLINE_ALLOWED

#endif

// Отсекаем часть
int64 clcTruncValue(int64 Value, int Divider)
{
	return Value / Divider;
}

// Округляем результат операции путем деления значения на нужный делитель
int64 clcNormalizeValue(int64 Value, int Divider)
{
	return Value >= 0 ? (Value + (Divider / 2)) / Divider : (Value - (Divider / 2)) / Divider;
}

// Округляем результат умножения цены на другую цену или скидку
int64 clcNormalizePrice(int64 Value)
{
	return clcNormalizeValue(Value, 10000);
}

// Округляем результат умножения цены на другую цену или скидку
int64 clcNormalizePriceQuantity(int64 Value)
{
	return clcNormalizeValue(Value, 1000);
}

// Преобразовать Value в зависимости от текущих настроек округления: (оставить без изменений, отсечь копейки, округлить копейки по математическим правилам)
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

// Функция деления стоимости на цену ДЛЯ ПОЛУЧЕНИЯ ЦЕНЫ
int64 clcDivideAmountPriceForPrice(int64 Amount, int64 Price)
{
	byte Precision = 2; // округляем всегда до двух знаков после запятой
	if (Price <= 0) return -2;
	int64 Result = Amount * 100000 / Price; // умножаем на 100000, а не на 1000 для двукратного запаса для округления, которое производим потом
	card Divider = stdPow(10, 2 - Precision);
	return clcNormalizeValue(Result, 1000 * Divider) * Divider;
}

// Функция деления стоимости на цену ДЛЯ ПОЛУЧЕНИЯ КОЛИЧЕСТВА с округлением до указанного количества разрядов
int64 clcDivideAmountPriceForQuantity(int64 Amount, int64 Price, byte Precision)
{
	if (Precision > 3) return -1;
	if (Price <= 0) return -2;
	int64 Result = Amount * 100000 / Price; // умножаем на 100000, а не на 1000 для двукратного запаса для округления, которое производим потом
	card Divider = stdPow(10, 3 - Precision);
	return clcNormalizeValue(Result, 100 * Divider) * Divider;
}

// Функция деления стоимости на количество ДЛЯ ПОЛУЧЕНИЯ ЦЕНЫ
int64 clcDivideAmountQuantityForPrice(int64 Amount, int64 Quantity)
{
	if (Quantity == 0)
	{
		PrintRefusal8859("Количество не может быть ноль для данной операции");
		return 0;
	}
	else
	{
		int64 Result = (Amount * 10 * 100000) / Quantity; // умножаем на 100000 для двукратного запаса для округления, которое производим потом
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
	return value * stdPow(10, DecPos); // тута херня написана. Нужен round и обработка типа округления (арифметическое или классическое банковское)
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
    switch (PurseType) // в зависимости от типа кошелька преобразуем значение либо в валюту карты, либо в количество
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

// Возвращает текстовую метку продолжительности текущего лимита
// Размер строки - 2 байта
void clcGetLimitDurationSign(byte Duration, char *DurationSign)
{
	switch (Duration)
	{
		case cldDaily: strcpy(DurationSign, "С"); break;
		case cldWeek: strcpy(DurationSign, "Н"); break;
		case cldMonth: strcpy(DurationSign, "М"); break;
		case cldQuarter: strcpy(DurationSign, "К"); break;
		case cldYear: strcpy(DurationSign, "Г"); break;
		case cldNonRenewable: strcpy(DurationSign, "Е"); break;
		default: strcpy(DurationSign, "?"); break;
	}
}

// Возвращает текстовую продолжительность текущего лимита
// Размер строки - 15 байт
void clcGetLimitDurationString(byte Duration, char *DurationString)
{
	switch (Duration)
	{
		case cldDaily: strcpy(DurationString, "Суточный"); break;
		case cldWeek: strcpy(DurationString, "Недельный"); break;
		case cldMonth: strcpy(DurationString, "Месячный"); break;
		case cldQuarter: strcpy(DurationString, "Квартальный"); break;
		case cldYear: strcpy(DurationString, "Годовой"); break;
		case cldNonRenewable: strcpy(DurationString, "Единовременный"); break;
		default: strcpy(DurationString, "?"); break;
	}
}

