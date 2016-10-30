/*
 *
 *  Created on: 25.01.2012
 *      Author: Pavel
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <LinkLayer.h>
#include <loaderapi.h>
#include <oem_public.h>
#include <oem_sysfioctl.h>
#include "log.h"

// ***************************************** OPS processing center **************************************** //

// TRUE if ServerNumber if configured correctly
static inline Bool pcGetDialString(byte ServerNumber, char *Config)
{
	char ip[lenIPAddress];
	word port = 0;
	word keyIP, keyPort;
	switch (ServerNumber)
	{
		case 2:
			keyIP = cfgComPC1ServerIP;
			keyPort = cfgComPC1ServerPort;
			break;
		case 3:
			keyIP = cfgComPC2ServerIP;
			keyPort = cfgComPC2ServerPort;
			break;
		case 4:
			keyIP = cfgComPC3ServerIP;
			keyPort = cfgComPC3ServerPort;
			break;
		case 5:
			keyIP = cfgComPC4ServerIP;
			keyPort = cfgComPC4ServerPort;
			break;
		case 6:
			keyIP = cfgComPC5ServerIP;
			keyPort = cfgComPC5ServerPort;
			break;
			// first server in other cases
		default:
			keyIP = cfgComPC0ServerIP;
			keyPort = cfgComPC0ServerPort;
			break;
	}
	mapGet(keyIP, ip, lenIPAddress);
	mapGetWord(keyPort, port);
	if (strlen(ip) != 0 && port > 16)
	{
		sprintf(Config, "%s|%d", ip, port);
		return TRUE;
	}
	else
		return FALSE;
}

/**
 @brief Возвращает коммуникационный канал, используемый для подключения к серверу
 @return one of eChn enumeration
 */
static inline byte pcGetChannel()
{
	switch (mapGetByteValue(cfgComPCConnectionType))
	{
		case pctInternalGPRS:
			return chnGprs;
		case pctExternalGPRS:
			switch (mapGetByteValue(cfgComPCExtGPRSModemType))
			{
			default:
				return chnExtGPRSCom;
			}
			break;
		case pctEthernet:
		default:
			return chnTcp;
	}
}

/**
 @brief Возвращает настройки коммуникационного канала, использующиеся для настройки сервера
 @return TRUE, if channel config was successfully formed
 */
static inline Bool pcGetChannelConfig(byte Channel, char *Config)
{
	switch (Channel)
	{
		case chnTcp:
			strcpy(Config, "");
			return TRUE;
		case chnGprs:
		{
			char sPIN[lenPIN], sAPN[lenGPRSAPN], sLogin[lenPPPLogin],
					sPassword[lenPPPPassword];
			mapGet(cfgComPCIntGPRSPIN, sPIN, lenPIN);
			mapGet(cfgComPCIntGPRSAPN, sAPN, lenGPRSAPN);
			mapGet(cfgComPCGPRSPPPLogin, sLogin, lenPPPLogin);
			mapGet(cfgComPCGPRSPPPPassword, sPassword, lenPPPPassword);
			sprintf(Config, "%s|%s|%s|%s", sPIN, sAPN, sLogin, sPassword);
			return TRUE;
		}
		case chnExtGPRSCom:
		{
			byte Channel = mapGetByteValue(cfgComPCExtGPRSModemChannel);
			card DialTimeoutSec = mapGetByteValue(cfgComPCExtGPRSModemDialTimeout), TCPConnectionTimeoutSec = mapGetByteValue(cfgComPCConnectionTimeout);
			char sLogin[lenPPPLogin], sPassword[lenPPPPassword],
					sChannelConfig[lenChn], sInit1[lenGPRSModemInitLine],
					sInit2[lenGPRSModemInitLine], sInit3[lenGPRSModemInitLine],
					sInit4[lenGPRSModemInitLine], sInit5[lenGPRSModemInitLine],
					sPhone[lenGPRSModemPhone], sDialTimeout[12 + 1],
					sTCPConnectionTimeout[12 + 1];
			mapGet(cfgComPCGPRSPPPLogin, sLogin, lenPPPLogin);
			mapGet(cfgComPCGPRSPPPPassword, sPassword, lenPPPPassword);
			mapGet(cfgComPCExtGPRSChannelConfig, sChannelConfig, lenChn);
			mapGet(cfgComPCExtGPRSModemInit1, sInit1, lenGPRSModemInitLine);
			mapGet(cfgComPCExtGPRSModemInit2, sInit2, lenGPRSModemInitLine);
			mapGet(cfgComPCExtGPRSModemInit3, sInit3, lenGPRSModemInitLine);
			mapGet(cfgComPCExtGPRSModemInit4, sInit4, lenGPRSModemInitLine);
			mapGet(cfgComPCExtGPRSModemInit5, sInit5, lenGPRSModemInitLine);
			mapGet(cfgComPCExtGPRSModemPhone, sPhone, lenGPRSModemPhone);
			sprintf(sDialTimeout, "%u", (unsigned int) DialTimeoutSec);
			sprintf(sTCPConnectionTimeout, "%u",
					(unsigned int) TCPConnectionTimeoutSec);

			sprintf(Config, "%s|%s|%u|%s|", sLogin, sPassword, Channel,
					sChannelConfig);
			if (strlen(sInit1) != 0)
			{
				strcat(Config, sInit1);
				strcat(Config, "\r");
			}
			if (strlen(sInit2) != 0)
			{
				strcat(Config, sInit2);
				strcat(Config, "\r");
			}
			if (strlen(sInit3) != 0)
			{
				strcat(Config, sInit3);
				strcat(Config, "\r");
			}
			if (strlen(sInit4) != 0)
			{
				strcat(Config, sInit4);
				strcat(Config, "\r");
			}
			if (strlen(sInit5) != 0)
			{
				strcat(Config, sInit5);
				strcat(Config, "\r");
			}
			strcat(Config, "|");
			if (strlen(sPhone) != 0)
				strcat(Config, sPhone);
			strcat(Config, "|");
			strcat(Config, sDialTimeout);
			strcat(Config, "|");
			strcat(Config, sTCPConnectionTimeout);
			return TRUE;
		}
	}
	return FALSE;
}

/**
 @brief Конфигурирует коммуникационный канал и открывает подключение к серверу
 В ChannelError возвращает ошибку канала, которую можно преобразовать в текст
 @return TRUE if OK, or dial error code
 in SelectedServer returns last tried server
 */
int pcOpenChannel(byte ServerNumber, int *ChannelError)
{
	*ChannelError = LL_ERROR_OK;
	int ret;
	trcS("pcOpenChannel");
	char sConfig[lenGPRSConfig] = "";
	comSwitchToSlot(cslotPC);
	byte Channel = pcGetChannel();
	ret = comStart(Channel); // open channel
	if (ret >= 0)
	{
		if (pcGetChannelConfig(Channel, sConfig))
		{
			ret = comSet(sConfig); // set parameters
			if (ret >= 0)
			{
				// set connection timeout 0 - without timeout
				comSetTCPConnectionTimeout(mapGetByteValue(cfgComPCConnectionTimeout) * 100);
				// build communication string
				char cs[lenIPAddress + 6 + 1]; // address+port+nt
				ret = pcGetDialString(ServerNumber, cs) ? comDial(cs) : -1; // try connect for valid server config
			}
			else
				*ChannelError = ret;
		}
		else
			*ChannelError = LL_ERROR_UNKNOWN_CONFIG;
	}
	else
		*ChannelError = ret;
	return ret;
}

Bool pcCloseChannel() // close channel
{
	int ret;
	trcS("pcCloseChannel");

	comSwitchToSlot(cslotPC);
	ret = comHangStart();
	if (ret >= 0)
	{
		ret = comHangWait();
		if (ret >= 0)
		{
			ret = comStop();
			if (ret >= 0)
				return TRUE;
		}
	}
	return ret;
}

static Bool pcConnected = FALSE;
Bool pcIsConnected() { return pcConnected; }

Bool pcDisconnectFromServer() // send disconnect message
{
	trcS("pcDisconnectFromServer");
	comSwitchToSlot(cslotPC);
	int ret = 1;
	if (pcConnected)
		ret = comSend(pccEOT);
	pcCloseChannel();
	pcConnected = FALSE;
	return ret > 0;
}

