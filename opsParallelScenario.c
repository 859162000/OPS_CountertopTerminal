/*
 * opsParallelScenario.c
 *
 *  Created on: 26.08.2014
 *      Author: Pavel
 */

// ===================================== ПАРАЛЛЕЛЬНЫЕ СЦЕНАРИИ КОММУНИКАТОРА =====================================
#include <stdio.h>
#include <stdlib.h>
#include <sdk30.h>
#include "log.h"

// Create - вызывается каждый раз при запуске opsCommunication. Это подготовка к старту. Должна быть максимально быстрой
// Initialize - вызывается перед стартом основного функционала. Может быть "долгой", до 100ms
// Iterate - одна итерация параллельного сценария. до 50ms
// Terminate - завершение параллельного сценария. до 50ms

// ===================================== Ввод и проверка пин-кода =====================================

static Bool IsEnterPINStarted = FALSE;
static tDisplayState DisplayState;
static char PIN[lenPIN];

void psEnterPINCreate()
{
	IsEnterPINStarted = FALSE;
}

Bool psIsEnterPINStarted()
{
	return IsEnterPINStarted;
}

void psEnterPINInitialize(byte *PCState, byte *RSState)
{
	if (!cphIsPINKeyLoaded(TRUE))
	{
		usrInfo(infPINKeyNotLoaded);
		*PCState = lssQueryForDisconnect;
		return;
	}
	// если текущий пакет - Confirmation, то игнорируем его (может быть это просто второй/третий/десятый раз проверяют PIN-код)
	// если условие убрать, то если проверка ввести неправильный пин, а потом правильный - будет вечная обработка Confirmation
	byte PacketID = mapGetByteValue(traPCPacketID);
	if (PacketID != pcpConfirmation)
		mapPutByte(traPCPacketIDSave, PacketID); // сохраняем какой пакет был инициатором подтверждения транзакции
	// переводим все соединения в режим ожидания
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;
	// устанавливаем режим ввода PIN-кода
	Click();Click();ttestall(0, 10);Click();ttestall(0, 20);Click(); // приглашаем клиента ввести PIN код

	memset(PIN, 0, lenPIN);
	dspGetState(&DisplayState); // save DisplayState
	dspClear();
	displayLS8859(cdlPINQuery, "Введите PIN:");
	displayLS8859(cdlPINHelp, "ЗЕЛ: Далее/КР: Отмена");
	IsEnterPINStarted = TRUE;
}

void psEnterPINTerminate(byte *PCState, byte *RSState)
{
	dspSetState(&DisplayState); // restore DisplayState
	IsEnterPINStarted = FALSE;
}

void psEnterPINIterate(char key, byte *PCState, byte *RSState)
{
	byte PINLength = strlen(PIN);

	if (key >= '0' && key <= '9') // добавляем цифру к ПИН-коду
	{
		if (PINLength < lenPIN-1) PIN[PINLength] = key; else Beep();
	}
	else if (key == kbdANN)
		psEnterPINTerminate(PCState, RSState);
	else if (key == kbdVAL)
	{
		if (PINLength > 0 && PINLength < lenPIN)
		{
			psEnterPINTerminate(PCState, RSState);
			// нужно преобразовать PIN в PINBlock и записать его в транзакцию
			char CardNumber[lenCardNumber];
			mapGet(traCardNumber, CardNumber, lenCardNumber);
			byte PINBlock[lenPINBlock]; memclr(PINBlock, sizeof(PINBlock));

			cphGetPinBlock(CardNumber, PIN, PINBlock);
			mapPut(traPINBlock, PINBlock, lenPINBlock);
			// перевести PCState в отправку QueryConfirmation
			*PCState = lssQueryConfirmation;
		}
		else
			Beep();
	}
	else if (key == kbdCOR) // удаляем последний символ
	{
		if (PINLength > 0) PIN[PINLength-1] = 0x00; else Beep();
	}

	if (IsEnterPINStarted)
	{
		char PINForDisplay[sizeof(PIN) + 1]; // +1 for cursor
		memset(PINForDisplay, '*', PINLength);
		PINForDisplay[PINLength] = '_'; // show cursor at end of string (or not)
		PINForDisplay[PINLength + 1] = 0x00;
		displayLS(cdlPINEntry, PINForDisplay);
	}
}

