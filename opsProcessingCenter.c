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
 @brief ���������� ���������������� �����, ������������ ��� ����������� � �������
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
 @brief ���������� ��������� ����������������� ������, �������������� ��� ��������� �������
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
 @brief ������������� ���������������� ����� � ��������� ����������� � �������
 � ChannelError ���������� ������ ������, ������� ����� ������������� � �����
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
						// �������� ��� ��������� �� ������ ����������������� ������ � ����������� � ��������� ��� �������� Session Key
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

// ��������� ���������� � �� � ������� BEL � ���������� TRUE, ���� �� ������� ���������� � ��
Bool pcCheckConnection()
{
	trcS("pcCheckConnection");
	comSwitchToSlot(cslotPC);

	if (!pcIsConnected())
		return FALSE; // ����� �� ���������, ���� �� �� ���������

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

	pcDisconnectFromServer(); // ������������� ������� ����, ��� �� �� ��������� � ��������
	return FALSE;
}

// ���������� TRUE, ���� �������� ��������� � ������ ����������� ����������� ���������� � ��
Bool pcShouldPermanentlyConnected()
{
	return mapGetByteValue(cfgComPCPermanentConnection) != 0;
}

// =========================================== ������ �� ������������ ������ ============================================================================

static void pcAppendShiftAuthUD(tlvWriter *tw)
{
	byte buffer[lenShiftAuthID];
	mapGet(regShiftAuthID, buffer, lenShiftAuthID);
	if (!arrIsEmpty(buffer, lenShiftAuthID))
		tlvwAppend(tw, srtShiftAuthID, sizeof(buffer), buffer); // shift auth ID
}