Bool pcConnectToServer()
{
	trcS("pcConnectToServer");
	Bool Result = FALSE;
	int ret;
	byte Message[60];
	byte pcPV = mapGetByteValue(cphPCProtocolVersion);
	if (pcPV == 0)
	{
		PrintRefusal("Can not to use basic unciphered protocol for communicating with remote server.\n");
		return FALSE;
	}
	byte CodogrammSize = cphGetCodogrammSize(TRUE);
	card Timeout = mapGetWordValue(cfgComPCTimeout) * 100;

	comSwitchToSlot(cslotPC);

	Message[0] = pccENQ; // connection query
	Message[1] = pcPV; // send protocol version
	Message[2] = mapGetByteValue(cphPCKeySetNumber); // send key set number
	//memcpy(Message+1, &ProtocolVersion, sizeof(ProtocolVersion));
	ret = comSendBufLarge(Message, 3); // send connection query
	if (ret == 3) // if ok, wait for receive answer
	{
		tBuffer buf;
		bufInit(&buf, Message, 1 + CodogrammSize); // ACK + Codogramm
		ret = comRecvBufLarge(&buf, buf.dim, Timeout);
		if (ret == buf.dim) // ok, answer is here
			if (Message[0] == pccACK) // first symbol id right, now lets'go crypto
			{
				if (CodogrammSize == 0)
					Result = TRUE;
				else
				{
					byte *cipher = Message + 1;
					cphDecryptCodogramm(TRUE, cipher, cipher, (byte *)PCTransportKey, CodogrammSize); // decypher server codogramm
					byte receivedcrc = *(cipher + CodogrammSize - 1); // last byte of message

					byte crc = CalcCRC8(cipher, CodogrammSize - 1); // calculate crc
					if (crc == receivedcrc) // ok, crc is equal
					{
						cphMakeSessionKey(TRUE, cipher, PCSessionKey, (byte *)PCClientKey, CodogrammSize); // create session key
						//printS("Session key:\n");
						//PrintBytes(FALSE, SessionKeyDES, 0, 8);
						// подумать над отправкой на сервер инициализирующего пакета с информацией о терминале для проверки Session Key
						Result = TRUE; // connection estabilished
					}
					else
						printS("Invalid codogramm CRC\nConnection refused\n");
				}
			}
	}
	pcConnected = Result;
	if (!Result)
		pcDisconnectFromServer(); // disconnect if error
	return Result;
}

// Проверяет соединение с ПЦ с помощью BEL и возвращает TRUE, если мы реально подключены к ПЦ
Bool pcCheckConnection()
{
	trcS("pcCheckConnection");
	comSwitchToSlot(cslotPC);

	if (!pcIsConnected())
		return FALSE; // можно не проверять, если мы не соединены

	card Timeout = mapGetWordValue(cfgComPCTimeout) * 100;
	int ret = comSend(pccBEL); // send BEL
	if (ret > 0)
	{
		byte answer = 0x00;
		ret = comRecv(&answer, Timeout);
		if (ret > 0)
		{
			if (answer == pccBEL)
				return TRUE;
		}
	}

	pcDisconnectFromServer(); // устанавливаем признак того, что мы не соединены с сервером
	return FALSE;
}

// возвращает TRUE, если терминал находится в режиме поддержания постоянного соединения с ПЦ
Bool pcShouldPermanentlyConnected()
{
	return mapGetByteValue(cfgComPCPermanentConnection) != 0;
}

// =========================================== Работа со стандартными тегами ============================================================================

static void pcAppendShiftAuthUD(tlvWriter *tw)
{
	byte buffer[lenShiftAuthID];
	mapGet(regShiftAuthID, buffer, lenShiftAuthID);
	if (!arrIsEmpty(buffer, lenShiftAuthID))
		tlvwAppend(tw, srtShiftAuthID, sizeof(buffer), buffer); // shift auth ID
}

// добавить в буфер ещё неотправленные стандартные теги
Bool pcAppendStandardTags(tlvWriter *tw)
{
	card SendedTagsFlag = mapGetCardValue(traSendedTagsFlag);

	// если ID терминала мы ещё не отправляли, пихаем в буфер и устанавливаем флаг того, что мы его уже отправляли
	if (!HAS_FLAG(SendedTagsFlag, stfFlags))
	{
		word PCFlags = mapGetWordValue(traPCFlags);
		if (PCFlags != 0)
			tlvwAppendWord(tw, srtFlags, PCFlags); // current flags
		SendedTagsFlag |= stfFlags;
	}
	if (!HAS_FLAG(SendedTagsFlag, stfTerminalID))
	{
		tlvwAppendKeyCard(tw, srtTerminalID, cfgTrmTerminalID); // terminal ID
		SendedTagsFlag |= stfTerminalID;
	}

	if (!HAS_FLAG(SendedTagsFlag, stfCardNumber) && !traCardNumberIsEmpty()) // если номер карты не отправлялся ранее и номер карты не пустой
	{
		char CardNumber[lenCardNumber], BCDCardNumber[lenCardNumber];
		mapGet(traCardNumber, CardNumber, lenCardNumber);
		int lenBCDCardNumber = StringToBCD(CardNumber, BCDCardNumber);
		tlvwAppend(tw, srtCardID, lenBCDCardNumber, BCDCardNumber); // card number
		byte CardReadingMethod;
		mapGetByte(traCardReadingMethod, CardReadingMethod);
		if (CardReadingMethod > 0)
			tlvwAppendByte(tw, srtCardReadingMethod, CardReadingMethod);

		if (ocdCanStore()) // если на карте можно хранить аварийную информацию, то делаем на сервер запрос о её обновлении
		{
			tlvWriter cdtw; tlvwInit(&cdtw); // запрос на обновление offline-данных карты
			tlvwAppendCard(&cdtw, scdtCRC, mapGetCardValue(ofcdCRCReaded));
			tlvwAppend(tw, srtOfflineCardData, cdtw.DataSize, cdtw.Data);
			tlvwFree(&cdtw);

			tlvwInit(&cdtw);
			tlvwAppendCard(&cdtw, sclltCRC, mapGetCardValue(ofcdLimitsCRCReaded));
			tlvwAppend(tw, srtOfflineCardLimits, cdtw.DataSize, cdtw.Data);
			tlvwFree(&cdtw);
		}
		SendedTagsFlag |= stfCardNumber;
	}
	if (!HAS_FLAG(SendedTagsFlag, stfDateTime))
	{
		tlvwAppendCard(tw, srtTerminalDateTime, mapGetCardValue(traDateTimeUnix)); // date time
		SendedTagsFlag |= stfDateTime;
	}
	if (!HAS_FLAG(SendedTagsFlag, stfShiftAuthID))
	{
		pcAppendShiftAuthUD(tw);
		SendedTagsFlag |= stfShiftAuthID;
	}
	if (!HAS_FLAG(SendedTagsFlag, stfSerialNumber))
	{
		char SerialNumber[lenSerialNumber];
		memset(SerialNumber, 0, sizeof(SerialNumber));
		getSapSer(NULL, SerialNumber, 0);

		tlvWriter ditw; tlvwInit(&ditw);
		tlvwAppendString(&ditw, ditSerialNumber, SerialNumber);
		tlvwAppend(tw, srtDeviceInfo, ditw.DataSize, ditw.Data); // device info
		tlvwFree(&ditw);
		//tlvwAppendCard(tw, srtSerialNumber, GetSerialNumber());
		SendedTagsFlag |= stfSerialNumber;
	}
	mapPutCard(traSendedTagsFlag, SendedTagsFlag);
	return TRUE;
}

// =========================================================== SEND MESSAGES ====================================================================

// send to server query for get card info
void pcSendQueryCardInfo(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	if (!pcInOffline())
	{
		tlvWriter tw; tlvwInit(&tw);

		tlvwAppendByte(&tw, srtPackageID, pcpCardInfo); // packet ID
		if (IsLastPackage) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package
		pcAppendStandardTags(&tw);
		/*if (IsLastPackage)
			tlvwAppendByte(&tw, srtIsLastPackage, IsLastPackage);*/
		if (SendRSPackage)
			tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // Пакет из СУ ККМ

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		//Message = memFree(Message);
		tlvwFree(&tw);
		dsFree(dslRSPackage);
	}
#ifdef OFFLINE_ALLOWED
	else
		switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryCardInfo(IsLastPackage, SendRSPackage, PCState, RSState); break; // для аварийного режима offline
			case ofpVoiceAuthorization: PrintRefusal8859("Не поддерживается\nв режиме голосовой\nавторизации\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("Неизвестный тип offline\n"); *PCState = lssDisconnecting; break;
		}
#endif
}

void pcSendQueryLiteCardInfo(byte *PCState, byte *RSState)
{
	if (!pcInOffline())
	{
		tlvWriter tw; tlvwInit(&tw);

		tlvwAppendByte(&tw, srtPackageID, pcpLiteCardInfo); // packet ID
		pcAppendStandardTags(&tw);
		tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // Пакет из СУ ККМ

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		//Message = memFree(Message);
		tlvwFree(&tw);
		dsFree(dslRSPackage);
	}
#ifdef OFFLINE_ALLOWED
	else
	{
		PrintRefusal8859("Не реализована в режиме offline на текущий момент\n"); *PCState = lssDisconnecting;
	}

		/*switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryCardInfo(IsLastPackage, SendRSPackage, PCState, RSState); break; // для аварийного режима offline
			case ofpVoiceAuthorization: PrintRefusal8859("Не поддерживается\nв режиме голосовой\nавторизации\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("Неизвестный тип offline\n"); *PCState = lssDisconnecting; break;
		}*/
#endif
}

// запрос за соединение от внешнего приложения
void pcSendQueryConnectionFromExternalApp(byte *PCState, byte *RSState)
{
	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpConnectionFromExternalApplication); // packet ID
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

