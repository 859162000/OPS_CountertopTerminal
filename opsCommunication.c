/*
 *
 *  Created on: 22.03.2011
 *      Author: Pavel
 *  ������ ��������� �������� �������������� ��-��������-���. ��� ������������� ���������� offline-����������
 */
#include <stdio.h>
#include <stdlib.h>
#include <sdk30.h>
#include <string.h>
#include <LinkLayer.h>
#include "log.h"

// ========================== ����������� � ����������� =================================================

// ConnectionStatuses
typedef enum
{
	csNotConnected,
	csConnected,
	csOffline
} ConnectionStatuses;


static void dspConnectToPC(byte ServerNumber, ConnectionStatuses Status)
{
    displayLS8859(cdlPC, "  � �� #%u...%s", ServerNumber, Status == csConnected ? "OK" : (Status == csOffline ? "�������" : ""));
}

static void dspConnectToRS(ConnectionStatuses Status)
{
    displayLS8859(cdlRS, "  c ���...%s", Status == csConnected ? "OK" : (Status == csOffline ? "�������" : ""));
}

// ========================== ���������� � �������� ���������� =================================================

Bool opsConnectToPC(Bool CanSwitchToOfflineMode)
{
	Bool CanSwitchToOnlineMode = TRUE;
	byte i, ServerNumber = mapGetByteValue(cfgComPCSelected);
	int ret = 0; // 0 - ��������� �� ���������, �� ���������� ���. 1 - �������. -1 - ������

	//if (!pcInOffline())
	//{
		if (!pcCheckConnection()) // ���� �� �� ��������� � ��, ��
		{
			byte attempt, AttemptsCount = mapGetByteValue(cfgComConnectionAttmpts);

			for (attempt = 0; attempt < AttemptsCount; attempt++) // �������� ����������� � ������� ������� AttemptsCount ���
			{
				for (i = 0; i < PC_SERVERS_COUNT; i++) // ���������� ��� �������
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
						    mapPutByte(cfgComPCSelected, ServerNumber); // �������� ������������ ������
						    if (CanSwitchToOnlineMode)
						    	mapPutByte(regPCCurrentMode, pcmOnline); // ���� ����������� - ����������� ����� �� �� online
						    //mapPutByte(traPCConnected, 1);
						    //printS("Finish! %d\n", get_tick_counter());
						    ret = 1; // good connection
						    break;
						}
						/*else - ������� ��� ������ ����� ������� ����������� � ��������
						{
							char cs[lenIPAddress+6+1]; // address+port+nt
							if (pcGetConfigString(ServerNumber, cs))
								PrintRefusal("\xC1\xD5\xE0\xD2\xD5\xE0\n%s\n\xDD\xD5\x20\xDE\xE2\xD2\xD5\xE7\xD0\xD5\xE2\n", cs); // ������ \nip|port\n �� ��������
						}*/
					}
					ServerNumber = (ServerNumber % PC_SERVERS_COUNT) + 1;
					pcCloseChannel();

					if (ChannelError != LL_ERROR_OK) // ���� ��������� ������ ������, �� ���������� ���������� � �������
					{
						DisplayLLError(cdlDisplayError, ChannelError, "������ ������", 10);
						ret = -1;
						break;
					}
				}
				if (ret != 0) break; // ���� ��������� ��� �������� - �������
			}
		}
		else
		{
			// �� ��������� � ��
		    dspConnectToPC(mapGetByteValue(cfgComPCSelected), csConnected);
		    ret = 1; // good connection
		}
	//}

	// ���� ret <= 0, �� �� ��� �� ������ ����������� � �� ��� �� ������� �������
	if (ret <= 0 && CanSwitchToOfflineMode) // ���� ��� ��������� ���������� �������� � ����� offline, �� ��������� ��� � offline
	{
		mapPutByte(regPCCurrentMode, pcmOffline); // ��������� �������� � ����� Offline
		dspConnectToPC(mapGetByteValue(cfgComPCSelected), csOffline);
	}

	return ret > 0;
}