// �������� � ����� ��� �������������� ����������� ����
Bool pcAppendStandardTags(tlvWriter *tw)
{
	card SendedTagsFlag = mapGetCardValue(traSendedTagsFlag);

	// ���� ID ��������� �� ��� �� ����������, ������ � ����� � ������������� ���� ����, ��� �� ��� ��� ����������
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

	if (!HAS_FLAG(SendedTagsFlag, stfCardNumber) && !traCardNumberIsEmpty()) // ���� ����� ����� �� ����������� ����� � ����� ����� �� ������
	{
		char CardNumber[lenCardNumber], BCDCardNumber[lenCardNumber];
		mapGet(traCardNumber, CardNumber, lenCardNumber);
		int lenBCDCardNumber = StringToBCD(CardNumber, BCDCardNumber);
		tlvwAppend(tw, srtCardID, lenBCDCardNumber, BCDCardNumber); // card number
		byte CardReadingMethod;
		mapGetByte(traCardReadingMethod, CardReadingMethod);
		if (CardReadingMethod > 0)
			tlvwAppendByte(tw, srtCardReadingMethod, CardReadingMethod);

		if (ocdCanStore()) // ���� �� ����� ����� ������� ��������� ����������, �� ������ �� ������ ������ � � ����������
		{
			tlvWriter cdtw; tlvwInit(&cdtw); // ������ �� ���������� offline-������ �����
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
			tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // ����� �� �� ���

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		//Message = memFree(Message);
		tlvwFree(&tw);
		dsFree(dslRSPackage);
	}
#ifdef OFFLINE_ALLOWED
	else
		switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryCardInfo(IsLastPackage, SendRSPackage, PCState, RSState); break; // ��� ���������� ������ offline
			case ofpVoiceAuthorization: PrintRefusal8859("�� ��������������\n� ������ ���������\n�����������\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("����������� ��� offline\n"); *PCState = lssDisconnecting; break;
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
		tlvwAppend(&tw, srtRetailSystemPackage, dsGetDataSize(dslRSPackage), dsGetData(dslRSPackage)); // ����� �� �� ���

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		//Message = memFree(Message);
		tlvwFree(&tw);
		dsFree(dslRSPackage);
	}
#ifdef OFFLINE_ALLOWED
	else
	{
		PrintRefusal8859("�� ����������� � ������ offline �� ������� ������\n"); *PCState = lssDisconnecting;
	}

		/*switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryCardInfo(IsLastPackage, SendRSPackage, PCState, RSState); break; // ��� ���������� ������ offline
			case ofpVoiceAuthorization: PrintRefusal8859("�� ��������������\n� ������ ���������\n�����������\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("����������� ��� offline\n"); *PCState = lssDisconnecting; break;
		}*/
#endif
}

// ������ �� ���������� �� �������� ����������
void pcSendQueryConnectionFromExternalApp(byte *PCState, byte *RSState)
{
	tlvWriter tw; tlvwInit(&tw);
	// build message
	tlvwAppendByte(&tw, srtPackageID, pcpConnectionFromExternalApplication); // packet ID
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

/*���������� �� ������������� ������������ ��������� ����� ���
 � ����� ��� ���������� ���� "��� �����", ����� �� ���� �������� ���-����
 ��������� �� ������� ���������� � ������ �� ���� � ���������*/

// ������ �� ������ ������� ��� ���
void pcSendQueryCancellationWithoutRS(byte *PCState, byte *RSState)
{
	// ��������� ����� ��� �������� ����������� ��������
	card MessageSize = dsGetDataSize(dslRSPackage);
	byte *Message = memAllocate(MessageSize);
	tBuffer buf;
	bufInit(&buf, Message, MessageSize);
	memcpy(Message, dsGetData(dslRSPackage), MessageSize); // �������� ���������� ��������� � �����
	dsFree(dslRSPackage); // ����������� ����

	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // �.�. ������� ��� ���, �� ����� �������� ���������� � �� ����� ��������� ������

	rsHandleQueryCancellation(&buf, PCState, RSState);

	Message = memFree(Message);
}

// �������� ������� �� �������� �����
void pcSendQueryStartValidation(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID);

	// ����� ��������� Flags � TerminalID
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

// �������� ������� �� ����������� ���������
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
	// ���� ��������� �� � ���� ���������
	if (InstallDate != 0) tlvwAppendCard(&ditw, ditApplicationInstallationDate, InstallDate);
	if (ActivationDate != 0) tlvwAppendCard(&ditw, ditApplicationActivationDate, ActivationDate);
	// ��������� � ��� ���������
	if (SystemFioctl(SYS_FIOCTL_GET_TERMINAL_TYPE, &TerminalType) == 0)
	{
		tlvwAppendByte(&ditw, ditPlatformType, 1); // 1 - Telium platform
		tlvwAppendCard(&ditw, ditTerminalType, TerminalType);
	}
	// ���� ������������ ���������
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

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // ����� ��������� Flags, TerminalID, DateTime
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
	/*if (!IsEmptyATR(dsGetData(dslUnknownATR), dsGetDataSize(dslUnknownATR))) // ���������� ATR ������ ���� �� �� ������
	{
	������ ��� ����������� ������� � �������� �� ������������� ����������
	}
	else
		*PCState = lssQueryForDisconnect;*/
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // ����� ��������� Flags, TerminalID
	tlvWriter tw; tlvwInit(&tw);

	mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsLastPackage); // Is last package

	tlvwAppendByte(&tw, srtPackageID, pcpUnknownATR);
	pcAppendStandardTags(&tw);
	tlvwAppend(&tw, srtATR, dsGetDataSize(dslUnknownATR), dsGetData(dslUnknownATR));
	dsFree(dslUnknownATR);
	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssQueryForDisconnect : lssProtocolError;
	tlvwFree(&tw);

	mapPutCard(traSendedTagsFlag, 0); // ���� ����� ������ �� ���������, �� �� ��������������� ����� ��������
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
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // ����� ��������� Flags, TerminalID
	// build message
	tlvWriter tw; tlvwInit(&tw);

	tlvwAppendByte(&tw, srtPackageID, pcpFileList);
	pcAppendStandardTags(&tw);
	if (FolderName && strlen(FolderName) > 0) // FolderName - ��� �������� �� �������, ��� ����� ������ �����
		tlvwAppendString(&tw, srtRootFolder, FolderName);
	//tlvwAppendCard(&tw, srtVersion, PROGRAM_BUILD);

	tlvWriter ditw; tlvwInit(&ditw);
	tlvwAppendCard(&ditw, ditApplicationVersion, PROGRAM_BUILD); // ���������� ���� ���������� � ������� �� ���������� ��/������������
	tlvwAppend(&tw, srtDeviceInfo, ditw.DataSize, ditw.Data); // device info
	tlvwFree(&ditw);


	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// ������� �������������� ��������� �� OPS: center
void pcSendPeriodic(byte *PCState, byte *RSState)
{
	// prepare
	if (mapGetCardValue(cfgTrmTerminalID) == 0)
	{
		*PCState = lssQueryForDisconnect;
		return; // ���� �������� �� ��������, �� ������ �� �������, � ������ �������
	}

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // ����� ��������� Flags, TerminalID, DateTime
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
		//tlvwAppendByte(&tw, srtIsLastPackage, 1); �������� �� �����
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
			case ofpOffline: oflrsHandleCloseReceipt(&buf, PCState, RSState); break; // ��� ���������� ������ offline
			case ofpVoiceAuthorization: vaHandleCloseReceipt(&buf, PCState, RSState); break;
			default: PrintRefusal8859("����������� ��� offline\n"); break;
		}
	}