/*ПЕРЕДЕЛАТЬ НА ИСПОЛЬЗОВАНИЕ СТАНДАРТНОГО МЕХАНИЗМА ЧЕРЕЗ ККМ
 В ФЛАГИ ККМ ПЕРЕДАВАТЬ ФЛАГ "БЕЗ КАРТЫ", ЧТОБЫ НЕ БЫЛО ПРОВЕРКИ ПИН-КОДА
 СООБЩЕНИЯ ИЗ СЕРВЕРА ОСВОБОДИТЬ И УБРАТЬ ИЗ ДОКИ К ПРОТОКОЛУ*/

// запрос на полный возврат без ККМ
void pcSendQueryCancellationWithoutRS(byte *PCState, byte *RSState)
{
	// формируем буфер для передачи обработчику возврата
	card MessageSize = dsGetDataSize(dslRSPackage);
	byte *Message = memAllocate(MessageSize);
	tBuffer buf;
	bufInit(&buf, Message, MessageSize);
	memcpy(Message, dsGetData(dslRSPackage), MessageSize); // копируем содержимое датаслота в буфер
	dsFree(dslRSPackage); // освобождаем слот

	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // т.к. возврат без ККМ, то нужно разовать соединение с ПЦ после получения ответа

	rsHandleQueryCancellation(&buf, PCState, RSState);

	Message = memFree(Message);
}

// Отправка запроса на закрытие смены
void pcSendQueryStartValidation(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID);

	// Нужно отправить Flags и TerminalID
	// build message
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpValidation);
	pcAppendStandardTags(&tw);
	tlvwAppendCard(&tw, srtShiftDateBegin, mapGetCardValue(regShiftDateBegin));
	tlvwAppendCard(&tw, srtShiftDateEnd, mapGetCardValue(regShiftDateEnd));
	tlvwAppendCard(&tw, srtShiftNumber, mapGetCardValue(traShiftNumber));
	pcAppendShiftAuthUD(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// Отправка запроса на авторизацию терминала
void pcSendQueryStartTerminalAuth(byte *PCState, byte *RSState)
{
	tlvWriter tetw; tlvwInit(&tetw);
	tlvwAppendCard(&tetw, tetCRC, mapGetCardValue(trmeCRC)); // CRC of terminal environment

	char SerialNumber[lenSerialNumber], ReferenceNumber[24], sManufacturingDate[9];
	memset(SerialNumber, 0, sizeof(SerialNumber));
	memset(ReferenceNumber, 0, sizeof(ReferenceNumber));
	memset(sManufacturingDate, 0, sizeof(sManufacturingDate));

	getSapSer(ReferenceNumber, SerialNumber, 0);

	card TerminalType = 0, InstallDate = mapGetCardValue(actInstallDate), ActivationDate = mapGetCardValue(actCodeDate);

	tlvWriter ditw; tlvwInit(&ditw);
	tlvwAppendString(&ditw, ditSerialNumber, SerialNumber);
	tlvwAppendString(&ditw, ditReferenceNumber, ReferenceNumber);
	tlvwAppendString(&ditw, ditApplicationName, PROGRAM_NAME); // app name & version
	tlvwAppendCard(&ditw, ditApplicationVersion, PROGRAM_BUILD); // app build
	tlvwAppendCard(&ditw, ditVolatileSize, RamGetSize());
	tlvwAppendCard(&ditw, ditVolatileFree, FreeSpace());
	tlvwAppendCard(&ditw, ditNonvolatileSize, flshGetSize());
	tlvwAppendCard(&ditw, ditNonvolatileFree, flshGetFree());
	// дата установки ПО и дата активации
	if (InstallDate != 0) tlvwAppendCard(&ditw, ditApplicationInstallationDate, InstallDate);
	if (ActivationDate != 0) tlvwAppendCard(&ditw, ditApplicationActivationDate, ActivationDate);
	// платформа и тип терминала
	if (SystemFioctl(SYS_FIOCTL_GET_TERMINAL_TYPE, &TerminalType) == 0)
	{
		tlvwAppendByte(&ditw, ditPlatformType, 1); // 1 - Telium platform
		tlvwAppendCard(&ditw, ditTerminalType, TerminalType);
	}
	// дата производства терминала
	if (SystemFioctl(SYS_FIOCTL_GET_PRODUCT_MANUFACTURING_DATE, sManufacturingDate) == 0 && strlen(sManufacturingDate) > 0)
	{
		char UTATime[lenDatTim]; memset(UTATime, 0, sizeof(UTATime));
		strncpy(UTATime, sManufacturingDate + 4, 4); // copy year
		strncpy(UTATime + 4, sManufacturingDate + 2, 2); // copy month
		strncpy(UTATime + 6, sManufacturingDate, 2); // copy day
		strcat(UTATime, "000000"); // set time
		card ManufacturingDate = utFromUTA(UTATime); // convert to UNIX format
		if (ManufacturingDate > 0)
			tlvwAppendCard(&ditw, ditTerminalManufacturingDate, ManufacturingDate);
	}

	Bool SendActivationQuery = mapGetByteValue(cfgRemoteActivation) != 0 && !actIsActivated();
	tlvWriter acttw; tlvwInit(&acttw);
	if (SendActivationQuery)
	{
		tlvwAppendCard(&acttw, aitFirstNumber, actGenGetFirstNumber());
		tlvwAppendCard(&acttw, aitSecondNumber, actGenGetSecondNumber());
		tlvwAppendCard(&acttw, aitDate, utExtractDate(mapGetCardValue(traDateTimeUnix)));
	}

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // Нужно отправить Flags, TerminalID, DateTime
	// build message
	tlvWriter tw; tlvwInit(&tw);
	tlvwAppendByte(&tw, srtPackageID, pcpTerminalAuth);
	pcAppendStandardTags(&tw);
	/*tlvwAppendCard(&tw, srtSerialNumber, GetSerialNumber()); // terminal serial number
	tlvwAppendCard(&tw, srtVersion, PROGRAM_BUILD); // app build
	tlvwAppendString(&tw, srtAppName, PROGRAM_NAME); // app name & version*/
	tlvwAppendCard(&tw, srtShiftNumber, mapGetCardValue(regShiftNumber)); // Shift number
	tlvwAppend(&tw, srtEnvironmentInfo, tetw.DataSize, tetw.Data); // Current CRC of terminal environment
	tlvwAppend(&tw, srtDeviceInfo, ditw.DataSize, ditw.Data); // device info
	if (SendActivationQuery)
		tlvwAppend(&tw, srtTerminalActivationInfo, acttw.DataSize, acttw.Data); // device info
	tlvwFree(&tetw);
	tlvwFree(&ditw);
	tlvwFree(&acttw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

void pcSendUnknownATR(byte *PCState, byte *RSState)
{
	/*if (!IsEmptyATR(dsGetData(dslUnknownATR), dsGetDataSize(dslUnknownATR))) // отправляем ATR только если он не пустой
	{
	Убрано для диагностики проблем с ридерами на установленных терминалах
	}
	else
		*PCState = lssQueryForDisconnect;*/
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // Нужно отправить Flags, TerminalID
	tlvWriter tw; tlvwInit(&tw);

	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package

	tlvwAppendByte(&tw, srtPackageID, pcpUnknownATR);
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtATR, dsGetDataSize(dslUnknownATR), dsGetData(dslUnknownATR));
	dsFree(dslUnknownATR);
	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssQueryForDisconnect : lssProtocolError;
	tlvwFree(&tw);

	mapPutCard(traSendedTagsFlag, 0); // если вдруг сессия не завершена, то мы восстанавливаем флаги отправки
}

void pcSendQueryAcceptQuest(byte *PCState, byte *RSState)
{
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpAcceptQuest);
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// get file list for software update
void pcSendQueryFileList(char* FolderName, byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // Нужно отправить Flags, TerminalID
	// build message
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpFileList);
	pcAppendStandardTags(&tw);
	if (FolderName && strlen(FolderName) > 0) // FolderName - имя подпапки на сервере, где будут лежать файлы
		tlvwAppendString(&tw, srtRootFolder, FolderName);
	//tlvwAppendCard(&tw, srtVersion, PROGRAM_BUILD);

	tlvWriter ditw; tlvwInit(&ditw);
	tlvwAppendCard(&ditw, ditApplicationVersion, PROGRAM_BUILD); // отправляем билд приложения в запросе на обновления ПО/конфигурации
	tlvwAppend(&tw, srtDeviceInfo, ditw.DataSize, ditw.Data); // device info
	tlvwFree(&ditw);


	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// Отсылка периодического сообщения на OPS: center
void pcSendPeriodic(byte *PCState, byte *RSState)
{
	// prepare
	if (mapGetCardValue(cfgTrmTerminalID) == 0)
	{
		*PCState = lssQueryForDisconnect;
		return; // если терминал не настроен, то ничего не передаём, а просто выходим
	}

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // Нужно отправить Flags, TerminalID, DateTime
	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpOnlineState); //
	pcAppendStandardTags(&tw);
	Bool SendResult = opsSendMessage(cslotPC, tw.Data, tw.DataSize);
	if (SendResult)
		mapPutByte(traResponse, crSuccess);
	*PCState = SendResult ? lssQueryForDisconnect : lssProtocolError;
	tlvwFree(&tw);
}

void pcSendAutonomousTransaction(byte *PCState, byte *RSState)
{
	if (!pcInOffline())
	{
		tlvWriter tw; tlvwInit(&tw);

		// build message
		tlvwAppendByte(&tw, srtPackageID, pcpCloseReceipt); // packet ID
		mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package - server should end connection after answer
		pcAppendStandardTags(&tw);
		//tlvwAppendByte(&tw, srtIsLastPackage, 1); заменено на флаги
		tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // Package with autonomouse transaction

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		dsFree(dslRSPackage);
		tlvwFree(&tw);
	}
#ifdef OFFLINE_ALLOWED
	else
	{
		tBuffer buf;
		tlvBufferInit(&buf, dsGetData(dslRSPackage), dsGetDataSize(dslRSPackage));
		switch (oflGetPossibility())
		{
			case ofpOffline: oflrsHandleCloseReceipt(&buf, PCState, RSState); break; // для аварийного режима offline
			case ofpVoiceAuthorization: vaHandleCloseReceipt(&buf, PCState, RSState); break;
			default: PrintRefusal8859("Неизвестный тип offline\n"); break;
		}
	}
#endif
}

// get list of autonomous goods
void pcSendQueryAutonomousGoods(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // Нужно отправить Flags, TerminalID, DateTime
	tlvWriter tw; tlvwInit(&tw);
	// build message

	tlvwAppendByte(&tw, srtPackageID, pcpAutonomousGoods);
	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package
	pcAppendStandardTags(&tw);
	//tlvwAppendByte(&tw, srtIsLastPackage, 1);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// get set of keys for ciphering
void pcSendQueryKeySet(byte *PCState, byte *RSState)
{
	// build query
	tlvWriter querytw; tlvwInit(&querytw);
	tlvwAppendWord(&querytw, kstKeySetID, mapGetWordValue(traKeySetID));

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // Нужно отправить Flags, TerminalID
	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpKeySet);
	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtKeySetInfo, querytw.DataSize, querytw.Data);
	//tlvwAppendByte(&tw, srtIsLastPackage, 1);
	tlvwFree(&querytw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

void pcSendQueryConfirmation(byte *PCState, byte *RSState)
{
	if (!pcInOffline())
	{
		tlvWriter tw; tlvwInit(&tw);

		byte PINBlock[lenPINBlock];
		mapGet(traPINBlock, PINBlock, lenPINBlock);

		tlvwAppendByte(&tw, srtPackageID, pcpConfirmation);
		tlvwAppend(&tw, srtPINBlock, lenPINBlock, PINBlock);
		tlvwAppendByte(&tw, srtPINMethod, 1); // PIN блок пересылаем всегда в формате ISO9564 format 0

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		tlvwFree(&tw);
	}
	#ifdef OFFLINE_ALLOWED
	else
		switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryConfirmation(PCState, RSState); break; // для аварийного режима offline
			case ofpVoiceAuthorization: PrintRefusal8859("Не поддерживается\nв режиме голосовой\nавторизации\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("Неизвестный тип offline\n"); *PCState = lssDisconnecting; break;
		}
	#endif

}

void pcSendQueryLogotype(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // заполняем все флаги отправленных стандартных тегов кроме флагов и ID терминала

	tlvWriter tw; tlvwInit(&tw);
	tlvwAppendByte(&tw, srtPackageID, pcpLogotype); // packet ID
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// send to server query for get card info
void pcSendQueryRefillAccount(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	if (!SendRSPackage) // если нету пакета от ККМ - делаем свой пакет
	{
		tlvWriter rstw; tlvwInit(&rstw);

		tlvwAppendByte(&rstw, rstPackageID, rspQueryRefillAccount); // refill account
		tlvwAppendCard(&rstw, rstAmount, mapGetCardValue(traAmount)); // amount
		tlvwAppendCard(&rstw, rstDateTime, mapGetCardValue(traDateTimeUnix)); // date time

		dsAllocate(dslRSPackage, rstw.Data, rstw.DataSize);
		tlvwFree(&rstw);
	}

	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpRefillAccount); // packet ID
	if (IsLastPackage)
		mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // tag contains the RS packet
	/*if (IsLastPackage)
		tlvwAppendByte(&tw, srtIsLastPackage, IsLastPackage); // Is last package*/

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError; // send message to PC
	tlvwFree(&tw);
}

void pcSendQueryCardActionsList(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	// предварительно очищаем список акций (если был) и создаём новый
	calistAllocate();

	if (!SendRSPackage) // если нету пакета от ККМ - делаем свой пакет
	{
		tlvWriter rstw; tlvwInit(&rstw);

		tlvwAppendByte(&rstw, rstPackageID, rspCardActionsList); // refill account

		tlvWriter catw; tlvwInit(&catw);
		tlvwAppendByte(&catw, catState, mapGetByteValue(traCardActionState)); // запрос
		tlvwAppend(&rstw, rstCardAction, catw.DataSize, catw.Data);
		tlvwFree(&catw);

		dsAllocate(dslRSPackage, rstw.Data, rstw.DataSize);
		tlvwFree(&rstw);
	}

	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpCardActionsList); // packet ID
	if (IsLastPackage)
		mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // If is last package
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // tag contains the RS packet

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError; // send message to PC
	tlvwFree(&tw);
}

void pcSendQueryEnableAction(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	if (!SendRSPackage) // если нету пакета от ККМ - делаем свой пакет
	{
		tlvWriter rstw; tlvwInit(&rstw);

		tlvwAppendByte(&rstw, rstPackageID, rspEnableAction); // refill account

		tlvWriter catw; tlvwInit(&catw);
		tlvwAppendCard(&catw, catID, mapGetCardValue(traCardActionID)); // ID подключаемой или отключаемой
		tlvwAppendByte(&catw, catState, mapGetByteValue(traCardActionState)); // команда на изменение состояния акции (0 - подключить, 1 - отключить)
		tlvwAppend(&rstw, rstCardAction, catw.DataSize, catw.Data);
		tlvwFree(&catw);

		dsAllocate(dslRSPackage, rstw.Data, rstw.DataSize);
		tlvwFree(&rstw);
	}

	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpEnableAction); // packet ID
	if (IsLastPackage)
		mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // If is last package
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // tag contains the RS packet

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError; // send message to PC
	tlvwFree(&tw);
}