// ===================================== Чтение вставленной карты =====================================

static Bool IsReadCardStarted = FALSE;
static byte ReadCardMode = 0; // 0 - read card, 1 - enter card number
static char magtrk1[ISO_TRACK_SIZE], magtrk2[ISO_TRACK_SIZE], magtrk3[ISO_TRACK_SIZE];
static char CardNumber[lenCardNumber];

void psReadCardCreate()
{
	IsReadCardStarted = FALSE;
}

Bool psIsReadCardStarted()
{
	return IsReadCardStarted;
}

static void psReadCardShowInvitation()
{
	dspClear();
	switch (ReadCardMode)
	{
		case 0:
			displayLS8859(cdlReadCard1, " Вставьте");
			displayLS8859(cdlReadCard2, "      карту");
	    	if (crmCanEnterManually())
	    		displayLS8859(cdlFToEnterCardNumber, "F: Ввод номера карты");
			break;
		case 1:
			displayLS8859(cdlCardNumberPrompt, "Номер карты:");
	    	/*if (crmCanEnterManually()) временно запрещено, т.к. обратный ввод номера карты не предусмотрен при вызове opsEnterCardNumber
	    		displayLS8859(cdlFToReadCard, "F: Чтение карты");*/
			break;
		default:
			displayLS(0, "ReadCardMode error\n%d", ReadCardMode);
			break;
	}
}

void psReadCardInitialize(byte *PCState, byte *RSState)
{
	mapPutByte(traRSPacketIDSave, mapGetByteValue(traRSPacketID)); // сохраняем какой пакет был прислан перед запуском чтения карты
	// переводим все соединения в режим ожидания
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;

	// открываем магнитный ридер (чиповый будем открывать при каждой итерации)
	if (magStart())
	{

		// обнуляем магнитные треки
		memset(magtrk1, 0, sizeof(magtrk1));
		memset(magtrk2, 0, sizeof(magtrk2));
		memset(magtrk3, 0, sizeof(magtrk3));

		// обнуляем буфер для ввода
		memset(CardNumber, 0, sizeof(CardNumber));

		traResetCardNumber(); // очищаем номер карты в транзкции

		// устанавливаем режим вставки карты
		ReadCardMode = 0;
		dspGetState(&DisplayState); // save DisplayState
		psReadCardShowInvitation();
		Beep();

		IsReadCardStarted = TRUE;
	}
	else
		usrInfo(infMagnetReaderOpenError);
}

void psReadCardTerminate(byte *PCState, byte *RSState)
{
	*RSState = rssRehandlePacket;
    magStop(); // close all used readers
	dspSetState(&DisplayState); // restore DisplayState
	IsReadCardStarted = FALSE;
}