#endif
}

// get list of autonomous goods
void pcSendQueryAutonomousGoods(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // ����� ��������� Flags, TerminalID, DateTime
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

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // ����� ��������� Flags, TerminalID
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
		tlvwAppendByte(&tw, srtPINMethod, 1); // PIN ���� ���������� ������ � ������� ISO9564 format 0

		*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
		tlvwFree(&tw);
	}
	#ifdef OFFLINE_ALLOWED
	else
		switch (oflGetPossibility())
		{
			case ofpOffline: oflpcSendQueryConfirmation(PCState, RSState); break; // ��� ���������� ������ offline
			case ofpVoiceAuthorization: PrintRefusal8859("�� ��������������\n� ������ ���������\n�����������\n"); *PCState = lssDisconnecting; break;
			default: PrintRefusal8859("����������� ��� offline\n"); *PCState = lssDisconnecting; break;
		}
	#endif

}

void pcSendQueryLogotype(byte *PCState, byte *RSState)
{
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // ��������� ��� ����� ������������ ����������� ����� ����� ������ � ID ���������

	tlvWriter tw; tlvwInit(&tw);
	tlvwAppendByte(&tw, srtPackageID, pcpLogotype); // packet ID
	pcAppendStandardTags(&tw);

	*PCState = opsSendMessage(cslotPC, tw.Data, tw.DataSize) ? lssPrepareWaitForData : lssProtocolError;
	tlvwFree(&tw);
}

// send to server query for get card info
void pcSendQueryRefillAccount(Bool IsLastPackage, Bool SendRSPackage, byte *PCState, byte *RSState)
{
	if (!SendRSPackage) // ���� ���� ������ �� ��� - ������ ���� �����
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
	// �������������� ������� ������ ����� (���� ���) � ������ �����
	calistAllocate();

	if (!SendRSPackage) // ���� ���� ������ �� ��� - ������ ���� �����
	{
		tlvWriter rstw; tlvwInit(&rstw);

		tlvwAppendByte(&rstw, rstPackageID, rspCardActionsList); // refill account

		tlvWriter catw; tlvwInit(&catw);
		tlvwAppendByte(&catw, catState, mapGetByteValue(traCardActionState)); // ������
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
	if (!SendRSPackage) // ���� ���� ������ �� ��� - ������ ���� �����
	{
		tlvWriter rstw; tlvwInit(&rstw);

		tlvwAppendByte(&rstw, rstPackageID, rspEnableAction); // refill account

		tlvWriter catw; tlvwInit(&catw);
		tlvwAppendCard(&catw, catID, mapGetCardValue(traCardActionID)); // ID ������������ ��� �����������
		tlvwAppendByte(&catw, catState, mapGetByteValue(traCardActionState)); // ������� �� ��������� ��������� ����� (0 - ����������, 1 - ���������)
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
	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID); // ��������� ��� ����� ������������ ����������� ����� ����� ������ � ID ���������
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
	if (!SendRSPackage) // ���� ���� ������ �� ��� - ������ ���� �����
	{
		tlvWriter rstw; tlvwInit(&rstw);

		char TraID[lenID];
		mapGet(traPurchaseID, TraID, lenID);

		tlvwAppendByte(&rstw, rstPackageID, rspQueryCopyOfReceipt); // get a copy...
		tlvwAppendBCDString(&rstw, rstPurchaseID, TraID); // for this receipt

		dsAllocate(dslRSPackage, rstw.Data, rstw.DataSize);
		tlvwFree(&rstw);
	}

	mapPutCard(traSendedTagsFlag, 0xFFFFFFFF & ~stfFlags & ~stfTerminalID & ~stfDateTime); // ����� ��������� Flags, TerminalID � DateTime

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

// true, ���� ������ ���������� - ����������
Bool pcIsTransitTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsTransitTransaction);
}

// true, ���� ������ ���������� - offline ���������
Bool pcIsOfflineTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsOfflineTransaction);
}

// true, ���� ������ ���������� - offline ��������� �����������
Bool pcIsVoiceAuthTransaction()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsVoiceAuthTransaction);
}

// true, ���� ������� ����� �������� ��������� � ������
Bool pcIsLastPackage()
{
	return HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage);
}

// ������ ��������� TransmissionControl, �������� �����. ������ � ��. ���� ���� �� = 0, �� ������ �� ������������
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
		// �� 1 ��� �� �������� �������� - �� ��� ������� ������
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