void pcSendQueryDateTime(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // заполняем все флаги отправленных стандартных тегов кроме флагов и ID терминала
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpDateTime); // packet ID
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

void pcSendQueryRemindPIN(byte *PCState, byte *RSState)
{
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpRemindPIN); // packet ID
	pcAppendStandardTags(&tw);
	tlvwAppendByte(&tw, srtRemindPINVariant, mapGetByteValue(traPINRemindVariant)); // variant of reminding selected

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// send to server query for get card info
void pcSendQueryCopyOfReceipt(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	if (!SendRSPackage) // если нету пакета от ККМ - делаем свой пакет
	{
		tlvWriter rstw; tlvwInit(&rstw);

		char TraID[lenID];
		mapGet(traPurchaseID, TraID, lenID);

		tlvwAppendByte(&rstw, rstPackageID, rspQueryCopyOfReceipt); // get a copy...
		tlvwAppendBCDString(&rstw, rstPurchaseID, TraID); // for this receipt

		dsAllocate(dslRSPackage, rstw.Data, rstw.DataSize);
		tlvwFree(&rstw);
	}

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // Нужно отправить Flags, TerminalID и DateTime

	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpCopyOfReceipt); // packet ID
	if (IsLastPackage) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package
	pcAppendStandardTags(&tw);
	/*if (IsLastPackage)
		tlvwAppendByte(&tw, srtIsLastPackage, IsLastPackage); // Is last package*/
	tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // Retail system package

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	dsFree(dslRSPackage);
	tlvwFree(&tw);
}

// =============================================================== PROCESSING CENTER PACKETS HANDLING =============================================
static Bool pcIsPINVerificationRequired()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfPINVerificationRequired);
}

// true, если текущя транзакция - транзитная
Bool pcIsTransitTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsTransitTransaction);
}

// true, если текущя транзакция - offline аварийная
Bool pcIsOfflineTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsOfflineTransaction);
}

// true, если текущя транзакция - offline голосовая авторизация
Bool pcIsVoiceAuthTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsVoiceAuthTransaction);
}

// true, если текущий пакет является последним в сессии
Bool pcIsLastPackage()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage);
}

// парсит структуру TransmissionControl, заполняя соотв. записи в БД. Если ключ БД = 0, то запись не производится
// Flags - byte, Index & Count - word
void ParseTransmissionControl(byte *Buffer, card BufferSize, word keyFlags, word keyIndex, word keyCount)
{
	tlvReader tr;
	tlvrInit(&tr, Buffer, BufferSize);
	byte tag;
	card len;
	byte *value;
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
		value = memAllocate(len + 1); // +1 for strings
		tlvrNextValue(&tr, len, value);
		switch (tag)
		// на 1 уже не обращаем внимания - мы его увидели раньше
		{
			case tctFlags:
				if (keyFlags != 0)
					mapPutByte(keyFlags, value[0]);
				break;
			case tctIndex:
				if (keyIndex != 0)
					mapPutWord(keyIndex, tlv2word(value));
				break;
			case tctCount:
				if (keyCount != 0)
					mapPutWord(keyCount, tlv2word(value));
				break;
		}
		value = memFree(value); // FREE MEMORY OF VALUE
	}
}

