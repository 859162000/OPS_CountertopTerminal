/*
 *
 *  Created on: 22.03.2011
 *      Author: Pavel
 *  Онлайн обработка тройного взаимодействия ПЦ-терминал-ККМ. При необходимости вызывается offline-обработчик
 */
#include <stdio.h>
#include <stdlib.h>
#include <sdk30.h>
#include <string.h>
#include <LinkLayer.h>
#include "log.h"

// ========================== УВЕДОМЛЕНИЯ О СОЕДИНЕНИЯХ =================================================

// ConnectionStatuses
typedef enum
{
	csNotConnected,
	csConnected,
	csOffline
} ConnectionStatuses;


static void dspConnectToPC(byte ServerNumber, ConnectionStatuses Status)
{
    displayLS8859(cdlPC, "  с ПЦ #%u...%s", ServerNumber, Status == csConnected ? "OK" : (Status == csOffline ? "оффлайн" : ""));
}

static void dspConnectToRS(ConnectionStatuses Status)
{
    displayLS8859(cdlRS, "  c ККМ...%s", Status == csConnected ? "OK" : (Status == csOffline ? "оффлайн" : ""));
}

// ========================== СОЕДИНЕНИЕ С СЕРВЕРОМ ЛОЯЛЬНОСТИ =================================================

Bool opsConnectToPC(Bool CanSwitchToOfflineMode)
{
	Bool CanSwitchToOnlineMode = TRUE;
	byte i, ServerNumber = mapGetByteValue(cfgComPCSelected);
	int ret = 0; // 0 - результат не определен, но соединения нет. 1 - успешно. -1 - ошибка

	//if (!pcInOffline())
	//{
		if (!pcCheckConnection()) // если мы не соединены с ПЦ, то
		{
			byte attempt, AttemptsCount = mapGetByteValue(cfgComConnectionAttmpts);

			for (attempt = 0; attempt < AttemptsCount; attempt++) // пытаемся подключится к каждому серверу AttemptsCount раз
			{
				for (i = 0; i < PC_SERVERS_COUNT; i++) // опрашиваем все сервера
				{
					if (ServerNumber <= 0) ServerNumber = 1; // check restrictions
				    if (ServerNumber > PC_SERVERS_COUNT) ServerNumber = PC_SERVERS_COUNT;

				    dspConnectToPC(ServerNumber, csNotConnected);
					// connect to PC server (physic)
				    int ChannelError = LL_ERROR_OK;
					if (pcOpenChannel(ServerNumber, &ChannelError) >= 0)
					{
					    //printS("ConnectTo %d\n", get_tick_counter());
						if (pcConnectToServer()) // connect to server (logical and get session key)
						{
						    dspConnectToPC(ServerNumber, csConnected);
						    mapPutByte(cfgComPCSelected, ServerNumber); // выбираем отозвавшийся сервер
						    if (CanSwitchToOnlineMode)
						    	mapPutByte(regPCCurrentMode, pcmOnline); // если соединились - переключаем режим ПЦ на online
						    //mapPutByte(traPCConnected, 1);
						    //printS("Finish! %d\n", get_tick_counter());
						    ret = 1; // good connection
						    break;
						}
						/*else - выводит чек отказа после попытки соединиться с сервером
						{
							char cs[lenIPAddress+6+1]; // address+port+nt
							if (pcGetConfigString(ServerNumber, cs))
								PrintRefusal("\xC1\xD5\xE0\xD2\xD5\xE0\n%s\n\xDD\xD5\x20\xDE\xE2\xD2\xD5\xE7\xD0\xD5\xE2\n", cs); // сервер \nip|port\n не отвечает
						}*/
					}
					ServerNumber = (ServerNumber % PC_SERVERS_COUNT) + 1;
					pcCloseChannel();

					if (ChannelError != LL_ERROR_OK) // если серъёзная ошибка канала, то отображаем информацию и выходим
					{
						DisplayLLError(cdlDisplayError, ChannelError, "Ошибка канала", 10);
						ret = -1;
						break;
					}
				}
				if (ret != 0) break; // если результат уже известен - выходим
			}
		}
		else
		{
			// мы соединены с ПЦ
		    dspConnectToPC(mapGetByteValue(cfgComPCSelected), csConnected);
		    ret = 1; // good connection
		}
	//}

	// если ret <= 0, то мы или не смогли соединиться с ПЦ или он ответил отказом
	if (ret <= 0 && CanSwitchToOfflineMode) // если нам разрешено переводить терминал в режим offline, то переводим его в offline
	{
		mapPutByte(regPCCurrentMode, pcmOffline); // переводим терминал в режим Offline
		dspConnectToPC(mapGetByteValue(cfgComPCSelected), csOffline);
	}

	return ret > 0;
}