// ��������� ���������� ��������� ���������
static void ParseTerminalEnvironment(byte *Buffer, card BufferSize)
{
	tlvReader tr;
	tlvrInit(&tr, Buffer, BufferSize);
	byte tag;
	card len;
	byte *value;

	card CurrentPCOwnerID = mapGetCardValue(trmePCOwnerID); // ��������� �������� ��������� ��
	// ������� TerminalConfig ����� ��������� ����� ������������
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
		// �� 1 ��� �� �������� �������� - �� ��� ������� ������
		{
			case tetCRC: mapPutCard(trmeCRC, tlv2card(value)); break;
			case tetPCOwnerID:
			{
				card ReceivedPCOwnerID = tlv2card(value);
				// ���� � ��� �������� ��� ��� �������� �� ������-�� ��������� �� � �� ����� �������� - ��� ������!
				// ���� �������� �� ���� � ������������� ��������.
				if (CurrentPCOwnerID != 0 && CurrentPCOwnerID != ReceivedPCOwnerID)
				{
					mapPutByte(traResponse, crSecurityAlert);
					PrintRefusal8859("������ ������������!\n��������� ���������\n��������� ��. ��������\n����� ������������...\n");
					ttestall(PRINTER, 0); // ��� ���� ������� �����������
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
	mapPutByte(trmeTagReceived, 1); // ������ ������� ����, ��� �� ������� ��� � ���������� ���������
	mapSave(trmeBeg); // ��������� ������������ �������� TerminalEnvironment
}

// ��������� ���������� ��������� � ������� �� �������� ��������� ���������
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
		// �� 1 ��� �� �������� �������� - �� ��� ������� ������
		{
			case aitCode:
			{
				card Code = tlv2card(value);
				if (!actTryActivate(utGetDateTime(), Code))
					tmrPause(1); // ���� ��������� ���� ��������� - ������ ����� � ������� ��� �������������� ����-�����
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
		// �� 1 ��� �� �������� �������� - �� ��� ������� ������
		{
			/*case srtQueryID:
			 mapPutByte(traQueryID, value[0]);
			 break;*/
			case srtResponse:
				mapPutByte(traResponse, value[0]);
				// ����� ������� ������� (0 - �������� ����������, ����� ��� ������)
				break;
			case srtTerminalDateTime:
			{
				card Time = tlv2card(value);
				mapPutCard(traDateTimeUnix, Time); // ���� � ����� ��������� (��� ������������� ��� ����� ����)

				char sDateTime[lenDatTim];
				utToUTA(Time, sDateTime);
				mapPut(traDateTimeASCII, sDateTime, lenDatTim);
			}
				break;
			case srtTransactionType:
				mapPutByte(traType, value[0]);
				// ��� ������� ����������
				break;
			case srtTerminalPrinter: // message
				if (len != 0)
				{
					value[len] = 0;
					dsAllocate(dslReceipt, value, len + 1); // � �������� �������� ��������� ��� ����
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
				dsAllocate(dslRSPackage, value, len); // � �������� �������� ����� ��� ������� ����������
				tBuffer rsBuf;
				tlvBufferInit(&rsBuf, value, len);
				rsParse(&rsBuf); // ������ ����� ��� ���
				break;
			/*case srtIsLastPackage:
				mapPutByte(traIsLastPackage, value[0]);
				// 1, ���� ������ ��������� �� ������������ ������� ��������� ����� ��������� ������
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
	// ���������, ��������� �� ��� ����� � ��������

	//if (pcIsTransitTransaction()) // ���� �������*/

	return TRUE;
}

static void pcHandleMessageError(tBuffer *buffer, byte *PCState, byte *RSState)
{
	pcParse(buffer);

	// ������ ���� ������
	PrintRefusal(NULL);
	usrInfo(infServerError);

	dsFree(dslReceipt); // ����������� ������

	// ������� ����� � ������� ����������.
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

		if (!dsIsFree(dslRSPackage) && !pcIsLastPackage()) // ���� ����� ��� ��� ���� � ��� �� ��������� ����� �� �������, �� �� �������
			*RSState = rssQueryForEstabilishConnection;
		else
			*PCState = lssQueryForDisconnect; // �������������, ���� ����� � �� �� ������ ��� ������ ������ ������� ���������� ����� ���������
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerLiteCardInfo(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		if (!dsIsFree(dslRSPackage) && !pcIsLastPackage()) // ���� ����� ��� ��� ���� � ��� �� ��������� ����� �� �������, �� �� �������
			*RSState = rssQueryForEstabilishConnection;
		else
			*PCState = lssQueryForDisconnect; // �������������, ���� ����� � �� �� ������ ��� ������ ������ ������� ���������� ����� ���������
	}
	else
		*PCState = lssEnterPIN;
}

// ����� �� ������ �� � ����������� ����
static void pcHandleAnswerChangeReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		// �������� - �������� ��� ������� ������� (���� �����)
		if (!dsIsFree(dslReceipt)) // �������� ���������, ���� ��� ����
		{
			qprintS(dsGetData(dslReceipt));
			rptReceipt(rloSkip);
			dsFree(dslReceipt);
			printWait();
		}

		// ������� ����� � ������� ����������. �� ���� ������ ���� ������
		*RSState = rsSendPackage() ? rssPrepareWaitForData : rssProtocolError;
	}
	else
		*PCState = lssEnterPIN;
}

// ����� �� ������ �� �� �������� ����
static void pcHandleAnswerCloseReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf);

	if (!pcIsPINVerificationRequired())
	{
		// �������� ��� ����������� �������
		PrintReceipt(rloPurchase, -1);

		// ������� ����� � ������� ����������. �� ���� ������ ���� ������
		*RSState = rsSendPackage() ? rssPrepareWaitForData : rssProtocolError;
		// ���� ��� ��������� ����� - ��������� ���������� � ��������
		*PCState = HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage) ? *PCState : lssQueryForDisconnect;
	}
	else
		*PCState = lssEnterPIN;
}

// ��������� ����� �� ������ �� � ���������� ��������
static void pcHandleAnswerCancellation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloRefund, -1); // �������� ��� ������ ����������� �������

		*RSState = rsIsConnected() ? ( rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � ������� ����������. �� ���� ������ ���� ������
		*PCState = (pcIsConnected() && !HAS_FLAG(mapGetWordValue(traPCFlags), pcfIsLastPackage)) ? *PCState : lssQueryForDisconnect;
	}
	else
		*PCState = lssEnterPIN;
}