// разбирает полученное окружение терминала
static void ParseTerminalEnvironment(byte *Buffer, card BufferSize)
{
	tlvReader tr;
	tlvrInit(&tr, Buffer, BufferSize);
	byte tag;
	card len;
	byte *value;

	card CurrentPCOwnerID = mapGetCardValue(trmePCOwnerID); // сохраняем текущего владельца ПЦ
	// очищаем TerminalConfig перед парсингом новой конфигурации
	mapReset(trmeBeg);
	/*int i;
	 for (i = trmeBeg+1; i < trmeEnd; i++)
	 mapPut((word)i, "\x00", 1);*/

	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
		value = memAllocate(len + 1); // +1 for strings
		value[len] = 0;
		tlvrNextValue(&tr, len, value);
		switch (tag)
		// на 1 уже не обращаем внимания - мы его увидели раньше
		{
			case tetCRC: mapPutCard(trmeCRC, tlv2card(value)); break;
			case tetPCOwnerID:
			{
				card ReceivedPCOwnerID = tlv2card(value);
				// если у нас терминал уже был настроен на какого-то владельца ПЦ и он вдруг меняется - это ужасно!
				// Надо сообщить об этом и перезагрузить терминал.
				if (CurrentPCOwnerID != 0 && CurrentPCOwnerID != ReceivedPCOwnerID)
				{
					mapPutByte(traResponse, crSecurityAlert);
					PrintRefusal8859("Ошибка безопасности!\nЗапрещено изменение\nвладельца ПЦ. Терминал\nбудет перезагружен...\n");
					ttestall(PRINTER, 0); // ждём пока принтер распечатает
					SystemFioctl(SYS_FIOCTL_SYSTEM_RESTART, NULL); // restart the terminal
				}
				else
					mapPutCard(trmePCOwnerID, tlv2card(value));
				break;
			}
			case tetNetworkName:
				if (len != 0)
				{
					value[len] = 0;
					mapPut(trmeNetworkName, value, len + 1);
				}
				break;
			case tetNetworkOwnerID: mapPutCard(trmeNetworkOwnerID, tlv2card(value)); break;
			case tetGlobalTerminalID: mapPutCard(trmeGlobalTerminalID, tlv2card(value)); break;
			case tetNetworkTaxInfo:
				if (len != 0)
				{
					value[len] = 0;
					mapPut(trmeNetworkTaxInfo, value, len + 1);
				}
				break;
			case tetServicePointName:
				if (len != 0)
				{
					value[len] = 0;
					mapPut(trmeServicePointName, value, len + 1);
				}
				break;
			case tetServicePointAddress:
				if (len != 0)
				{
					value[len] = 0;
					mapPut(trmeServicePointAddress, value, len + 1);
				}
				break;
			case tetGlobalRetailSystemID: mapPutCard(trmeGlobalRetailSystemID, tlv2card(value)); break;
			case tetCurrencyID: mapPutCard(trmeCurrencyID, tlv2card(value)); break;
			case tetCurrencyIntCode: mapPutWord(trmeCurrencyIntCode, tlv2word(value)); break;
			case tetPCID: mapPutCard(trmePCID, tlv2card(value)); break;
			case tetRoundingMode: mapPutByte(trmeRoundingMode, tlv2byte(value)); break;
			case tetRoundingDigits: mapPutByte(trmeRoundingDigits, tlv2byte(value)); break;
			case tetRoundingMethod: mapPutByte(trmeRoundingMethod, tlv2byte(value)); break;
			case tetRSCodePage: mapPutByte(trmeRSCodePage, tlv2byte(value)); break;
		}
		value = memFree(value); // FREE MEMORY OF VALUE
	}
	mapPutByte(trmeTagReceived, 1); // ставим признак того, что мы приняли тег с окружением терминала
	mapSave(trmeBeg); // сохраняем распарсенные значения TerminalEnvironment
}

// Разбирает полученную структуру с данными об удалённой активации терминала
static void ParseTerminalActivation(byte *Buffer, card BufferSize)
{
	tlvReader tr;
	tlvrInit(&tr, Buffer, BufferSize);
	byte tag;
	card len;
	byte *value;

	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
		value = memAllocate(len + 1); // +1 for strings
		tlvrNextValue(&tr, len, value);
		switch (tag)
		// на 1 уже не обращаем внимания - мы его увидели раньше
		{
			case aitCode:
			{
				card Code = tlv2card(value);
				if (!actTryActivate(utGetDateTime(), Code))
					tmrPause(1); // если активация была неудачной - делаем паузу в секунду для предотвращения брут-форса
				break;
			}
		}
		value = memFree(value); // FREE MEMORY OF VALUE
	}
}

static Bool TagCardRangesListReceived = FALSE;
static Bool TagPursesListReceived = FALSE;
static Bool TagGoodsToProductsListReceived = FALSE;
static Bool TagProductGroupsListReceived = FALSE;
static Bool TagPCPursesRecodeListReceived = FALSE;

static Bool pcParse(tBuffer *buf)
{
	TagCardRangesListReceived = FALSE;
	TagPursesListReceived = FALSE;
	TagGoodsToProductsListReceived = FALSE;
	TagProductGroupsListReceived = FALSE;
	TagPCPursesRecodeListReceived = FALSE;

	// read the buffer
	tlvReader tr;
	tlvrInit(&tr, buf->ptr, buf->dim);
	byte tag;
	card len;
	byte *value;
	while (tlvrNext(&tr, &tag, &len) == TRUE)
	{
		value = memAllocate(len + 1); // +1 for strings
		tlvrNextValue(&tr, len, value);
		switch (tag)
		// на 1 уже не обращаем внимания - мы его увидели раньше
		{
			/*case srtQueryID:
			 mapPutByte(traQueryID, value[0]);
			 break;*/
			case srtResponse:
				mapPutByte(traResponse, value[0]);
				// ответ внешней системы (0 - успешное завершение, иначе код ошибки)
				break;
			case srtTerminalDateTime:
			{
				card Time = tlv2card(value);
				mapPutCard(traDateTimeUnix, Time); // дата и время терминала (для синхронизации или копии чека)

				char sDateTime[lenDatTim];
				utToUTA(Time, sDateTime);
				mapPut(traDateTimeASCII, sDateTime, lenDatTim);
			}
				break;
			case srtTransactionType:
				mapPutByte(traType, value[0]);
				// тип текущей транзакции
				break;
			case srtTerminalPrinter: // message
				if (len != 0)
				{
					value[len] = 0;
					dsAllocate(dslReceipt, value, len + 1); // в датаслот положить сообщение для чека
				}
				break;
			case srtTerminalID: // terminal id
				mapPutCard(traTerminalID, tlv2card(value));
				break;
			case srtCardID: // card number
				if (len != 0)
				{
					char CardNumber[lenCardNumber];
					BCDToString(value, len, CardNumber);
					SetCardNumber(CardNumber, crmFromProcessingCenter);
				}
				break;
			case srtReportType:
				mapPutByte(traIsIntermediateReport, value[0]);
				break;
			case srtShiftAuthID:
				mapPut(regShiftAuthID, value, len);
				break;
			case srtRetailSystemPackage: // packet for RS
				dsAllocate(dslRSPackage, value, len); // в датаслот положить пакет для системы управления
				tBuffer rsBuf;
				tlvBufferInit(&rsBuf, value, len);
				rsParse(&rsBuf); // парсим пакет для ККМ
				break;
			/*case srtIsLastPackage:
				mapPutByte(traIsLastPackage, value[0]);
				// 1, если сервер указывает на необходмость закрыть соедиение после получения пакета
				break;*/
			case srtFlags:
				mapPutWord(traPCFlags, tlv2word(value));
				break;
			case srtEnvironmentInfo:
				ParseTerminalEnvironment(value, len);
				break;
		#ifdef OFFLINE_ALLOWED
			case srtCardRangesList:
				ocParseCardRangesList(value, len);
				TagCardRangesListReceived = TRUE;
				break;
			case srtPursesList:
				ocParsePursesList(value, len);
				TagPursesListReceived = TRUE;
				break;
			case srtGoodsToProductsList:
				ocParseGoodsToProductsList(value, len);
				TagGoodsToProductsListReceived = TRUE;
				break;
			case srtProductGroupsList:
				ocParseProductGroupsList(value, len);
				TagProductGroupsListReceived = TRUE;
				break;
			case srtPCPursesRecodeList:
				ocParsePCPursesRecodeList(value, len);
				TagPCPursesRecodeListReceived = TRUE;
				break;
		#endif
			case srtOfflineCardData:
				ocdParseCardDataReceived(value, len);
				break;
			case srtOfflineCardLimits:
				ocdParseCardLimitsListReceived(value, len);
				break;
			case srtTerminalActivationInfo:
				ParseTerminalActivation(value, len);
				break;
		}
		value = memFree(value); // FREE MEMORY OF VALUE
	}

	/*word PCFlags = mapGetWordValue(traPCFlags);
	if (HAS_FLAG(PCFlags, pcfIsLastPackage))
		mapPutByte(traIsLastPackage, 1);
	// проверяем, последний ли это пакет в передаче

	//if (pcIsTransitTransaction()) // если транзит*/

	return TRUE;
}