// ========================== СОЕДИНЕНИЕ С СИСТЕМОЙ УПРАВЛЕНИЯ =================================================

Bool opsConnectToRS()
{
	dspConnectToRS(csNotConnected);
    int ret = rsOpenChannel();
	if (ret >= 0)
	{
		ret = rsConnectToServer();
		if (ret == TRUE)
		{
			dspConnectToRS(csConnected);
			return TRUE;
		}
		else
		{
			displayLS8859(dspLastRow, "Код ошибки: %d                     ", ret);
			usrInfo(infRSDidNotAnswer);
		}
	}
	else
	{
		displayLS8859(dspLastRow, "Код ошибки: %d                         ", ret);
		usrInfo(infRSConnectionError);
	}
	return FALSE;
}

Bool comIsConnected()
{
	int ret;
#ifdef __TELIUM__
	ret = comGetDialInfo();
	if (ret != 0x02000000 && ret != 0x03000000)
		return FALSE;
#else
#endif
	return TRUE;
}

// пока не используется #define CHECK_REFUNDS_ONLY { if (IsOnlyRefundAllowed()) { RSState = rssProtocolError; PCState = lssProtocolError; break; } };

// возвращает TRUE, если переданное стартовое состояние может быть реализовано только в Online
static Bool comnIsOnlineOnly(byte PCState)
{
	return PCState == lssStartUpdateSoftware || PCState == lssStartUpdateConfiguration || PCState == lssStartSendATR
			|| PCState == lssStartValidation || PCState == lssStartAcceptQuest || PCState == lssStartSendOnlineState
			|| PCState == lssStartOpenShift || PCState == lssStartLoadAutonomousGoods || PCState == lssStartQueryDateTime
			|| PCState == lssStartSendArchiveTransactions || PCState == lssStartGetCopyOfReceipt
			|| PCState == lssStartQueryOfflineConfiguration || PCState == lssStartRefillAccount
			|| PCState == lssStartQueryKeySet || PCState == lssStartQueryLogotype
			|| PCState == lssStartGetCardActions || PCState == lssStartEnableCardAction
			|| PCState == lssStartRemindPIN;
}

// возвращает TRUE, если для переданных стартовых состояний нужно скрывать ошибки подключений
static Bool comnHideConnectionErrors(byte PCState)
{
	return PCState == lssStartSendATR || PCState == lssStartSendOnlineState;
}

/*
// возвращает TRUE, если для переданных стартовых состояний допустима работа в режиме голосовой авторизации
static Bool comnIsAllowVoiceAuth(byte PCState)
{
	return PCState == lssStartAutonomousTransaction || PCState == lssStartCardMaintenance || PCState == lssStartCancellationWithoutRS
			|| PCState == lssStartConnectionFromPetrol;
}*/

/*
// возвращает TRUE, если можно подключаться к ПЦ
static Bool comnAllowConnectToPC(Bool OnlineOnly, opsOfflinePossibilities OfflinePossibility, Bool IsAllowVoiceAuth)
{
	if (OnlineOnly) return TRUE;
	else
	{
		switch (OfflinePossibility)
		{
			case ofpOnlineOnly: case ofpVoiceAuthorization:
				return TRUE; // no offline allowed
			case ofpOnlineAndOffline:
				#ifdef OFFLINE_ALLOWED
					return comnQueryTryOnline(); // offline allowed
				#else
					return TRUE;
				#endif

			default:
				printS("Alert! Invalid offline possibility: %d\n", OfflinePossibility);
				break;

			case voice auth
				if (IsAllowVoiceAuth && pcInOfflineMode())
				{
					Result = comnQueryTryOnline();
					if (!Result)
					{
						// генерируем код голосовой авторизации
						char VoiceCode[lenVoiceCode];
						GenerateVoiceCode(VoiceCode);

						dspClear();
						displayLS(0, "\xBF\xDE\xD7\xD2\xDE\xDD\xD8\xE2\xD5\x20\xDF\xDE\x20\xE2\xD5\xDB\x2E\x3A"); // Позвоните по тел.:
						displayLS(1, "\xD3\xDE\xE0\xEF\xE7\xD5\xD9\x20\xDB\xD8\xDD\xD8\xD8\x20\x20\x20\x20\x20"); // горячей линии
						displayLS(2, "\xD8\x20\xDF\xE0\xDE\xD4\xD8\xDA\xE2\xE3\xD9\xE2\xD5\x20\xDA\xDE\xD4\x20"); // и продиктуйте код
						displayLS(3, VoiceCode);
						displayLS(INV(dspLastRow), "\xB7\xB5\xBB\x2D\xD4\xD0\xDB\xD5\xD5\x20\x20\xBA\xC0\x2D\xE1\xE2\xDE\xDF"); // ЗЕЛ-далее  КР-стоп
						kbdWait(0);
						dspClear();
					}
				}
				break;
		}
	}
	return TRUE; // если не обработали ситуацию - подключаемся к ПЦ
}*/