// ����� �� ������� �� ������ �� � ���������� �������� ��� ������������ �����
static void pcHandleAnswerConnectionFromPetrol(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������
	if (!dsIsFree(dslRSPackage)) // ��������� ���� ��� ���� ������ ��� ���
		*RSState = rssQueryForEstabilishConnection;
	else
		*PCState = lssQueryForDisconnect; // ���� ����� � �� �� ������, ������������� �� �������
}

// �������� �� ������� ����� �� �����
static void pcHandleAnswerValidation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	// �������� ��� ����� �� �����
	if (mapGetByteValue(traIsIntermediateReport) == 1)
		PrintReceipt(rloIntermediateReport, 1);
	else
	{
		PrintReceipt(rloShiftReport, -1);
		mapPutByte(traResponse, crSuccess);
		// �������� �������� �����
	}

	*PCState = lssQueryForDisconnect;
}

// ����� �� ������ "������� ������"
static void pcHandleAnswerAcceptQuest(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloAcceptQuest, 1); // �������� ��� ������
		// ������� ��������� ��� ���, ���� ��� ����
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* ���� �� �� ��������� � ��� ��� ��� ��������� ����� � ������ � �� - ��������� ����������*/
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


// ��������� ��������� "�����"
static void pcHandleConfirmation(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	// ������� ��������� ������
	byte Response = mapGetByteValue(traResponse);
	if (Response == crSuccess) // ��??: ����� ���� HoldConnection ��� ������ �� �������? ��: �� ������� ������. ���� lssPrepareForWaitData
	{
		// ������ �������� ���������� ������, ���������� �������� PIN-����
		pcHandlePacket(mapGetByteValue(traPCPacketIDSave), buf, PCState, RSState); // ��������� ��������� ������, ������� ������ �������� PIN-����
	}
	else
	{
		tDisplayState ds;
		dspGetState(&ds);
		*PCState = lssProtocolError; // ���� ������� ������ �����
		switch (Response)
		{
			case crIncorrectPIN:
			{
				byte MaxPINAttempts = pcGetMaxPINAttempts();
				mapIncByte(traPINAttempts, 1);
				byte PINAttempts = mapGetByteValue(traPINAttempts);
				//mapPutByteValue(traPINAttempts, PINAttempts)
				displayLS8859(3, "������� %d �� %d", PINAttempts, MaxPINAttempts);
				if (PINAttempts < MaxPINAttempts)
					*PCState = lssEnterPIN; // ���� ����� �� ��������, �� �������� �����
				else
				{
					*PCState = lssIncorrectPIN; // ��������� ���������� ������� ����� ���-����
					PrintRefusal8859("��������� ������������\n���������� �������\n����� PIN ����.\n���������� ���������.\n");
				}
				usrInfo(infIncorrectPIN);
			}
				break;
			default: // ��������� ������ ��� ���� �� ����������
				break;

		}
		dspSetState(&ds);
	}
}