static void pcHandleMessageError(tBuffer *buffer, byte *PCState, byte *RSState)
{
	pcParse(buffer);

	// печать чека отказа
	PrintRefusal(NULL);
	usrInfo(infServerError);

	dsFree(dslReceipt); // освобождаем память

	// передаём пакет в систему управления.
	rsSendPackage();

	*PCState = lssErrorReceived;
}

// parse message and put data into tra memory
static void pcHandleAnswerCardInfo(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		// print the card info receipt
		PrintReceipt(rloCardInfo, 1);

		if (!dsIsFree(dslRSPackage) && !pcIsLastPackage()) // если пакет для ККМ есть и это не последний пакет от сервера, то всё отлично
			*RSState = rssQueryForEstabilishConnection;
		else
			*PCState = lssQueryForDisconnect; // отсоединяемся, если пакет в СУ не пришёл или сервер желает закрыть соедиенние после обработки
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerLiteCardInfo(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		if (!dsIsFree(dslRSPackage) && !pcIsLastPackage()) // если пакет для ККМ есть и это не последний пакет от сервера, то всё отлично
			*RSState = rssQueryForEstabilishConnection;
		else
			*PCState = lssQueryForDisconnect; // отсоединяемся, если пакет в СУ не пришёл или сервер желает закрыть соедиенние после обработки
	}
	else
		*PCState = lssEnterPIN;
}

// ответ на запрос СУ о модификации чека
static void pcHandleAnswerChangeReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		// доделать - печатать чек расчёта бонусов (если нужно)
		if (!dsIsFree(dslReceipt)) // печатаем сообщение, если оно есть
		{
			qprintS(dsGetData(dslReceipt));
			rptReceipt(rloSkip);
			dsFree(dslReceipt);
			printWait();
		}

		// передаём пакет в систему управления. Он есть должен быть ВСЕГДА
		*RSState = rsSendPackage() ? rssPrepareWaitForData : rssProtocolError;
	}
	else
		*PCState = lssEnterPIN;
}

// ответ на запрос СУ на закрытие чека
static void pcHandleAnswerCloseReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		// печатаем чек РЕГИСТРАЦИЯ ПОКУПКИ
		PrintReceipt(rloPurchase, -1);

		// передаём пакет в систему управления. Он есть должен быть ВСЕГДА
		*RSState = rsSendPackage() ? rssPrepareWaitForData : rssProtocolError;
		// если это последний пакет - закрываем соединение с сервером
		*PCState = HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage) ? *PCState : lssQueryForDisconnect;
	}
	else
		*PCState = lssEnterPIN;
}

// обработка ответ на запрос СУ о проведении возврата
static void pcHandleAnswerCancellation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloRefund, -1); // печатаем чек ОТМЕНА РЕГИСТРАЦИЯ ПОКУПКИ

		*RSState = rsIsConnected() ? ( rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в систему управления. Он есть должен быть ВСЕГДА
		*PCState = (pcIsConnected() && !HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage)) ? *PCState : lssQueryForDisconnect;
	}
	else
		*PCState = lssEnterPIN;
}

// ответ от сервера на запрос СУ о проведении возврата без предъявления карты
static void pcHandleAnswerConnectionFromPetrol(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера
	if (!dsIsFree(dslRSPackage)) // проверяем есть или нету пакета для ККМ
		*RSState = rssQueryForEstabilishConnection;
	else
		*PCState = lssQueryForDisconnect; // если пакет в СУ не пришёл, отсоединяемся от сервера
}

// получаем от сервера отчёт за смену
static void pcHandleAnswerValidation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	// печатаем чек ОТЧЕТ ЗА СМЕНУ
	if (mapGetByteValue(traIsIntermediateReport) == 1)
		PrintReceipt(rloIntermediateReport, 1);
	else
	{
		PrintReceipt(rloShiftReport, -1);
		mapPutByte(traResponse, crSuccess);
		// успешное закрытие смены
	}

	*PCState = lssQueryForDisconnect;
}

// ответ за запрос "Принять анкету"
static void pcHandleAnswerAcceptQuest(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloAcceptQuest, 1); // печатаем чек АНКЕТА
		// передаём сообщение для ККМ, если оно есть
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* если мы не соединены с ККМ или это последний пакет в обмене с ПЦ - закрываем соединение*/
	}
	else
		*PCState = lssEnterPIN;
}

int pcGetCardRangeIndex()
{
	int CardRangeIndex = mapGetCardValue(traCardRangeIndex);
	if (CardRangeIndex < 0 && ocHasConfig())
	{
		CardRangeIndex = ocGetCurrentCardRangeIndex();
		if (CardRangeIndex >= 0)
			mapPutCard(traCardRangeIndex, CardRangeIndex);
	}
	return CardRangeIndex;
}

static byte ConstMaxPINAttempts = 3;

static byte pcGetMaxPINAttempts()
{
	int CardRangeIndex = pcGetCardRangeIndex();
	if (CardRangeIndex >= 0)
	{
		mapMove(crdrBeg, CardRangeIndex);
		return mapGetByteValue(crdrMaximumPINAttempts);
	}
	else
		return ConstMaxPINAttempts;
	return CardRangeIndex;
}


// обработка сообщения "Ответ"
static void pcHandleConfirmation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	// обычная обработка ответа
	byte Response = mapGetByteValue(traResponse);
	if (Response == crSuccess) // АП??: может быть HoldConnection или вообще не трогать? АП: не трогаем теперь. Было lssPrepareForWaitData
	{
		// заново вызываем обработчик пакета, вызвавшего проверку PIN-кода
		pcHandlePacket(mapGetByteValue(traPCPacketIDSave), buf, PCState, RSState); // запускаем обработку пакета, который вызвал проверку PIN-кода
	}
	else
	{
		tDisplayState ds;
		dspGetState(&ds);
		*PCState = lssProtocolError; // если прислан другой ответ
		switch (Response)
		{
			case crIncorrectPIN:
			{
				byte MaxPINAttempts = pcGetMaxPINAttempts();
				mapIncByte(traPINAttempts, 1);
				byte PINAttempts = mapGetByteValue(traPINAttempts);
				//mapPutByteValue(traPINAttempts, PINAttempts)
				displayLS8859(3, "Попытка %d из %d", PINAttempts, MaxPINAttempts);
				if (PINAttempts < MaxPINAttempts)
					*PCState = lssEnterPIN; // если лимит не исчерпан, то пытаемся снова
				else
				{
					*PCState = lssIncorrectPIN; // превышено количество попыток ввода пин-кода
					PrintRefusal8859("Превышено максимальное\nколичество попыток\nввода PIN кода.\nТранзакция отклонена.\n");
				}
				usrInfo(infIncorrectPIN);
			}
				break;
			default: // остальные ответы нас пока не интересуют
				break;

		}
		dspSetState(&ds);
	}
}

/*static void pcHandleResponse(tBuffer *buf, byte *PCState, byte *RSState)
 {
 pcParse(buf); // парсим сообщение от сервера

 // печатаем чек ответа, если он есть
 word ReceiptLayout = mapGetWordValue(traReceiptLayout);
 short int ReceiptsCount = mapGetWordValue(traReceiptsCount);
 if (ReceiptLayout != 0 && !dsIsFree(dslReceipt))
 PrintReceipt(ReceiptLayout, ReceiptsCount);

 // обычная обработка ответа
 byte Response = mapGetByteValue(traResponse);
 if (Response == crSuccess)                  // АП??: может быть HoldConnection или вообще не трогать? АП: не трогаем теперь. Было lssPrepareForWaitData
 {
 if (mapGetByteValue(traQueryID) == msConfirmation) // если это ответ на запрос подтверждения транзакции
 {
 // заново вызываем обработчик пакета, вызвавшего проверку PIN-кода
 byte PacketIDSave = mapGetByteValue(traPCPacketIDSave); // ID пакета, который вызвал проверку PIN-кода
 if (PacketIDSave != msResponse)
 pcHandlePacket(PacketIDSave, buf, PCState, RSState);
 else
 printS("pcHandleResponse alert: invalid PCPacketIDForConfirm\n");
 }
 else
 *PCState = mapGetByteValue(traIsLastPackage) == 0 ? *PCState : lssQueryForDisconnect; // если сервер хочет закрыть соедиенние после обработки, отсоединяемся
 }
 else
 {
 tDisplayState ds;
 dspGetState(&ds);
 *PCState = lssProtocolError;
 switch (Response)
 {
 case crIncorrectPIN:
 mapIncByte(traPINAttempts, 1);
 byte PINAttempts = mapGetByteValue(traPINAttempts);
 displayLS8859(3, "Попытка %d из %d", PINAttempts, MaxPINAttempts);
 usrInfo(infIncorrectPIN);
 if (PINAttempts < MaxPINAttempts) *PCState = lssEnterPIN; // если лимит не исчерпан, то пытаемся снова
 break;
 // остальные нас пока не интересуют
 }
 dspSetState(&ds);
 }
 }*/