/*Bool comnIsImmediatlyConnectWithPC(byte PCState)
{
	return PCState != lssNotConnected && PCState != lssWaitDataFromRS;
}*/

#define PCBREAK { if (PCState == lssDisconnecting || PCState == lssQueryForDisconnect || PCState == lssErrorReceived || PCState == lssProtocolError || PCState == lssTimeout /*|| PCState == lssNotConnected*/ || PCState == lssIncorrectPIN) break; }
#define RSBREAK { if (RSState == rssDisconnecting || RSState == rssErrorReceived || RSState == rssProtocolError || RSState == rssTimeout) break; }

//#define SHOW_STATUS

// ========================= ОСНОВНАЯ ФУНКЦИЯ ДВОЙНОГО ВЗАИМОДЕЙСТВИЯ С TIMEOUT-ЗАЩИТОЙ В ОБОИХ НАПРАВЛЕНИЯХ =================================
// По сути это - конечный автомат
// Соедиение с PC сервером и, при необходимости, соедиение с ККМ, если не соединились в opsProcessEvent()
void opsCommunication(char * Header, byte InitialPCState, byte InitialRSState)
{
	#ifdef SHOW_STATUS
		byte StatusPCState = 0, StatusRSState = 0;
		unsigned long int ticks1 = get_tick_counter(), ticks2;
	#endif

	int ret = 0;
	tBuffer buf;
    card DataSize = 0, PCTimeout = mapGetWordValue(cfgComPCTimeout)*100, RSTimeout = mapGetWordValue(cfgComRSTimeout)*100, BELFreq = mapGetWordValue(cfgComBELFreq)*100;
	mapPutCard(traRSTimeout, RSTimeout);
    byte *Data = NULL, Answer, ControlCharacter, PCState = lssQueryForEstabilishConnection, RSState = InitialRSState;
    Bool AllowShowConnectionErrors = !comnHideConnectionErrors(InitialPCState);
    Bool OnlineOnly = comnIsOnlineOnly(InitialPCState);
    Bool CanSwitchPCToOffline = AllowShowConnectionErrors && oflGetPossibility() != ofpOnline; // Можем переводить ПЦ в offline только когда показываем ошибки и у нас разрешено использование offline-режимов
    tDisplayState DisplayState;
    Bool IsPCConnectionDone = FALSE, IsRSConnectonDone = RSState != rssNotConnected; // если TRUE, то процесс соединения с ПЦ/ККМ завершен (даже если ПЦ в offline)
    Bool RSHaveUnhandledPacket = FALSE;

    if (AllowShowConnectionErrors && !PrintPaperControl()) { usrInfo(infNoPaper); return; }

    opsOfflinePossibilities OfflinePossibility = oflGetPossibility(); // проверка доступен ли режим offline или ГА
	#ifndef OFFLINE_ALLOWED
    	OfflinePossibility = ofpOnline; // если аварийный режим запрещён - всегда режим всегда ONLINE, независимо от установок и текущего пакета
    	OnlineOnly = TRUE;
	#endif

	dspClear();
    displayLS(cdlHeader, Header);

	// убрано 05/09/2014 - флаг устанавливается при обработке offline if (pcInOffline()) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsOfflineTransaction); // устанавливаем флаг "offline-транзакция"
	if (pcShouldPermanentlyConnected()) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsFirstPackage); // если нужно постоянно держать связь с ПЦ, то устанавливаем флаг "первый пакет в транзакции"

	if (pcIsConnected()) dspConnectToPC(mapGetByteValue(cfgComPCSelected), csConnected);
	if (rsIsConnected()) dspConnectToRS(csConnected);

	displayLS8859(cdlAbort, "КРАСНАЯ: Отмена");

	// основной цикл двойного сетевого взаимодействия
	card cntr = 0;
	byte cntrinc = 1;
	psEnterPINCreate();
	psReadCardCreate();
	psEnterVACodeCreate();

	#ifdef __TELIUM__
		// выводим информацию о свободной памяти
		//printS("mem1: %u\n", FreeSpace());
	#endif

	#ifdef SHOW_STATUS
		ticks2 = get_tick_counter();
		printS("Before start: %d\nPC: %d  RS: %d\n", ticks2-ticks1, PCState, RSState);
		StatusPCState = PCState;
		StatusRSState = RSState;
	#endif
	kbdStart(1);

	while (1)
    {
		#ifdef SHOW_STATUS
			ticks1 = get_tick_counter();
		#endif

		char key = kbdKey();
    	if (key == kbdANN) // если в цикле обслуживания нажали КР - выходим
    	{
    		usrInfo(infCancelledByUser);
    		break;
    	}

    	if (!psIsStarted())
    	{
    		displayLS8859(cdlQueryCounter, "Запрос: %u", cntr);
    		displayLS8859(cdlQueryCounter+1, " "); // clear next string

    		if (ocdCanStore()) // если на карте есть offline-информация, то выводим уведомление о том, что карту вынимать нельзя, иначе могут быть проблемы с записью обновлеённой инфы
    			displayLS8859(cdlCardWarning, "КАРТУ НЕ ВЫНИМАТЬ");
    	}
    	else
    	{
    		if (psIsEnterPINStarted())
    			psEnterPINIterate(key, &PCState, &RSState);
    		else if (psIsReadCardStarted())
    			psReadCardIterate(key, &PCState, &RSState);
    		else if (psIsEnterVACodeStarted())
    			psEnterVACodeIterate(key, &PCState, &RSState);
    		cntrinc = 0;
    	}
		cntr += cntrinc; cntrinc = 1;


    	PCBREAK; // обработка ошибок PC (выход из цикла приёма/передачи)

    	// ======================================================== статус соединения с Processing center ======================
		comSwitchToSlot(cslotPC);

    	if (PCState != lssNotConnected && PCState != lssQueryForEstabilishConnection && !pcInOffline()) // check the connection status
    		PCState = comIsConnected() ? PCState : lssDisconnecting;

    	switch (PCState)
    	{
    		case lssQueryForEstabilishConnection: // попытка установить соединение с ПЦ
    			// анализируем состояние ККМ, чтобы не мешать принимать данные от него
    			if (RSState == rssPrepareWaitForData || RSState == rssWaitForData)
    				break; // выходим и возвращаемся к соединению чуть позже...

    			// всегда пытаемся соединиться с ПЦ
				PCState = opsConnectToPC(CanSwitchPCToOffline) ? InitialPCState : lssNotConnected;

				if (PCState == lssNotConnected) // Не смогли подключится к серверу
				{
					mapPutByte(traResponse, 5); // Устанавливаем код ошибки
					if (AllowShowConnectionErrors) // если можем взаимодействовать с юзером, то
					{
						dspGetState(&DisplayState);
						#ifdef OFFLINE_ALLOWED
						if (OfflinePossibility != ofpOnline && pcInOffline() && !OnlineOnly) // если не-онлайн режимы доступны, то спрашиваем можно ли продолжать
						{
							Beep();
							tDisplayState ds;
							dspGetState(&ds);
							dspClear();
							displayLS8859(1, "Сервер ПЦ недоступен.");
							displayLS8859(2, "Продолжить работу");
							switch (OfflinePossibility)
							{
								case ofpOffline:
									displayLS8859(3, "в аварийном режиме?");
									break;
								case ofpVoiceAuthorization:
									displayLS8859(3, "в режиме голосовой");
									displayLS8859(4, "авторизации?");
									break;
								default:
									displayLS8859(3, "в неизвестном режиме?");
									break;
							}
							displayLS8859(INV(dspLastRow), "ЗЕЛ: ДА       КР: Нет");
							int key = kbdWaitInfo8859(10, CNTR(INV(0)), "Уведомление (%d с.)"); // Уведомление (10 с.)
							if (key == kbdANN)
								goto lblEnd; // если Offline отменен - выходим
							dspSetState(&ds);
						}
						else
						#endif
							usrInfo(infNoPCConnection); // можно только в online - выдаём ошибку
						dspSetState(&DisplayState);
					}
				}

    			if (PCState == lssNotConnected && (OfflinePossibility == ofpOnline || OnlineOnly)) // если мы не соединились с сервером и можем работать только в online - выходим из цикла обработки
    				goto lblEnd;

				#ifdef OFFLINE_ALLOWED
				if (pcInOffline() && !ocHasConfig()) // если терминал в режиме offline, но нет аварийной конфигурации - выводим сообщение и выходим
				{
					usrInfo(infOfflineConfigIsNotLoaded);
					goto lblEnd;
				}
				#endif

				#ifdef OFFLINE_ALLOWED
				if (pcInOffline()) // если терминал в offline-режиме, то устанавливаем начальное состояние автомат и указываем на то, что работаем в режиме offline
				{
					PCState = InitialPCState;
					dspConnectToPC(mapGetByteValue(cfgComPCSelected), csOffline);
				}
				#endif

				if (PCState == lssNotConnected) // Отлов багов: если после всех проверок ПЦ остался в состоянии NotConnected - дисконнектим его
					PCState = lssDisconnecting;

				IsPCConnectionDone = TRUE; // процесс соединения завершен, даже если мы в режиме offline
    			break;
			case lssStartCardMaintenance: // отправляем первый пакет на получение информации о карте от ПЦ
				pcSendQueryCardInfo(FALSE, FALSE, &PCState, &RSState); break;
			case lssStartGetCardInfo: // отправляем запрос на получение информации о карте от ПЦ
				pcSendQueryCardInfo(TRUE, FALSE, &PCState, &RSState); break;
	  	    case lssStartConnectionFromPetrol: // отправляем первый пакет для начала обслуживания без карты
				pcSendQueryConnectionFromExternalApp(&PCState, &RSState); break;
	  	    case lssStartCancellationWithoutRS:
				pcSendQueryCancellationWithoutRS(&PCState, &RSState); break;
			case lssStartValidation: // отправляем запрос на получение отчёта за смену
				pcSendQueryStartValidation(&PCState, &RSState); break;
			case lssStartOpenShift: // отправляем запрос на регистрацию смены на терминале
				pcSendQueryStartTerminalAuth(&PCState, &RSState); break;
			case lssStartSendATR: // отправляем на сервер информацию о неизвестном ATR
				pcSendUnknownATR(&PCState, &RSState); break;
			case lssStartSendOnlineState:// отправляем на сервер информацию о терминале онлайн
				pcSendPeriodic(&PCState, &RSState); break;
			case lssStartAcceptQuest: // отправляем на сервер запрос на приём анкеты
				pcSendQueryAcceptQuest(&PCState, &RSState); break;
			case lssStartGetCardActions: // запрос на получение списка акций с которым может подключиться (от которых может отключиться) карта
				pcSendQueryCardActionsList(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartEnableCardAction: // запрос на активацию или деактивацию акции
				pcSendQueryEnableAction(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartUpdateSoftware: // отправляем запрос на получение списка файлов для обновления
				pcSendQueryFileList("SWAP", &PCState, &RSState); break;
			case lssStartUpdateConfiguration: // отправляем запрос на получение конфигурации
				pcSendQueryFileList("HOST", &PCState, &RSState); break;
			case lssStartAutonomousTransaction: // отправляем на сервер автономную транзакцию
				pcSendAutonomousTransaction(&PCState, &RSState); break;
			case lssStartLoadAutonomousGoods: // отправляем на сервер запрос на загрузку товаров для работы терминала в автономном режиме
				pcSendQueryAutonomousGoods(&PCState, &RSState); break;
			case lssStartQueryKeySet: // отправляем на сервер запрос на загрузку параметров шифрования
				pcSendQueryKeySet(&PCState, &RSState); break;
			case lssEnterPIN: // параллельный сценарий ввода PIN-кода
				psEnterPINInitialize(&PCState, &RSState); break;
			case lssEnterVoiceAuthAnswerCode: // параллельный сценарий ввода ответного кода ГА
				psEnterVACodeInitialize(&PCState, &RSState); break;
			case lssQueryConfirmation: // отправить на сервер запрос подтверждения транзакции
				pcSendQueryConfirmation(&PCState, &RSState); break;
			case lssReadCard: // параллельный сценарий чтения карты
				psReadCardInitialize(&PCState, &RSState); break;
			case lssStartQueryLogotype: // отправляем запрос на получение логотипа
				pcSendQueryLogotype(&PCState, &RSState); break;
			case lssStartQueryDateTime: // отправляем запрос на синхронизацию даты и времени
				pcSendQueryDateTime(&PCState, &RSState); break;
			case lssStartGetCopyOfReceipt: // отправляем запрос на получение копии чека
				pcSendQueryCopyOfReceipt(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartRemindPIN: // send query for remind card PIN code
				pcSendQueryRemindPIN(&PCState, &RSState); break;
		#ifdef OFFLINE_ALLOWED
			case lssStartSendArchiveTransactions:
				pcSendArchiveTransactions(&PCState, &RSState); break; // пытаемся отправить пакет с архивными транзакциями
			case lssStartQueryOfflineConfiguration:
				ocSendQuery(&PCState, &RSState); break; // отправляем запрос загрузки аварийной конфигурации
		#endif
			case lssStartRefillAccount:
				pcSendQueryRefillAccount(TRUE, FALSE, &PCState, &RSState); break;
			case lssQueryForDisconnect:
				PCState = lssDisconnecting;
				if (!pcShouldPermanentlyConnected())
					pcDisconnectFromServer();
				break;
			case lssPrepareWaitForData:
				PCState = lssWaitForData;
				tmrRestart(tsPCTimeout, PCTimeout); // заново начинаем отчёт таймаута
	    		break;
	    	case lssWaitForData:
	    		if (!pcInOffline())
	    		{   // for online mode
	    			Data = opsReceiveMessage(cslotPC, &DataSize, &ControlCharacter); // Receive message and ALLOCATE MEMORY
	    		}
	    		else
	    		{   // for offline mode - retreive packet from data slot
	    			DataSize = dsGetDataSize(dslReceivedData);
	    			Data = memAllocate(DataSize); // нельзя использовать датаслот напрямую, потому что Data очищается в конце
	    			memcpy(Data, dsGetData(dslReceivedData), DataSize);
	    			dsFree(dslReceivedData);
	    			ControlCharacter = pccSTX;
	    		}
	    		switch (ControlCharacter)
	    		{
					case pccSTX:
						if (Data)
						{
							tlvBufferInit(&buf, Data, DataSize);
							card len = 0;
							byte PacketID = 0;
							if (tlvGetValue(&buf, 0x01, &len, &PacketID)) // tag 01 ALWAYS contains 1 byte
							{
								mapPutByte(traPCPacketID, PacketID); // сохраняем ID текущего пакета
								PCState = lssHoldConnection; // начинаем удержание соединения
								pcHandlePacket(PacketID, &buf, &PCState, &RSState); // вызываем обработчик пакетов ПЦ
							}
							else
								PrintRefusal("Package from PC does not contain required tag 0x01\n");
						}
						else
						{
							PCState = lssProtocolError;
							usrInfo(infEmptyPackageFromPC); // От ПЦ получен пустой пакет
						}
						break;
					case pccBEL: // hold the connection
						comSwitchToSlot(cslotPC);
						comSend(pccBEL);
						PCState = lssPrepareWaitForData;
						break;
					case pccEOT: // disconnect from server
						PCState = lssDisconnecting;
						pcDisconnectFromServer(); // сюда пришел реальный запрос на дисконнект - нельзя игнорировать
						usrInfo(infDisconnectFromPC); // Обмен завершен\nпо запросу ПЦ
						break;
					default: // no data - check timeout
			    		if (tmrGet(tsPCTimeout) <= 0) // check server timeout
			    		{
			    			tmrStop(tsPCTimeout);
			    			PCState = lssTimeout;
			    			usrInfo(infPCTimeout); // ПЦ: таймаут
			    		}
			    		else
			    			cntrinc = 0; // не наращиваем количство запросов
						break;
	    		}
				// FREE MEMORY OF MESSAGE
	    		Data = memFree(Data);
	    		break;
			case lssHoldConnection:
				if (IsPCConnectionDone)
				{
					if (tmrGet(tsPCBELTimeout) <= 0) // check BEL timeout
					{
						PCState = lssSendBEL;
						tmrRestart(tsPCBELTimeout, BELFreq); // BEL sends to PC server with freq 5 second
					}
					else
						cntrinc = 0;
				}
				else {
					if (InitialPCState == lssPrepareWaitForData)
					{
						PCState = lssQueryForEstabilishConnection;
						InitialPCState = lssHoldConnection;
					}
				}
				break;
	    	case lssSendBEL:
	    		PCState = lssReceiveBEL;
	    		if (!pcInOffline())
	    		{
					comSwitchToSlot(cslotPC);
	    			comSend(pccBEL);
	    		}
	    		tmrRestart(tsPCTimeout, PCTimeout);
	    		break;
	    	case lssReceiveBEL:
	    		if (!pcInOffline())
	    		{
					comSwitchToSlot(cslotPC);
	    			ret = comRecv(&Answer, 0);
	    		}
	    		else // в режиме offline эмулируем приём BEL от "виртуального" ПЦ.
	    		{
	    			ret = 1;
	    			Answer = pccBEL;
	    		}
	    		if (ret > 0) // check receiving BEL
	    		{
	    			if (Answer == pccBEL) PCState = lssHoldConnection;
	    			else if (Answer == pccEOT) PCState = lssDisconnecting;
	    		}
	    		else
	    			PCState = lssHoldConnection;
	    		if (tmrGet(tsPCTimeout) <= 0) // check server timeout
	    		{
	    			tmrStop(tsPCTimeout);
	    			PCState = lssTimeout;
	    		}
	    		break;
	    	/*case lssWaitDataFromRS:
				RSState = rssPrepareForWaitData;
				//PCState = lssHoldConnection;  //тут нужно изменить статус на установление соединения с ПЦ, после чего, имея на руках пакет с данными работать дальше
				//PCState = lssQueryForEstabilishConnection; // убрано, т.к. никто не знает в каком состоянии находится соединение с ПЦ
				break;*/
			case lssNotConnected:
				break;
			default:
				break;
    	}

    	PCBREAK; // обработка ошибок PC (выход из цикла приёма/передачи)

    	// ======================================== статус соединения с Retail System ==================================


    	RSBREAK; // обработка ошибок RS (выход из цикла приёма/передачи)

    	comSwitchToSlot(cslotRS);

    	if (RSState != rssNotConnected && RSState != rssQueryForEstabilishConnection && RSState != rssRehandlePacket) // check the connection status, but no effect for COM
    		RSState = comIsConnected() ? RSState : rssDisconnecting;

    	switch (RSState)
    	{
			case rssQueryForEstabilishConnection:
			    // estabilish connection with retail system
				if (!comIsConnected())
					RSState = opsConnectToRS() ? rssConnected : rssProtocolError;
				else
					RSState = rssConnected;
				IsRSConnectonDone = TRUE;
			    break;
			case rssQueryForDisconnect:
				RSState = rssDisconnecting;
				rsDisconnectFromServer();
				break;
			case rssConnected: // send first query to RS
				// send data from the slot
				RSState = rsSendPackage() ? rssPrepareWaitForData : rssProtocolError;
				break;
			case rssRehandlePacket: // повторная обработка ранее принятого пакета
				tlvBufferInit(&buf, dsGetData(dslRSPackage), dsGetDataSize(dslRSPackage)); // инициализируем буфер пакетом от ККМ, сохранённым в дата слоте
				rsHandlePacket(mapGetByteValue(traRSPacketIDSave), &buf, &PCState, &RSState);
				if (RSState == rssRehandlePacket) // если состояние соединения не изменено, то или переходим в режим ожидания или "выключаем" ККМ
					RSState = rsIsConnected() ? rssHoldConnection : rssNotConnected;
				break;
			case rssPrepareWaitForData:
				RSState = rssWaitForData;
				tmrRestart(tsRSTimeout, mapGetCardValue(traRSTimeout));
	    		break;
			case rssWaitForData: // ожидаем данные с системы управления
				mapPutCard(traRSTimeout, RSTimeout);
			    Data = opsReceiveMessage(cslotRS, &DataSize, &ControlCharacter); // RECEIVE MESSAGE AND ALLOCATE MEMORY
	    		switch (ControlCharacter)
	    		{
					case pccSTX:
						if (Data)
						{
							tlvBufferInit(&buf, Data, DataSize);
							card len;
							byte PacketID;
							if (tlvGetValue(&buf, 0x01, &len, &PacketID)) // tag 01 ALWAYS contains 1 byte
							{
								mapPutByte(traRSPacketID, PacketID); // сохраняем ID пакета
								dsAllocate(dslRSPackage, Data, DataSize); // сохраняем сам пакет
								RSState = rssHoldConnection; // удерживаем соединение. Пакет будет обработан в этом обработчике
								RSHaveUnhandledPacket = TRUE; // взводим флаг необходимости обработки принятого пакета
							}
							else
								PrintRefusal("Package from RS does not contain required tag 0x01\n");
						}
						else
						{
							RSState = rssProtocolError;
							usrInfo(infEmptyPackageFromRS); // От СУАЗС получен пустой пакет ------------------------
						}
						break;
					case pccBEL: // hold the connection
						comSwitchToSlot(cslotRS);
						comSend(pccBEL);
						RSState = rssPrepareWaitForData;
						break;
					case pccEOT: // disconnect from server
						RSState = rssDisconnecting;
						rsDisconnectFromServer();
						usrInfo(infDisconnectFromRS); // Обмен завершен\nпо запросу СУ ККМ
						break;
					default: // no data - error or timeout
			    		if (tmrGet(tsRSTimeout) <= 0) // check RS timeout
			    		{
			    			tmrStop(tsRSTimeout);
			    			RSState = rssTimeout;
			    			usrInfo(infRSTimeout); // ККМ: таймаут
			    		}
			    		else
			    			cntrinc = 0; // не наращиваем количство запросов
						break;
	    		}
				// FREE MEMORY OF MESSAGE
	    		Data = memFree(Data);
	    		break;
	    	case rssHoldConnection:
				if (IsPCConnectionDone && RSHaveUnhandledPacket) // если обработка пакета завершена
				{
					tlvBufferInit(&buf, dsGetData(dslRSPackage), dsGetDataSize(dslRSPackage));
					rsHandlePacket(mapGetByteValue(traRSPacketID), &buf, &PCState, &RSState); // вызываем обработчик пакетов ККМ
					RSHaveUnhandledPacket = FALSE; // всё, пакет обработан
				}
				else if (tmrGet(tsRSBELTimeout) <= 0) // in another case - check BEL timeout. rsHandlePacket can to change RSState and without "else if" RSState can be changed to rssSendBEL
				{
					RSState = rssSendBEL;
					tmrRestart(tsRSBELTimeout, BELFreq); // BEL sends to RS with freq 5 second
				}
				else
					cntrinc = 0;
				break;
			case rssSendBEL:
	    		RSState = rssReceiveBEL;
				comSwitchToSlot(cslotRS);
				ret = comSend(pccBEL);
	    		if (ret <= 0)
	    			RSState = rssProtocolError;
	    		tmrRestart(tsRSTimeout, mapGetCardValue(traRSTimeout));
	    		break;
			case rssReceiveBEL:
				comSwitchToSlot(cslotRS);
	    		ret = comRecv(&Answer, 0);
	    		if (ret > 0) // check receiving BEL
	    		{
	    			if (Answer == pccBEL) RSState = rssHoldConnection;
	    			else if (Answer == pccEOT) RSState = rssDisconnecting;
	    		}
	    		else
					RSState = rssHoldConnection;
	    		if (tmrGet(tsRSTimeout) <= 0) // check server timeout
	    		{
	    			tmrStop(tsRSTimeout);
	    			RSState = rssTimeout;
	    		}
	    		break;
			case rssNotConnected:
				break;
			default:
				break;
    	}

    	RSBREAK; // обработка ошибок RS (выход из цикла приёма/передачи)

		#ifdef SHOW_STATUS
			ticks2 = get_tick_counter();
			if (PCState != StatusPCState || RSState != StatusRSState)
			{
				printS("PC: %d  RS: %d  PCC: %d\n", /*Ticks: %d\nticks2-ticks1,*/ PCState, RSState, IsPCConnectionDone);
				StatusPCState = PCState;
				StatusRSState = RSState;
			}
		#endif
    }

	lblEnd:
	kbdStop();

#ifdef SHOW_STATUS
	printS("PC: %d  RS: %d  PCC: %d\n", PCState, RSState, IsPCConnectionDone);
#endif

	//dspClear();
	displayLS8859(INV(cdlHeader), "Отключение...");
	displayLS8859(cdlPC, "  от ПЦ...");
	displayLS8859(cdlRS, "  от ККМ...");
    // завершаем все соединения
	rsDisconnectFromServer();
	displayLS8859(cdlRS, "  от ККМ...OK");
	if ((!pcShouldPermanentlyConnected()) || ((pcShouldPermanentlyConnected() && pcIsConnected()) && (PCState == lssNotConnected || PCState == lssTimeout)))
		pcDisconnectFromServer(); // завершаем соединение, если подключение не постоянное
	displayLS8859(cdlPC, "  от ПЦ...OK");
	Data = memFree(Data); // если в Data что-то есть, то освобождаем её память

	#ifdef OFFLINE_ALLOWED
		if (pcInOffline())
		{
			ocsFree(); // если мы в режиме offline, то очищаем память аварийного скрипта
			oftFree(); // 27/02/2015 ...и транзакции. Добавлено для надежности, т.к. в режиме ГА скрипт не создаётся, а транзакция активно используется
		}
	#endif

	// если обновление начато и мы пришлю сюда - значит оно завершилось некорректно, т.к. обновление
	// завершается перезагрузкой в функции обработки последней части файла
    if (mapGetByteValue(traIsUpdateStarted) != 0) // если обновление завершено некорректно, чистим память и выводим об этом информацию
    {
    	updDeinitialize();
    	usrInfo(infUpdateError);
    }

    opsShowTransactionInformation();

    opsWaitForEjectCardAndCloseReader(); // Если у нас вставлена карта, то просим её вынуть

#ifdef __TELIUM__
	// выводим информацию о свободной памяти
	//printS("mem2: %u\n", FreeSpace());
#endif

}