/*static void pcHandleResponse(tBuffer *buf, byte *PCState, byte *RSState)
 {
 pcParse(buf); // ������ ��������� �� �������

 // �������� ��� ������, ���� �� ����
 word ReceiptLayout = mapGetWordValue(traReceiptLayout);
 short int ReceiptsCount = mapGetWordValue(traReceiptsCount);
 if (ReceiptLayout != 0 && !dsIsFree(dslReceipt))
 PrintReceipt(ReceiptLayout, ReceiptsCount);

 // ������� ��������� ������
 byte Response = mapGetByteValue(traResponse);
 if (Response == crSuccess)                  // ��??: ����� ���� HoldConnection ��� ������ �� �������? ��: �� ������� ������. ���� lssPrepareForWaitData
 {
 if (mapGetByteValue(traQueryID) == msConfirmation) // ���� ��� ����� �� ������ ������������� ����������
 {
 // ������ �������� ���������� ������, ���������� �������� PIN-����
 byte PacketIDSave = mapGetByteValue(traPCPacketIDSave); // ID ������, ������� ������ �������� PIN-����
 if (PacketIDSave != msResponse)
 pcHandlePacket(PacketIDSave, buf, PCState, RSState);
 else
 printS("pcHandleResponse alert: invalid PCPacketIDForConfirm\n");
 }
 else
 *PCState = mapGetByteValue(traIsLastPackage) == 0 ? *PCState : lssQueryForDisconnect; // ���� ������ ����� ������� ���������� ����� ���������, �������������
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
 displayLS8859(3, "������� %d �� %d", PINAttempts, MaxPINAttempts);
 usrInfo(infIncorrectPIN);
 if (PINAttempts < MaxPINAttempts) *PCState = lssEnterPIN; // ���� ����� �� ��������, �� �������� �����
 break;
 // ��������� ��� ���� �� ����������
 }
 dspSetState(&ds);
 }
 }*/

// ��������� ��������� "����� �� ������ ����������� ���������"
static void pcHandleAnswerTerminalAuth(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	// �������� ��� ������, ���� �� ����
	if (!dsIsFree(dslReceipt))
		PrintReceipt(rloShiftOpen, 1);

	// mapPutByte(traResponse, 0); ���������, �.�. �-� ����������� � �������� ���������. ���� �� ������������, �.�. ������ ����� ��� �������, �� � ���������� ����� ���� ����� ������� ������ ��� �������� �����

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // ���� ������ ����� ������� ���������� ����� ���������, �������������
}

// ��������� ��������� "����� �� ������ ������ ���������� �������"
static void pcHandleAnswerAutonomousGoods(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ����������� ���� ��������� �� �������
	autParseGoods(buf); // ������ ��������� �� �������, ��������� �� ���� ���������� ������

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // ���� ������ ����� ������� ���������� ����� ���������, �������������
}

// ��������� ��������� "����� ������ ����������"
static void pcHandleAnswerKeySet(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ����������� ���� ��������� �� �������
	cphParseInfo(buf); // ������ ��������� �� �������, ��������� �� ���� ���������� ����� ������ ����������

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // ���� ������ ����� ������� ���������� ����� ���������, �������������
}

// ��������� ��������� "�������"
static void pcHandleAnswerLogotype(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ����������� ���� ��������� �� �������
	opsLogoParse(buf); // ������ ��������� �� �������, ��������� �� ���� ���������� �������

	*PCState = !pcIsLastPackage() ? *PCState : lssQueryForDisconnect; // ���� ������ ����� ������� ���������� ����� ���������, �������������
}

