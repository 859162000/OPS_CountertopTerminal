/*
 * opsParallelScenario.c
 *
 *  Created on: 26.08.2014
 *      Author: Pavel
 */

// ===================================== ������������ �������� ������������� =====================================
#include <stdio.h>
#include <stdlib.h>
#include <sdk30.h>
#include "log.h"

// Create - ���������� ������ ��� ��� ������� opsCommunication. ��� ���������� � ������. ������ ���� ����������� �������
// Initialize - ���������� ����� ������� ��������� �����������. ����� ���� "������", �� 100ms
// Iterate - ���� �������� ������������� ��������. �� 50ms
// Terminate - ���������� ������������� ��������. �� 50ms

// ===================================== ���� � �������� ���-���� =====================================

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
	// ���� ������� ����� - Confirmation, �� ���������� ��� (����� ���� ��� ������ ������/������/������� ��� ��������� PIN-���)
	// ���� ������� ������, �� ���� �������� ������ ������������ ���, � ����� ���������� - ����� ������ ��������� Confirmation
	byte PacketID = mapGetByteValue(traPCPacketID);
	if (PacketID != pcpConfirmation)
		mapPutByte(traPCPacketIDSave, PacketID); // ��������� ����� ����� ��� ����������� ������������� ����������
	// ��������� ��� ���������� � ����� ��������
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;
	// ������������� ����� ����� PIN-����
	Click();Click();ttestall(0, 10);Click();ttestall(0, 20);Click(); // ���������� ������� ������ PIN ���

	memset(PIN, 0, lenPIN);
	dspGetState(&DisplayState); // save DisplayState
	dspClear();
	displayLS8859(cdlPINQuery, "������� PIN:");
	displayLS8859(cdlPINHelp, "���: �����/��: ������");
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

	if (key >= '0' && key <= '9') // ��������� ����� � ���-����
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
			// ����� ������������� PIN � PINBlock � �������� ��� � ����������
			char CardNumber[lenCardNumber];
			mapGet(traCardNumber, CardNumber, lenCardNumber);
			byte PINBlock[lenPINBlock]; memclr(PINBlock, sizeof(PINBlock));

			cphGetPinBlock(CardNumber, PIN, PINBlock);
			mapPut(traPINBlock, PINBlock, lenPINBlock);
			// ��������� PCState � �������� QueryConfirmation
			*PCState = lssQueryConfirmation;
		}
		else
			Beep();
	}
	else if (key == kbdCOR) // ������� ��������� ������
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

// ===================================== ������ ����������� ����� =====================================

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
			displayLS8859(cdlReadCard1, " ��������");
			displayLS8859(cdlReadCard2, "      �����");
	    	if (crmCanEnterManually())
	    		displayLS8859(cdlFToEnterCardNumber, "F: ���� ������ �����");
			break;
		case 1:
			displayLS8859(cdlCardNumberPrompt, "����� �����:");
	    	/*if (crmCanEnterManually()) �������� ���������, �.�. �������� ���� ������ ����� �� ������������ ��� ������ opsEnterCardNumber
	    		displayLS8859(cdlFToReadCard, "F: ������ �����");*/
			break;
		default:
			displayLS(0, "ReadCardMode error\n%d", ReadCardMode);
			break;
	}
}