void psReadCardIterate(char key, byte *PCState, byte *RSState)
{
	char rsp[ICC_RESPONSE_SIZE];
	memset(rsp, 0, sizeof(rsp));
	Bool CanTerminate = key == kbdANN; // остальное для kbdANN контролируется в opsCommunication
	byte ChipReader = mapGetByteValue(cfgChipReader);
	int ret, cardtype;

	switch (ReadCardMode)
	{
		case 0:
		{
			Bool tmp = traCardNumberIsEmpty();
			if (tmp)
			{
				ret = magGet(magtrk1, magtrk2, magtrk3);
				if (ret > 0) // magnet card swiped
				{
					usrInfo(infReadingOfMagnetCard);
					cardtype = ReadMagnetCardNumber(CardNumber, magtrk1, magtrk2, magtrk3);
					if (cardtype != phctUnknown)
					{
						SetCardNumber(CardNumber, crmMagnet);
		                ocdReadCardData(cardtype); // чтение аварийных данных карты
					}
					else
					{
						usrInfo(infMagnetCardIsNotSupported);
		            	CanTerminate = TRUE;
						*PCState = lssReadCard;
					}
				}

				if (iccStart(ChipReader) > 0)
				{
				    ret = iccCommand(ChipReader, (byte *) 0, (byte *) 0, rsp);
					iccStop(ChipReader);
					if (ret > 0)
					{
					    int rsplen = ret;
						usrInfo(infReadingOfChipCard);
						cardtype = GetCardType(rsp, rsplen);
				        if (cardtype != phctUnknown)
				        {
				            ret = ReadChipCardNumber(cardtype, ChipReader, CardNumber);
				            if (ret == TRUE)
				            {
				                SetCardNumber(CardNumber, crmChip);
				                ocdReadCardData(cardtype); // чтение аварийных данных карты
				            }
				            else
				            {
				            	usrInfo(ret == -1000 ? infInvalidCardType : infCardReadError); // Неверный тип карты ИЛИ Ошибка чтения номера карты
				            	CanTerminate = TRUE;
				    			*PCState = lssReadCard;
				            }
				        }
				        else
				        {   // передача на сервер информации о неизвестном типе карты
				        	usrInfo(infChipCardIsNotSupported); // неизвестный тип карты
							dsAllocate(dslUnknownATR, rsp, rsplen); // выделяем память под ATR
			            	CanTerminate = TRUE;
							*PCState = lssStartSendATR;
				        }
					}
				}
			}
			else
			{
			/*
			    Убрано - вызывается в конце opsCommunication, чтобы не тратить время пользователя + запись на карту должна производится после обслуживания
				if (iccStart(ChipReader) > 0) // просим вынуть карту
				{
					displayLS8859(cdlReadCard1, "  Выньте");
					displayLS8859(cdlReadCard2, "      карту");
					Beep();

					CanTerminate = iccCommand(ChipReader, (byte *) 0, (byte *) 0, (byte *) 0) == -iccCardRemoved;
					iccStop(ChipReader);
				}
			*/
				CanTerminate = TRUE;
			}

			if (crmCanEnterManually() && key == kbdMNU)
			{
				ReadCardMode = 1;
				psReadCardShowInvitation();
			}

			break;
		}
		case 1:
		{
			int CardNumberLength = strlen(CardNumber), MaxCardNumberLength = lenCardNumber - 1;

			if (key >= '0' && key <= '9') // добавляем цифру к ПИН-коду
			{
				if (CardNumberLength < MaxCardNumberLength) CardNumber[CardNumberLength] = key; else Beep();
			}
			else if (key == kbdCOR) // удаляем последний символ
			{
				if (CardNumberLength > 0) CardNumber[CardNumberLength-1] = 0x00; else Beep();
			}
			else if (key == kbdANN)
			{
				CanTerminate = TRUE; // остальное контролируется в opsCommunication
			}
			else if (key == kbdVAL)
			{
				if (strlen(CardNumber) > 0)
				{
					SetCardNumber(CardNumber, crmGetMethodForManualEntry());
					CanTerminate = TRUE;
				}
				else
				{
					Beep(); Beep();
				}
			}

			char CardNumberDisplay[dspW*2 + 1];
			strcpy(CardNumberDisplay, CardNumber);
			strcat(CardNumberDisplay, "_");
			displayLS(cdlCardNumberEntry, CardNumberDisplay);

			/*if (crmCanEnterManually() && key == kbdMNU) // переключение режима ввода
			{ // убрано, т.к. в opsEnterCardNumber нет обратного переключателя
				ReadCardMode = 0;
				psReadCardShowInvitation();
			}*/

			break;
		}
	}

	if (CanTerminate)
		psReadCardTerminate(PCState, RSState);
}

// ===================================== Ввод ответного кода голосовой авторизации =====================================

static Bool IsEnterVACodeStarted = FALSE;
static char VACode[lenVoiceAuthCode];

void psEnterVACodeCreate()
{
	IsEnterVACodeStarted = FALSE;
}

Bool psIsEnterVACodeStarted()
{
	return IsEnterVACodeStarted;
}