// обработка сообщения "Ответ на запрос авторизации терминала"
static void pcHandleAnswerTerminalAuth(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	// печатаем чек ответа, если он есть
	if (!dsIsFree(dslReceipt))
		PrintReceipt(rloShiftOpen, 1);

	// mapPutByte(traResponse, 0); отключено, т.к. р-т присылается в ответном сообщении. Пока не используется, т.е. только Успех или Неудача, но в дальнейшем могут быть более сложные случаи при открытии смены

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // если сервер хочет закрыть соедиенние после обработки, отсоединяемся
}

// обработка сообщения "Ответ на запрос списка автономных товаров"
static void pcHandleAnswerAutonomousGoods(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим стандартные теги сообщения от сервера
	autParseGoods(buf); // парсим сообщение от сервера, извлекаем из него присланные товары

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // если сервер хочет закрыть соедиенние после обработки, отсоединяемся
}

// обработка сообщения "Набор ключей шифрования"
static void pcHandleAnswerKeySet(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим стандартные теги сообщения от сервера
	cphParseInfo(buf); // парсим сообщение от сервера, извлекаем из него присланный набор ключей шифрования

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // если сервер хочет закрыть соедиенние после обработки, отсоединяемся
}

// обработка сообщения "Логотип"
static void pcHandleAnswerLogotype(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим стандартные теги сообщения от сервера
	opsLogoParse(buf); // парсим сообщение от сервера, извлекаем из него присланный логотип

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // если сервер хочет закрыть соедиенние после обработки, отсоединяемся
}