// ========================== ���������� � �������� ���������� =================================================

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
			displayLS8859(dspLastRow, "��� ������: %d                     ", ret);
			usrInfo(infRSDidNotAnswer);
		}
	}
	else
	{
		displayLS8859(dspLastRow, "��� ������: %d                         ", ret);
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

// ���� �� ������������ #define CHECK_REFUNDS_ONLY { if (IsOnlyRefundAllowed()) { RSState = rssProtocolError; PCState = lssProtocolError; break; } };

// ���������� TRUE, ���� ���������� ��������� ��������� ����� ���� ����������� ������ � Online
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

// ���������� TRUE, ���� ��� ���������� ��������� ��������� ����� �������� ������ �����������
static Bool comnHideConnectionErrors(byte PCState)
{
	return PCState == lssStartSendATR || PCState == lssStartSendOnlineState;
}

/*
// ���������� TRUE, ���� ��� ���������� ��������� ��������� ��������� ������ � ������ ��������� �����������
static Bool comnIsAllowVoiceAuth(byte PCState)
{
	return PCState == lssStartAutonomousTransaction || PCState == lssStartCardMaintenance || PCState == lssStartCancellationWithoutRS
			|| PCState == lssStartConnectionFromPetrol;
}*/

/*
// ���������� TRUE, ���� ����� ������������ � ��
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
						// ���������� ��� ��������� �����������
						char VoiceCode[lenVoiceCode];
						GenerateVoiceCode(VoiceCode);

						dspClear();
						displayLS(0, "\xBF\xDE\xD7\xD2\xDE\xDD\xD8\xE2\xD5\x20\xDF\xDE\x20\xE2\xD5\xDB\x2E\x3A"); // ��������� �� ���.:
						displayLS(1, "\xD3\xDE\xE0\xEF\xE7\xD5\xD9\x20\xDB\xD8\xDD\xD8\xD8\x20\x20\x20\x20\x20"); // ������� �����
						displayLS(2, "\xD8\x20\xDF\xE0\xDE\xD4\xD8\xDA\xE2\xE3\xD9\xE2\xD5\x20\xDA\xDE\xD4\x20"); // � ����������� ���
						displayLS(3, VoiceCode);
						displayLS(INV(dspLastRow), "\xB7\xB5\xBB\x2D\xD4\xD0\xDB\xD5\xD5\x20\x20\xBA\xC0\x2D\xE1\xE2\xDE\xDF"); // ���-�����  ��-����
						kbdWait(0);
						dspClear();
					}
				}
				break;
		}
	}
	return TRUE; // ���� �� ���������� �������� - ������������ � ��
}*/

/*Bool comnIsImmediatlyConnectWithPC(byte PCState)
{
	return PCState != lssNotConnected && PCState != lssWaitDataFromRS;
}*/

#define PCBREAK { if (PCState == lssDisconnecting || PCState == lssQueryForDisconnect || PCState == lssErrorReceived || PCState == lssProtocolError || PCState == lssTimeout /*|| PCState == lssNotConnected*/ || PCState == lssIncorrectPIN) break; }
#define RSBREAK { if (RSState == rssDisconnecting || RSState == rssErrorReceived || RSState == rssProtocolError || RSState == rssTimeout) break; }

//#define SHOW_STATUS

// ========================= �������� ������� �������� �������������� � TIMEOUT-������� � ����� ������������ =================================
// �� ���� ��� - �������� �������
// ��������� � PC �������� �, ��� �������������, ��������� � ���, ���� �� ����������� � opsProcessEvent()
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
    Bool CanSwitchPCToOffline = AllowShowConnectionErrors && oflGetPossibility() != ofpOnline; // ����� ���������� �� � offline ������ ����� ���������� ������ � � ��� ��������� ������������� offline-�������
    tDisplayState DisplayState;
    Bool IsPCConnectionDone = FALSE, IsRSConnectonDone = RSState != rssNotConnected; // ���� TRUE, �� ������� ���������� � ��/��� �������� (���� ���� �� � offline)
    Bool RSHaveUnhandledPacket = FALSE;

    if (AllowShowConnectionErrors && !PrintPaperControl()) { usrInfo(infNoPaper); return; }

    opsOfflinePossibilities OfflinePossibility = oflGetPossibility(); // �������� �������� �� ����� offline ��� ��
	#ifndef OFFLINE_ALLOWED
    	OfflinePossibility = ofpOnline; // ���� ��������� ����� �������� - ������ ����� ������ ONLINE, ���������� �� ��������� � �������� ������
    	OnlineOnly = TRUE;
	#endif

	dspClear();
    displayLS(cdlHeader, Header);

	// ������ 05/09/2014 - ���� ��������������� ��� ��������� offline if (pcInOffline()) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsOfflineTransaction); // ������������� ���� "offline-����������"
	if (pcShouldPermanentlyConnected()) mapPutWord(traPCFlags, mapGetWordValue(traPCFlags) | pcfIsFirstPackage); // ���� ����� ��������� ������� ����� � ��, �� ������������� ���� "������ ����� � ����������"

	if (pcIsConnected()) dspConnectToPC(mapGetByteValue(cfgComPCSelected), csConnected);
	if (rsIsConnected()) dspConnectToRS(csConnected);

	displayLS8859(cdlAbort, "�������: ������");

	// �������� ���� �������� �������� ��������������
	card cntr = 0;
	byte cntrinc = 1;
	psEnterPINCreate();
	psReadCardCreate();
	psEnterVACodeCreate();

	#ifdef __TELIUM__
		// ������� ���������� � ��������� ������
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
    	if (key == kbdANN) // ���� � ����� ������������ ������ �� - �������
    	{
    		usrInfo(infCancelledByUser);
    		break;
    	}

    	if (!psIsStarted())
    	{
    		displayLS8859(cdlQueryCounter, "������: %u", cntr);
    		displayLS8859(cdlQueryCounter+1, " "); // clear next string

    		if (ocdCanStore()) // ���� �� ����� ���� offline-����������, �� ������� ����������� � ���, ��� ����� �������� ������, ����� ����� ���� �������� � ������� ����������� ����
    			displayLS8859(cdlCardWarning, "����� �� ��������");
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


    	PCBREAK; // ��������� ������ PC (����� �� ����� �����/��������)

    	// ======================================================== ������ ���������� � Processing center ======================
		comSwitchToSlot(cslotPC);

    	if (PCState != lssNotConnected && PCState != lssQueryForEstabilishConnection && !pcInOffline()) // check the connection status
    		PCState = comIsConnected() ? PCState : lssDisconnecting;

    	switch (PCState)
    	{
    		case lssQueryForEstabilishConnection: // ������� ���������� ���������� � ��
    			// ����������� ��������� ���, ����� �� ������ ��������� ������ �� ����
    			if (RSState == rssPrepareWaitForData || RSState == rssWaitForData)
    				break; // ������� � ������������ � ���������� ���� �����...

    			// ������ �������� ����������� � ��
				PCState = opsConnectToPC(CanSwitchPCToOffline) ? InitialPCState : lssNotConnected;

				if (PCState == lssNotConnected) // �� ������ ����������� � �������
				{
					mapPutByte(traResponse, 5); // ������������� ��� ������
					if (AllowShowConnectionErrors) // ���� ����� ����������������� � ������, ��
					{
						dspGetState(&DisplayState);
						#ifdef OFFLINE_ALLOWED
						if (OfflinePossibility != ofpOnline && pcInOffline() && !OnlineOnly) // ���� ��-������ ������ ��������, �� ���������� ����� �� ����������
						{
							Beep();
							tDisplayState ds;
							dspGetState(&ds);
							dspClear();
							displayLS8859(1, "������ �� ����������.");
							displayLS8859(2, "���������� ������");
							switch (OfflinePossibility)
							{
								case ofpOffline:
									displayLS8859(3, "� ��������� ������?");
									break;
								case ofpVoiceAuthorization:
									displayLS8859(3, "� ������ ���������");
									displayLS8859(4, "�����������?");
									break;
								default:
									displayLS8859(3, "� ����������� ������?");
									break;
							}
							displayLS8859(INV(dspLastRow), "���: ��       ��: ���");
							int key = kbdWaitInfo8859(10, CNTR(INV(0)), "����������� (%d �.)"); // ����������� (10 �.)
							if (key == kbdANN)
								goto lblEnd; // ���� Offline ������� - �������
							dspSetState(&ds);
						}
						else
						#endif
							usrInfo(infNoPCConnection); // ����� ������ � online - ����� ������
						dspSetState(&DisplayState);
					}
				}

    			if (PCState == lssNotConnected && (OfflinePossibility == ofpOnline || OnlineOnly)) // ���� �� �� ����������� � �������� � ����� �������� ������ � online - ������� �� ����� ���������
    				goto lblEnd;

				#ifdef OFFLINE_ALLOWED
				if (pcInOffline() && !ocHasConfig()) // ���� �������� � ������ offline, �� ��� ��������� ������������ - ������� ��������� � �������
				{
					usrInfo(infOfflineConfigIsNotLoaded);
					goto lblEnd;
				}
				#endif

				#ifdef OFFLINE_ALLOWED
				if (pcInOffline()) // ���� �������� � offline-������, �� ������������� ��������� ��������� ������� � ��������� �� ��, ��� �������� � ������ offline
				{
					PCState = InitialPCState;
					dspConnectToPC(mapGetByteValue(cfgComPCSelected), csOffline);
				}
				#endif

				if (PCState == lssNotConnected) // ����� �����: ���� ����� ���� �������� �� ������� � ��������� NotConnected - ������������ ���
					PCState = lssDisconnecting;

				IsPCConnectionDone = TRUE; // ������� ���������� ��������, ���� ���� �� � ������ offline
    			break;
			case lssStartCardMaintenance: // ���������� ������ ����� �� ��������� ���������� � ����� �� ��
				pcSendQueryCardInfo(FALSE, FALSE, &PCState, &RSState); break;
			case lssStartGetCardInfo: // ���������� ������ �� ��������� ���������� � ����� �� ��
				pcSendQueryCardInfo(TRUE, FALSE, &PCState, &RSState); break;
	  	    case lssStartConnectionFromPetrol: // ���������� ������ ����� ��� ������ ������������ ��� �����
				pcSendQueryConnectionFromExternalApp(&PCState, &RSState); break;
	  	    case lssStartCancellationWithoutRS:
				pcSendQueryCancellationWithoutRS(&PCState, &RSState); break;
			case lssStartValidation: // ���������� ������ �� ��������� ������ �� �����
				pcSendQueryStartValidation(&PCState, &RSState); break;
			case lssStartOpenShift: // ���������� ������ �� ����������� ����� �� ���������
				pcSendQueryStartTerminalAuth(&PCState, &RSState); break;
			case lssStartSendATR: // ���������� �� ������ ���������� � ����������� ATR
				pcSendUnknownATR(&PCState, &RSState); break;
			case lssStartSendOnlineState:// ���������� �� ������ ���������� � ��������� ������
				pcSendPeriodic(&PCState, &RSState); break;
			case lssStartAcceptQuest: // ���������� �� ������ ������ �� ���� ������
				pcSendQueryAcceptQuest(&PCState, &RSState); break;
			case lssStartGetCardActions: // ������ �� ��������� ������ ����� � ������� ����� ������������ (�� ������� ����� �����������) �����
				pcSendQueryCardActionsList(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartEnableCardAction: // ������ �� ��������� ��� ����������� �����
				pcSendQueryEnableAction(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartUpdateSoftware: // ���������� ������ �� ��������� ������ ������ ��� ����������
				pcSendQueryFileList("SWAP", &PCState, &RSState); break;
			case lssStartUpdateConfiguration: // ���������� ������ �� ��������� ������������
				pcSendQueryFileList("HOST", &PCState, &RSState); break;
			case lssStartAutonomousTransaction: // ���������� �� ������ ���������� ����������
				pcSendAutonomousTransaction(&PCState, &RSState); break;
			case lssStartLoadAutonomousGoods: // ���������� �� ������ ������ �� �������� ������� ��� ������ ��������� � ���������� ������
				pcSendQueryAutonomousGoods(&PCState, &RSState); break;
			case lssStartQueryKeySet: // ���������� �� ������ ������ �� �������� ���������� ����������
				pcSendQueryKeySet(&PCState, &RSState); break;
			case lssEnterPIN: // ������������ �������� ����� PIN-����
				psEnterPINInitialize(&PCState, &RSState); break;
			case lssEnterVoiceAuthAnswerCode: // ������������ �������� ����� ��������� ���� ��
				psEnterVACodeInitialize(&PCState, &RSState); break;
			case lssQueryConfirmation: // ��������� �� ������ ������ ������������� ����������
				pcSendQueryConfirmation(&PCState, &RSState); break;
			case lssReadCard: // ������������ �������� ������ �����
				psReadCardInitialize(&PCState, &RSState); break;
			case lssStartQueryLogotype: // ���������� ������ �� ��������� ��������
				pcSendQueryLogotype(&PCState, &RSState); break;
			case lssStartQueryDateTime: // ���������� ������ �� ������������� ���� � �������
				pcSendQueryDateTime(&PCState, &RSState); break;
			case lssStartGetCopyOfReceipt: // ���������� ������ �� ��������� ����� ����
				pcSendQueryCopyOfReceipt(TRUE, FALSE, &PCState, &RSState); break;
			case lssStartRemindPIN: // send query for remind card PIN code
				pcSendQueryRemindPIN(&PCState, &RSState); break;
		#ifdef OFFLINE_ALLOWED
			case lssStartSendArchiveTransactions:
				pcSendArchiveTransactions(&PCState, &RSState); break; // �������� ��������� ����� � ��������� ������������
			case lssStartQueryOfflineConfiguration:
				ocSendQuery(&PCState, &RSState); break; // ���������� ������ �������� ��������� ������������
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
				tmrRestart(tsPCTimeout, PCTimeout); // ������ �������� ����� ��������
	    		break;
	    	case lssWaitForData:
	    		if (!pcInOffline())
	    		{   // for online mode
	    			Data = opsReceiveMessage(cslotPC, &DataSize, &ControlCharacter); // Receive message and ALLOCATE MEMORY
	    		}
	    		else
	    		{   // for offline mode - retreive packet from data slot
	    			DataSize = dsGetDataSize(dslReceivedData);
	    			Data = memAllocate(DataSize); // ������ ������������ �������� ��������, ������ ��� Data ��������� � �����
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
								mapPutByte(traPCPacketID, PacketID); // ��������� ID �������� ������
								PCState = lssHoldConnection; // �������� ��������� ����������
								pcHandlePacket(PacketID, &buf, &PCState, &RSState); // �������� ���������� ������� ��
							}
							else
								PrintRefusal("Package from PC does not contain required tag 0x01\n");
						}
						else
						{
							PCState = lssProtocolError;
							usrInfo(infEmptyPackageFromPC); // �� �� ������� ������ �����
						}
						break;
					case pccBEL: // hold the connection
						comSwitchToSlot(cslotPC);
						comSend(pccBEL);
						PCState = lssPrepareWaitForData;
						break;
					case pccEOT: // disconnect from server
						PCState = lssDisconnecting;
						pcDisconnectFromServer(); // ���� ������ �������� ������ �� ���������� - ������ ������������
						usrInfo(infDisconnectFromPC); // ����� ��������\n�� ������� ��
						break;
					default: // no data - check timeout
			    		if (tmrGet(tsPCTimeout) <= 0) // check server timeout
			    		{
			    			tmrStop(tsPCTimeout);
			    			PCState = lssTimeout;
			    			usrInfo(infPCTimeout); // ��: �������
			    		}
			    		else
			    			cntrinc = 0; // �� ���������� ��������� ��������
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
	    		else // � ������ offline ��������� ���� BEL �� "������������" ��.
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
				//PCState = lssHoldConnection;  //��� ����� �������� ������ �� ������������ ���������� � ��, ����� ����, ���� �� ����� ����� � ������� �������� ������
				//PCState = lssQueryForEstabilishConnection; // ������, �.�. ����� �� ����� � ����� ��������� ��������� ���������� � ��
				break;*/
			case lssNotConnected:
				break;
			default:
				break;
    	}

    	PCBREAK; // ��������� ������ PC (����� �� ����� �����/��������)

    	// ======================================== ������ ���������� � Retail System ==================================


    	RSBREAK; // ��������� ������ RS (����� �� ����� �����/��������)

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
			case rssRehandlePacket: // ��������� ��������� ����� ��������� ������
				tlvBufferInit(&buf, dsGetData(dslRSPackage), dsGetDataSize(dslRSPackage)); // �������������� ����� ������� �� ���, ���������� � ���� �����
				rsHandlePacket(mapGetByteValue(traRSPacketIDSave), &buf, &PCState, &RSState);
				if (RSState == rssRehandlePacket) // ���� ��������� ���������� �� ��������, �� ��� ��������� � ����� �������� ��� "���������" ���
					RSState = rsIsConnected() ? rssHoldConnection : rssNotConnected;
				break;
			case rssPrepareWaitForData:
				RSState = rssWaitForData;
				tmrRestart(tsRSTimeout, mapGetCardValue(traRSTimeout));
	    		break;
			case rssWaitForData: // ������� ������ � ������� ����������
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
								mapPutByte(traRSPacketID, PacketID); // ��������� ID ������
								dsAllocate(dslRSPackage, Data, DataSize); // ��������� ��� �����
								RSState = rssHoldConnection; // ���������� ����������. ����� ����� ��������� � ���� �����������
								RSHaveUnhandledPacket = TRUE; // ������� ���� ������������� ��������� ��������� ������
							}
							else
								PrintRefusal("Package from RS does not contain required tag 0x01\n");
						}
						else
						{
							RSState = rssProtocolError;
							usrInfo(infEmptyPackageFromRS); // �� ����� ������� ������ ����� ------------------------
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
						usrInfo(infDisconnectFromRS); // ����� ��������\n�� ������� �� ���
						break;
					default: // no data - error or timeout
			    		if (tmrGet(tsRSTimeout) <= 0) // check RS timeout
			    		{
			    			tmrStop(tsRSTimeout);
			    			RSState = rssTimeout;
			    			usrInfo(infRSTimeout); // ���: �������
			    		}
			    		else
			    			cntrinc = 0; // �� ���������� ��������� ��������
						break;
	    		}
				// FREE MEMORY OF MESSAGE
	    		Data = memFree(Data);
	    		break;
	    	case rssHoldConnection:
				if (IsPCConnectionDone && RSHaveUnhandledPacket) // ���� ��������� ������ ���������
				{
					tlvBufferInit(&buf, dsGetData(dslRSPackage), dsGetDataSize(dslRSPackage));
					rsHandlePacket(mapGetByteValue(traRSPacketID), &buf, &PCState, &RSState); // �������� ���������� ������� ���
					RSHaveUnhandledPacket = FALSE; // ��, ����� ���������
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

    	RSBREAK; // ��������� ������ RS (����� �� ����� �����/��������)

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
	displayLS8859(INV(cdlHeader), "����������...");
	displayLS8859(cdlPC, "  �� ��...");
	displayLS8859(cdlRS, "  �� ���...");
    // ��������� ��� ����������
	rsDisconnectFromServer();
	displayLS8859(cdlRS, "  �� ���...OK");
	if ((!pcShouldPermanentlyConnected()) || ((pcShouldPermanentlyConnected() && pcIsConnected()) && (PCState == lssNotConnected || PCState == lssTimeout)))
		pcDisconnectFromServer(); // ��������� ����������, ���� ����������� �� ����������
	displayLS8859(cdlPC, "  �� ��...OK");
	Data = memFree(Data); // ���� � Data ���-�� ����, �� ����������� � ������

	#ifdef OFFLINE_ALLOWED
		if (pcInOffline())
		{
			ocsFree(); // ���� �� � ������ offline, �� ������� ������ ���������� �������
			oftFree(); // 27/02/2015 ...� ����������. ��������� ��� ����������, �.�. � ������ �� ������ �� ��������, � ���������� ������� ������������
		}
	#endif

	// ���� ���������� ������ � �� ������ ���� - ������ ��� ����������� �����������, �.�. ����������
	// ����������� ������������� � ������� ��������� ��������� ����� �����
    if (mapGetByteValue(traIsUpdateStarted) != 0) // ���� ���������� ��������� �����������, ������ ������ � ������� �� ���� ����������
    {
    	updDeinitialize();
    	usrInfo(infUpdateError);
    }

    opsShowTransactionInformation();

    opsWaitForEjectCardAndCloseReader(); // ���� � ��� ��������� �����, �� ������ � ������

#ifdef __TELIUM__
	// ������� ���������� � ��������� ������
	//printS("mem2: %u\n", FreeSpace());
#endif

}