void psEnterVACodeInitialize(byte *PCState, byte *RSState)
{
	char sDummy[dspW + 1];
	// сохраняем предыдущие пакеты от ПЦ и ККМ
	mapCopyByte(traRSPacketID, traRSPacketIDSave);
	mapCopyByte(traPCPacketID, traPCPacketIDSave);
	// переводим все соединения в режим ожидания
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;
	// устанавливаем режим ввода кода ГА
	Click();Click();ttestall(0, 10);Click();ttestall(0, 20);Click(); // приглашаем кассира ввести код ГА

	memset(VACode, 0, sizeof(VACode));
	dspGetState(&DisplayState); // save DisplayState

	dspClear();
	displayLS8859(cdlVACodeTitle, "ГОЛОСОВАЯ АВТОРИЗАЦИЯ");
	displayLS8859(cdlVACodeFooter, "ЗЕЛ: Далее/КР: Отмена");
	displayLS8859(cdlVACodeInfoOnReceipt, "Инструкция на чеке.");
	mapGet(traVoiceAuthAnswerCode, VACode, lenVoiceAuthCode); // инициализируем код ГА тем, что уже есть в транзакции (для корректировки ввода)
	sprintf(sDummy, "Ответный код (%d):", (int)strlen(VACode)); // сколько осталось ввести
	displayLS8859(cdlVACodePrompt, sDummy);

	IsEnterVACodeStarted = TRUE;
}

void psEnterVACodeTerminate(byte *PCState, byte *RSState)
{
	dspSetState(&DisplayState); // restore DisplayState
	IsEnterVACodeStarted = FALSE;
}

void psEnterVACodeIterate(char key, byte *PCState, byte *RSState)
{
	char sDummy[dspW + 1];
	int VACodeLength = strlen(VACode), MaxVACodeLength = lenVoiceAuthCode - 1;

	if (key >= '0' && key <= '9') // добавляем цифру к ПИН-коду
	{
		if (VACodeLength < MaxVACodeLength) VACode[VACodeLength] = key; else Beep();
	}
	else if (key == kbdANN)
	{
		psEnterVACodeTerminate(PCState, RSState);
		*PCState = lssDisconnecting; // отказ от обслуживания
		*RSState = rssDisconnecting;
	}
	else if (key == kbdVAL)
	{
		if (strlen(VACode) >= 28) // 28 символов - минимальная длина ответного кода
		{
			psEnterVACodeTerminate(PCState, RSState);
			*RSState = rssRehandlePacket; // повторная обработка пакета от ККМ
			mapPut(traVoiceAuthAnswerCode, VACode, lenVoiceAuthCode); // сохраняем ответный код ГА в транзакции
		}
		else
		{
			Beep(); Beep();
		}
	}
	else if (key == kbdCOR) // удаляем последний символ
	{
		if (VACodeLength > 0) VACode[VACodeLength-1] = 0x00; else Beep();
	}

	if (key != 0)
	{
		sprintf(sDummy, "Ответный код (%d):", (int)strlen(VACode)); // сколько осталось ввести
		displayLS8859(cdlVACodePrompt, sDummy);
	}

	if (IsEnterVACodeStarted)
	{
		/*
		123456789012345678901

		000000-000000-000000-
		000000-000000
		*/

		char VACodeDisplay[dspW*3 + 1]; memset(VACodeDisplay, 0, sizeof(VACodeDisplay));
		vaFormatCode(VACodeDisplay, VACode, 6, '-');
		/*int i, di = 0;
		for (i = 0; i < VACodeLength; i++)
		{
			VACodeDisplay[di] = VACode[i];
			di++;
			if ((i+1) % 6 == 0 && i != VACodeLength-1)
			{
				VACodeDisplay[di] = '-';
				di++;
			}
		}*/
		VACodeDisplay[strlen(VACodeDisplay)] = '_'; // show cursor
		displayLS(cdlVACodeEntry, VACodeDisplay); // первую часть выводим всегда
		if (strlen(VACodeDisplay) > dspW) // если много букв, то
		{
			strcpy(sDummy, VACodeDisplay + dspW);
			displayLS(cdlVACodeEntry + 1, sDummy); // выводим оставшуюся часть кода
		}
		else
			displayLS(cdlVACodeEntry + 1, " ");
	}
}



// =============================== общие функции ========================================

Bool psIsStarted()
{
	return IsEnterPINStarted || IsReadCardStarted || IsEnterVACodeStarted;
}