// уведомление о пополнении счёта клиента
static void pcHandleAnswerRefillAccount(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloRefillAccount, 1); // печатаем чек ПОПОЛНЕНИЕ СЧЁТА
		// передаём сообщение для ККМ, если оно есть
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* если мы не соединены с ККМ или это последний пакет в обмене с ПЦ - закрываем соединение*/
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerGetCardActions(tBuffer *buf, byte *PCState, byte *RSState)
{
	calistAllocate();
	pcParse(buf); // парсим сообщение от сервера

	if (!pcIsPINVerificationRequired())
	{
		mapPutByte(traResponse, crSuccess);

		// передаём сообщение для ККМ, если оно есть
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* если мы не соединены с ККМ или это последний пакет в обмене с ПЦ - закрываем соединение*/
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerEnableAction(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	if (!pcIsPINVerificationRequired())
	{
		mapPutByte(traResponse, crSuccess);

		PrintReceipt(rloCardAction, -1); // печатаем чек УПРАВЛЕНИЕ АКЦИЯМИ

		// передаём сообщение для ККМ, если оно есть
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* если мы не соединены с ККМ или это последний пакет в обмене с ПЦ - закрываем соединение*/
	}
	else
		*PCState = lssEnterPIN;
}

// обработка сообщения "Дата и время"
static void pcHandleAnswerDateTime(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим стандартные теги сообщения от сервера
	*PCState = lssQueryForDisconnect;
}

// обработка ответа за "запрос копии чека"
static void pcHandleAnswerCopyOfReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	PrintReceipt(rloCopyOfReceipt, 1); // печатаем чек КОПИЯ ЧЕКА
	// передаём сообщение для ККМ
	*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
	*PCState = rsIsConnected() && !pcIsLastPackage() ? *PCState : lssQueryForDisconnect;
}

// обработка ответа за "запрос напоминания пин кода карты"
static void pcHandleAnswerRemindPIN(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	PrintReceipt(rloCardNotification, 1); // печатаем чек "Уведомление об отправке PIN кода"
	// передаём сообщение для ККМ
	*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // передаём пакет в СУ ККМ, если он есть
	*PCState = rsIsConnected() && !pcIsLastPackage() ? *PCState : lssQueryForDisconnect;
}

// обработка ответа за "запрос аварийной конфигурации"
static void pcHandleAnswerOfflineConfiguration(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // парсим сообщение от сервера

	// анализируем флаги контроля передачи
	// рвём соедиение если (используется контроль и передан флаг последнего пакета) ИЛИ (не используется контроль передачи)
	// в остальных случаях - отправляем запрос на получение следующего объёма данных
	byte TCFlags = mapGetByteValue(traCardRangesTCFlags);
	Bool CardRangesLast = (HAS_FLAG(TCFlags, pctfUseControl) && HAS_FLAG(TCFlags, pctfLast)) || (!HAS_FLAG(TCFlags, pctfUseControl)) || (!TagCardRangesListReceived);

	TCFlags = mapGetByteValue(traPursesTCFlags);
	Bool PursesLast = (HAS_FLAG(TCFlags, pctfUseControl) && HAS_FLAG(TCFlags, pctfLast)) || (!HAS_FLAG(TCFlags, pctfUseControl)) || (!TagPursesListReceived);

	TCFlags = mapGetByteValue(traGoodsToProductsTCFlags);
	Bool GoodsToPursesLast = (HAS_FLAG(TCFlags, pctfUseControl) && HAS_FLAG(TCFlags, pctfLast)) || (!HAS_FLAG(TCFlags, pctfUseControl)) || (!TagGoodsToProductsListReceived);

	TCFlags = mapGetByteValue(traProductGroupsTCFlags);
	Bool ProductGroupsLast = (HAS_FLAG(TCFlags, pctfUseControl) && HAS_FLAG(TCFlags, pctfLast)) || (!HAS_FLAG(TCFlags, pctfUseControl)) || (!TagProductGroupsListReceived);

	TCFlags = mapGetByteValue(traPCPursesRecodeTCFlags);
	Bool PCPursesRecodeLast = (HAS_FLAG(TCFlags, pctfUseControl) && HAS_FLAG(TCFlags, pctfLast)) || (!HAS_FLAG(TCFlags, pctfUseControl)) || (!TagPCPursesRecodeListReceived);

	Bool IsFinished = CardRangesLast && PursesLast && GoodsToPursesLast && ProductGroupsLast && PCPursesRecodeLast;

	if (IsFinished)
		mapPutByte(traResponse, crSuccess); // успешное завершение, если оно вообще было

	*PCState = IsFinished ? lssQueryForDisconnect : lssStartQueryOfflineConfiguration;
	// пытаемся разорвать соединение с ПЦ (оно не будет разорвано, если установлен режим постоянного соединения с ПЦ)
}

static void pcHandleAnswerOfflineTransaction(tBuffer *buf, byte *PCState, byte *RSState, byte *ItemState)
{
	pcParse(buf);
	byte Response = mapGetByteValue(traResponse), TransactionType = mapGetByteValue(traType);

	switch (TransactionType)
	{
		case ttDebit:
			PrintReceipt(/*pcIsTransitTransaction() ? rloPurchaseTransit :*/rloPurchase, 1); // печатаем чек покупки, пришедший с терминала
			break;
		case ttCancellation:
			PrintReceipt(/*pcIsTransitTransaction() ? rloRefundTransit :*/rloRefund, 1); // печатаем чек покупки, пришедший с терминала
			break;
		default:
			printS("Unknown type or archive transaction: %d\n", TransactionType);
			break;
	}

	if (Response != crSuccess) // если обработка транзакции завершилась с ошибками, то задаём вопрос - удалять её из архива или оставить для следующего раза?
	{
		tDisplayState state;
		dspGetState(&state);
		Beep(); Beep();
		dspClear();
		displayLS8859(CNTR(INV(0)), "ВНИМАНИЕ!");
		displayLS8859(1, "Обработка транзакции");
		displayLS8859(2, "завершена с ошибками");
		displayLS8859(3, "Удалить её из архива?");
		displayLS8859(INV(dspLastRow), "ЗЕЛ-да         КР-нет");
		*ItemState = kbdWait(0) == kbdVAL ? oisProcessed : oisError;
		dspSetState(&state);
	}
	else
		*ItemState = oisProcessed; // если ItemState == oisProcessed, то итем удаляется из архива
	*PCState = lssPrepareWaitForData;
}

void pcHandlePacket(byte PacketID, tBuffer *buf, byte *PCState, byte *RSState)
{
	switch (PacketID)
	{
		case pcpCardInfo:
			pcHandleAnswerCardInfo(buf, PCState, RSState);
			break;
		case pcpLiteCardInfo:
			pcHandleAnswerLiteCardInfo(buf, PCState, RSState);
			break;
		case pcpChangeReceipt:
			pcHandleAnswerChangeReceipt(buf, PCState, RSState);
			break;
		case pcpCloseReceipt:
			pcHandleAnswerCloseReceipt(buf, PCState, RSState);
			break; // если обработали сообщение без ошибок, отсоединяемся от всех точек
		case pcpCancellation:
			pcHandleAnswerCancellation(buf, PCState, RSState);
			break;
		case pcpConnectionFromExternalApplication:
			pcHandleAnswerConnectionFromPetrol(buf, PCState, RSState);
			break;
			//case mfsAnswerCancellationWithoutRS: pcHandleAnswerRefundWithoutRS(buf, PCState, RSState); break;
		case pcpValidation:
			pcHandleAnswerValidation(buf, PCState, RSState);
			break;
		case pcpAcceptQuest:
			pcHandleAnswerAcceptQuest(buf, PCState, RSState);
			break;
			//case msResponse: pcHandleResponse(buf, PCState, RSState); break;
		case pcpCardActionsList:
			pcHandleAnswerGetCardActions(buf, PCState, RSState);
			break;
		case pcpEnableAction:
			pcHandleAnswerEnableAction(buf, PCState, RSState);
			break;
		case pcpConfirmation:
			pcHandleConfirmation(buf, PCState, RSState);
			break;
		case pcpTerminalAuth:
			pcHandleAnswerTerminalAuth(buf, PCState, RSState);
			break;
		case pcpFileList:
			pcHandleAnswerFileList(buf, PCState, RSState);
			break;
		case pcpNextPartOfFile:
			pcHandleAnswerNextPart(buf, PCState, RSState);
			break;
		case pcpAutonomousGoods:
			pcHandleAnswerAutonomousGoods(buf, PCState, RSState);
			break;
		case pcpKeySet:
			pcHandleAnswerKeySet(buf, PCState, RSState);
			break;
		case pcpLogotype:
			pcHandleAnswerLogotype(buf, PCState, RSState);
			break;
		case pcpRefillAccount:
			pcHandleAnswerRefillAccount(buf, PCState, RSState);
			break;
		case pcpDateTime:
			pcHandleAnswerDateTime(buf, PCState, RSState);
			break;
		case pcpCopyOfReceipt:
			pcHandleAnswerCopyOfReceipt(buf, PCState, RSState);
			break;
		case pcpRemindPIN:
			pcHandleAnswerRemindPIN(buf, PCState, RSState);
			break;
		case pcpOfflineConfiguration:
			pcHandleAnswerOfflineConfiguration(buf, PCState, RSState);
			break;
		case pcpErrorMessage:
			pcHandleMessageError(buf, PCState, RSState);
			break;
		default:
			printS("PC Alert: unknown PacketID: %d\n", PacketID);
			*PCState = lssQueryForDisconnect;
			*RSState = lssQueryForDisconnect;
			break;
	}
}

// SendArchiveStates
typedef enum
{
	sasError = -1, sasSendTansaction = 2, sasFinish = 3, sasReceiveAnswer = 4
} SendArchiveStates;

typedef Bool (*tSuccessfulSendMethod)(byte *searchID);

// ===================================== отправка архивных данных (offline транзакций и банковских операций) ===============================
// Метод ssm вызывается после успешной передачи итема. В качестве параметра передаётся ID для поиска переданного итема
// Возвращает успешное ли было соединение с ПЦ
static Bool pcSendArchiveDataFile(char *FileName, tSuccessfulSendMethod ssm)
{
	card ArchiveTimeout = mapGetWordValue(cfgComPCTimeout) * 100;

	comSwitchToSlot(cslotPC);
	Bool IsConnected = comIsUsed(), Result = FALSE; // определяем, соединились ли мы с сервером
	byte PCState, RSState, ItemState;
	Result = IsConnected ? TRUE : opsConnectToPC(FALSE); // соединяемся с сервером ПЦ, если нужно
	if (Result)
	{
		tDataFile df;
		Result = dfInit(&df, FileName) && dfOpen(&df); // открываем архив
		if (Result)
		{
			card CurrentItem = 0, ItemsCount = dfCount(&df);
			if (ItemsCount > 0)
			{
				Result = dfFindFirst(&df);
				tDataItemHeader Item;
				while (dfFindNext(&df, &Item, TRUE)) // ищем все записи в архиве
				{
					SendArchiveStates ArchiveState = sasSendTansaction;

					byte *ItemData = memAllocate(Item.Size);
					if (dfReadItemData(&df, &Item, ItemData)) // читаем архивную запись из файла - в записи у нас подготовленный пакет для отправки на сервер ПЦ
					{
						CurrentItem++;
						displayLS8859(4, "%d из %d", CurrentItem, ItemsCount); // X из Y
						tBuffer sndbuf;
						tlvBufferInit(&sndbuf, ItemData, Item.Size); // инициализация буфера без обнуления данных

						ItemState = oisUnknown;
						tmrRestart(tsArchive, ArchiveTimeout); // ArchiveTimeout секунд на обработку одной транзакции
						while (ArchiveState != sasError && ArchiveState != sasFinish)
						{
							switch (ArchiveState)
							{
								case sasSendTansaction:
									traReset(); // очищаем текущую транзакцию перед отправкой данных
									pcParse(&sndbuf); // здесь нужно распарсить текущую транзакцию и засунуть её в tra
									Result = opsSendMessage(cslotPC, sndbuf.ptr, sndbuf.pos); // отправляем сообщение на сервер
									ArchiveState = sasReceiveAnswer;
									break;
								case sasReceiveAnswer:
								{
									card DataSize;
									byte ControlCharacter;
									byte PackageID = 0;
									byte *Data = opsReceiveMessage(cslotPC, &DataSize, &ControlCharacter); // ALLOCATE MEMORY
									switch (ControlCharacter)
									{
										case pccSTX:
											if (Data != NULL)
											{
												tBuffer buf;
												tlvBufferInit(&buf, Data, DataSize);
												card len = 0;
												if (tlvGetValue(&buf, srtPackageID, &len, &PackageID)) // tag 01 ALWAYS contains 1 byte
												{
													switch (PackageID)
													{
														case pcpOfflineTransaction:
															pcHandleAnswerOfflineTransaction(&buf, &PCState, &RSState, &ItemState); // анализируем полученный ответ
															Result = PCState == lssPrepareWaitForData;
															if (Result && ssm != NULL)
																Result = ssm(Item.SearchID);
															tmrRestart(tsArchive, ArchiveTimeout);
															ArchiveState = sasFinish;
															break;
														case pcpErrorMessage: // неконтролируемая ошибка
															pcHandleMessageError(&buf, &PCState, &RSState);
															Result = FALSE;
															ArchiveState = sasError;
															break;
														default: // неожиданная ошибка
															printS("Unexpected PackageID: %d\n", PackageID);
															Result = FALSE;
															ArchiveState = sasError;
															break;
													}
												}
												else
													usrInfo(infTag01NotFound);
											}
											break;
										default:
											if (tmrGet(tsArchive) <= 0) // timeout
												Result = FALSE;
											break;
									}
									Data = memFree(Data); // FREE MEMORY OF MESSAGE
								}
								case sasError:
								case sasFinish:
								default:
									break;
							}

						}
						if (ItemState == oisProcessed) // если итем обработан (успешно или не успешно, это другой вопрос)
							dfDeleteItem(&df, &Item); // помечаем запись как удалённую
						tmrStop(tsArchive);
					}
					ItemData = memFree(ItemData);
				}
			}
		}
		Result &= dfClose(&df); // Закрывается в любом случае
	}
	else
		usrInfo(infErrorSendingArchiveData); // Ошибка при передаче архива. При этом остаются только те записи которые не были переданы

	if (!IsConnected && comIsUsed()) // если мы не были соединены с сервером в начале функции, но при этом соединены с ним сейчас,
		pcDisconnectFromServer(); // то отсоединяемся

	//comSwitchToSavedSlot();
	return Result;
}

#ifdef OFFLINE_ALLOWED
// удаляет дополнительные данные после передачи offline транзакции
static Bool DeleteOfflineTransactionData(byte *searchID)
{
	Bool ResultInitials = oftDeleteFromArchiveBySearchID(FILENAME_OFFLINE_INITIALS, searchID);

	oftDeleteFromArchiveBySearchID(FILENAME_OFFLINE_LIMITS, searchID); // лимиты могут быть, а могут не быть по offline-транзакции (поэтому не контролируем)

	if (!ResultInitials)
		printS("ALERT! Integrity violation. Can't find initial transaction with id: %s\n", searchID);

	return ResultInitials;
}

void pcSendArchiveTransactions(byte *PCState, byte *RSState)
{
	tDisplayState ds;
	dspGetState(&ds);

	displayLS8859(3, "Аварийные: банковские");
	Bool Result = pcSendArchiveDataFile(FILENAME_BANKING, NULL);
	if (Result)
	{
		dspSetState(&ds);
		displayLS8859(3, "Аварийные: транзакции");
		pcSendArchiveDataFile(FILENAME_OFFLINE_TRANSACTIONS, DeleteOfflineTransactionData);
	}
	dspSetState(&ds);
	*PCState = lssQueryForDisconnect; // завершаем соединение после отправки Offline-транзакций
}
#endif

// true, если терминал находится в состоянии offline (аварийный режим или голосовая авторизация) по отношению к ПЦ
Bool pcInOffline()
{
	#ifdef OFFLINE_ALLOWED
		return mapGetByteValue(regPCCurrentMode) != pcmOnline;
	#else
		return FALSE;
	#endif
}