void psReadCardInitialize(byte *PCState, byte *RSState)
{
	mapPutByte(traRSPacketIDSave, mapGetByteValue(traRSPacketID)); // ��������� ����� ����� ��� ������� ����� �������� ������ �����
	// ��������� ��� ���������� � ����� ��������
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;

	// ��������� ��������� ����� (������� ����� ��������� ��� ������ ��������)
	if (magStart())
	{

		// �������� ��������� �����
		memset(magtrk1, 0, sizeof(magtrk1));
		memset(magtrk2, 0, sizeof(magtrk2));
		memset(magtrk3, 0, sizeof(magtrk3));

		// �������� ����� ��� �����
		memset(CardNumber, 0, sizeof(CardNumber));

		traResetCardNumber(); // ������� ����� ����� � ���������

		// ������������� ����� ������� �����
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
	Bool CanTerminate = key == kbdANN; // ��������� ��� kbdANN �������������� � opsCommunication
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
		                ocdReadCardData(cardtype); // ������ ��������� ������ �����
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
				                ocdReadCardData(cardtype); // ������ ��������� ������ �����
				            }
				            else
				            {
				            	usrInfo(ret == -1000 ? infInvalidCardType : infCardReadError); // �������� ��� ����� ��� ������ ������ ������ �����
				            	CanTerminate = TRUE;
				    			*PCState = lssReadCard;
				            }
				        }
				        else
				        {   // �������� �� ������ ���������� � ����������� ���� �����
				        	usrInfo(infChipCardIsNotSupported); // ����������� ��� �����
							dsAllocate(dslUnknownATR, rsp, rsplen); // �������� ������ ��� ATR
			            	CanTerminate = TRUE;
							*PCState = lssStartSendATR;
				        }
					}
				}
			}
			else
			{
			/*
			    ������ - ���������� � ����� opsCommunication, ����� �� ������� ����� ������������ + ������ �� ����� ������ ������������ ����� ������������
				if (iccStart(ChipReader) > 0) // ������ ������ �����
				{
					displayLS8859(cdlReadCard1, "  ������");
					displayLS8859(cdlReadCard2, "      �����");
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

			if (key >= '0' && key <= '9') // ��������� ����� � ���-����
			{
				if (CardNumberLength < MaxCardNumberLength) CardNumber[CardNumberLength] = key; else Beep();
			}
			else if (key == kbdCOR) // ������� ��������� ������
			{
				if (CardNumberLength > 0) CardNumber[CardNumberLength-1] = 0x00; else Beep();
			}
			else if (key == kbdANN)
			{
				CanTerminate = TRUE; // ��������� �������������� � opsCommunication
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

			/*if (crmCanEnterManually() && key == kbdMNU) // ������������ ������ �����
			{ // ������, �.�. � opsEnterCardNumber ��� ��������� �������������
				ReadCardMode = 0;
				psReadCardShowInvitation();
			}*/

			break;
		}
	}

	if (CanTerminate)
		psReadCardTerminate(PCState, RSState);
}

// ===================================== ���� ��������� ���� ��������� ����������� =====================================

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
	// ��������� ���������� ������ �� �� � ���
	mapCopyByte(traRSPacketID, traRSPacketIDSave);
	mapCopyByte(traPCPacketID, traPCPacketIDSave);
	// ��������� ��� ���������� � ����� ��������
	if (pcIsConnected() || pcInOffline()) *PCState = lssHoldConnection;
	if (rsIsConnected()) *RSState = rssHoldConnection;
	// ������������� ����� ����� ���� ��
	Click();Click();ttestall(0, 10);Click();ttestall(0, 20);Click(); // ���������� ������� ������ ��� ��

	memset(VACode, 0, sizeof(VACode));
	dspGetState(&DisplayState); // save DisplayState

	dspClear();
	displayLS8859(cdlVACodeTitle, "��������� �����������");
	displayLS8859(cdlVACodeFooter, "���: �����/��: ������");
	displayLS8859(cdlVACodeInfoOnReceipt, "���������� �� ����.");
	mapGet(traVoiceAuthAnswerCode, VACode, lenVoiceAuthCode); // �������������� ��� �� ���, ��� ��� ���� � ���������� (��� ������������� �����)
	sprintf(sDummy, "�������� ��� (%d):", (int)strlen(VACode)); // ������� �������� ������
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

	if (key >= '0' && key <= '9') // ��������� ����� � ���-����
	{
		if (VACodeLength < MaxVACodeLength) VACode[VACodeLength] = key; else Beep();
	}
	else if (key == kbdANN)
	{
		psEnterVACodeTerminate(PCState, RSState);
		*PCState = lssDisconnecting; // ����� �� ������������
		*RSState = rssDisconnecting;
	}
	else if (key == kbdVAL)
	{
		if (strlen(VACode) >= 28) // 28 �������� - ����������� ����� ��������� ����
		{
			psEnterVACodeTerminate(PCState, RSState);
			*RSState = rssRehandlePacket; // ��������� ��������� ������ �� ���
			mapPut(traVoiceAuthAnswerCode, VACode, lenVoiceAuthCode); // ��������� �������� ��� �� � ����������
		}
		else
		{
			Beep(); Beep();
		}
	}
	else if (key == kbdCOR) // ������� ��������� ������
	{
		if (VACodeLength > 0) VACode[VACodeLength-1] = 0x00; else Beep();
	}

	if (key != 0)
	{
		sprintf(sDummy, "�������� ��� (%d):", (int)strlen(VACode)); // ������� �������� ������
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
		displayLS(cdlVACodeEntry, VACodeDisplay); // ������ ����� ������� ������
		if (strlen(VACodeDisplay) > dspW) // ���� ����� ����, ��
		{
			strcpy(sDummy, VACodeDisplay + dspW);
			displayLS(cdlVACodeEntry + 1, sDummy); // ������� ���������� ����� ����
		}
		else
			displayLS(cdlVACodeEntry + 1, " ");
	}
}



// =============================== ����� ������� ========================================

Bool psIsStarted()
{
	return IsEnterPINStarted || IsReadCardStarted || IsEnterVACodeStarted;
}