// ����������� � ���������� ����� �������
static void pcHandleAnswerRefillAccount(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	if (!pcIsPINVerificationRequired())
	{
		PrintReceipt(rloRefillAccount, 1); // �������� ��� ���������� �ר��
		// ������� ��������� ��� ���, ���� ��� ����
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* ���� �� �� ��������� � ��� ��� ��� ��������� ����� � ������ � �� - ��������� ����������*/
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerGetCardActions(tBuffer *buf, byte *PCState, byte *RSState)
{
	calistAllocate();
	pcParse(buf); // ������ ��������� �� �������

	if (!pcIsPINVerificationRequired())
	{
		mapPutByte(traResponse, crSuccess);

		// ������� ��������� ��� ���, ���� ��� ����
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* ���� �� �� ��������� � ��� ��� ��� ��������� ����� � ������ � �� - ��������� ����������*/
	}
	else
		*PCState = lssEnterPIN;
}

static void pcHandleAnswerEnableAction(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	if (!pcIsPINVerificationRequired())
	{
		mapPutByte(traResponse, crSuccess);

		PrintReceipt(rloCardAction, -1); // �������� ��� ���������� �������

		// ������� ��������� ��� ���, ���� ��� ����
		*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
		*PCState = (rsIsConnected() && !pcIsLastPackage()) ? *PCState : lssQueryForDisconnect; /* ���� �� �� ��������� � ��� ��� ��� ��������� ����� � ������ � �� - ��������� ����������*/
	}
	else
		*PCState = lssEnterPIN;
}

// ��������� ��������� "���� � �����"
static void pcHandleAnswerDateTime(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ����������� ���� ��������� �� �������
	*PCState = lssQueryForDisconnect;
}

// ��������� ������ �� "������ ����� ����"
static void pcHandleAnswerCopyOfReceipt(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	PrintReceipt(rloCopyOfReceipt, 1); // �������� ��� ����� ����
	// ������� ��������� ��� ���
	*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
	*PCState = rsIsConnected() && !pcIsLastPackage() ? *PCState : lssQueryForDisconnect;
}

// ��������� ������ �� "������ ����������� ��� ���� �����"
static void pcHandleAnswerRemindPIN(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	PrintReceipt(rloCardNotification, 1); // �������� ��� "����������� �� �������� PIN ����"
	// ������� ��������� ��� ���
	*RSState = !dsIsFree(dslRSPackage) && rsIsConnected() ? (rsSendPackage() ? rssPrepareWaitForData : rssProtocolError) : *RSState; // ������� ����� � �� ���, ���� �� ����
	*PCState = rsIsConnected() && !pcIsLastPackage() ? *PCState : lssQueryForDisconnect;
}

// ��������� ������ �� "������ ��������� ������������"
static void pcHandleAnswerOfflineConfiguration(tBuffer *buf, byte *PCState, byte *RSState)
{
	pcParse(buf); // ������ ��������� �� �������

	// ����������� ����� �������� ��������
	// ��� ��������� ���� (������������ �������� � ������� ���� ���������� ������) ��� (�� ������������ �������� ��������)
	// � ��������� ������� - ���������� ������ �� ��������� ���������� ������ ������
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
		mapPutByte(traResponse, crSuccess); // �������� ����������, ���� ��� ������ ����

	*PCState = IsFinished ? lssQueryForDisconnect : lssStartQueryOfflineConfiguration;
	// �������� ��������� ���������� � �� (��� �� ����� ���������, ���� ���������� ����� ����������� ���������� � ��)
}

static void pcHandleAnswerOfflineTransaction(tBuffer *buf, byte *PCState, byte *RSState, byte *ItemState)
{
	pcParse(buf);
	byte Response = mapGetByteValue(traResponse), TransactionType = mapGetByteValue(traType);

	switch (TransactionType)
	{
		case ttDebit:
			PrintReceipt(/*pcIsTransitTransaction() ? rloPurchaseTransit :*/rloPurchase, 1); // �������� ��� �������, ��������� � ���������
			break;
		case ttCancellation:
			PrintReceipt(/*pcIsTransitTransaction() ? rloRefundTransit :*/rloRefund, 1); // �������� ��� �������, ��������� � ���������
			break;
		default:
			printS("Unknown type or archive transaction: %d\n", TransactionType);
			break;
	}

	if (Response != crSuccess) // ���� ��������� ���������� ����������� � ��������, �� ����� ������ - ������� � �� ������ ��� �������� ��� ���������� ����?
	{
		tDisplayState state;
		dspGetState(&state);
		Beep(); Beep();
		dspClear();
		displayLS8859(CNTR(INV(0)), "��������!");
		displayLS8859(1, "��������� ����������");
		displayLS8859(2, "��������� � ��������");
		displayLS8859(3, "������� � �� ������?");
		displayLS8859(INV(dspLastRow), "���-��         ��-���");
		*ItemState = kbdWait(0) == kbdVAL ? oisProcessed : oisError;
		dspSetState(&state);
	}
	else
		*ItemState = oisProcessed; // ���� ItemState == oisProcessed, �� ���� ��������� �� ������
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
			break; // ���� ���������� ��������� ��� ������, ������������� �� ���� �����
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

// ===================================== �������� �������� ������ (offline ���������� � ���������� ��������) ===============================
// ����� ssm ���������� ����� �������� �������� �����. � �������� ��������� ��������� ID ��� ������ ����������� �����
// ���������� �������� �� ���� ���������� � ��
static Bool pcSendArchiveDataFile(char *FileName, tSuccessfulSendMethod ssm)
{
	card ArchiveTimeout = mapGetWordValue(cfgComPCTimeout) * 100;

	comSwitchToSlot(cslotPC);
	Bool IsConnected = comIsUsed(), Result = FALSE; // ����������, ����������� �� �� � ��������
	byte PCState, RSState, ItemState;
	Result = IsConnected ? TRUE : opsConnectToPC(FALSE); // ����������� � �������� ��, ���� �����
	if (Result)
	{
		tDataFile df;
		Result = dfInit(&df, FileName) && dfOpen(&df); // ��������� �����
		if (Result)
		{
			card CurrentItem = 0, ItemsCount = dfCount(&df);
			if (ItemsCount > 0)
			{
				Result = dfFindFirst(&df);
				tDataItemHeader Item;
				while (dfFindNext(&df, &Item, TRUE)) // ���� ��� ������ � ������
				{
					SendArchiveStates ArchiveState = sasSendTansaction;

					byte *ItemData = memAllocate(Item.Size);
					if (dfReadItemData(&df, &Item, ItemData)) // ������ �������� ������ �� ����� - � ������ � ��� �������������� ����� ��� �������� �� ������ ��
					{
						CurrentItem++;
						displayLS8859(4, "%d �� %d", CurrentItem, ItemsCount); // X �� Y
						tBuffer sndbuf;
						tlvBufferInit(&sndbuf, ItemData, Item.Size); // ������������� ������ ��� ��������� ������

						ItemState = oisUnknown;
						tmrRestart(tsArchive, ArchiveTimeout); // ArchiveTimeout ������ �� ��������� ����� ����������
						while (ArchiveState != sasError && ArchiveState != sasFinish)
						{
							switch (ArchiveState)
							{
								case sasSendTansaction:
									traReset(); // ������� ������� ���������� ����� ��������� ������
									pcParse(&sndbuf); // ����� ����� ���������� ������� ���������� � �������� � � tra
									Result = opsSendMessage(cslotPC, sndbuf.ptr, sndbuf.pos); // ���������� ��������� �� ������
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
															pcHandleAnswerOfflineTransaction(&buf, &PCState, &RSState, &ItemState); // ����������� ���������� �����
															Result = PCState == lssPrepareWaitForData;
															if (Result && ssm != NULL)
																Result = ssm(Item.SearchID);
															tmrRestart(tsArchive, ArchiveTimeout);
															ArchiveState = sasFinish;
															break;
														case pcpErrorMessage: // ���������������� ������
															pcHandleMessageError(&buf, &PCState, &RSState);
															Result = FALSE;
															ArchiveState = sasError;
															break;
														default: // ����������� ������
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
						if (ItemState == oisProcessed) // ���� ���� ��������� (������� ��� �� �������, ��� ������ ������)
							dfDeleteItem(&df, &Item); // �������� ������ ��� ��������
						tmrStop(tsArchive);
					}
					ItemData = memFree(ItemData);
				}
			}
		}
		Result &= dfClose(&df); // ����������� � ����� ������
	}
	else
		usrInfo(infErrorSendingArchiveData); // ������ ��� �������� ������. ��� ���� �������� ������ �� ������ ������� �� ���� ��������

	if (!IsConnected && comIsUsed()) // ���� �� �� ���� ��������� � �������� � ������ �������, �� ��� ���� ��������� � ��� ������,
		pcDisconnectFromServer(); // �� �������������

	//comSwitchToSavedSlot();
	return Result;
}

#ifdef OFFLINE_ALLOWED
// ������� �������������� ������ ����� �������� offline ����������
static Bool DeleteOfflineTransactionData(byte *searchID)
{
	Bool ResultInitials = oftDeleteFromArchiveBySearchID(FILENAME_OFFLINE_INITIALS, searchID);

	oftDeleteFromArchiveBySearchID(FILENAME_OFFLINE_LIMITS, searchID); // ������ ����� ����, � ����� �� ���� �� offline-���������� (������� �� ������������)

	if (!ResultInitials)
		printS("ALERT! Integrity violation. Can't find initial transaction with id: %s\n", searchID);

	return ResultInitials;
}

void pcSendArchiveTransactions(byte *PCState, byte *RSState)
{
	tDisplayState ds;
	dspGetState(&ds);

	displayLS8859(3, "���������: ����������");
	Bool Result = pcSendArchiveDataFile(FILENAME_BANKING, NULL);
	if (Result)
	{
		dspSetState(&ds);
		displayLS8859(3, "���������: ����������");
		pcSendArchiveDataFile(FILENAME_OFFLINE_TRANSACTIONS, DeleteOfflineTransactionData);
	}
	dspSetState(&ds);
	*PCState = lssQueryForDisconnect; // ��������� ���������� ����� �������� Offline-����������
}
#endif

// true, ���� �������� ��������� � ��������� offline (��������� ����� ��� ��������� �����������) �� ��������� � ��
Bool pcInOffline()
{
	#ifdef OFFLINE_ALLOWED
		return mapGetByteValue(regPCCurrentMode) != pcmOnline;
	#else
		return FALSE;
	#endif
}

