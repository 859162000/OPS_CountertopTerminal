/*
 * opsCardOffline.c
 *
 *  Created on: 17.02.2014
 *      Author: Pavel
 */
/* Информация, записанная на карту, и использующаяся для работы в аварийном режиме */

#include "log.h"

// может ли карта
Bool ocdCanStore()
{
	return mapGetByteValue(ofcdCanStore) != 0;
}

// есть ли у текущей карты аварийная информация
Bool ocdHasData()
{
	return mapGetByteValue(ofcdHasData) != 0;
}

// если ли у текущей карты список аварийных лимитов
Bool ocdHasLimits()
{
	return mapGetByteValue(ofcdHasLimits) != 0;
}

// нужно ли обновить аварийные данные на карте
Bool ocdNeedUpdate()
{
	return ocdCanStore() && mapGetByteValue(ofcdTagReceived) != 0 && mapGetCardValue(ofcdCRCReaded) != mapGetCardValue(ofcdCRCReceived);
}

// TRUE, если расход хотя бы по одному из лимитов был изменен
static Bool ocdLimitsConsumptionWasChanged()
{
	return oclChangedCount() > 0;
}

// нужно ли записать новые аварийные лимиты на карту: нужно если а) загружены изменения лимитов ИЛИ б) при проведении offline-транзакции изменились расходы по лимитам
Bool ocdLimitsNeedUpdate()
{
	return ocdCanStore() && ((mapGetByteValue(ofcdLimitsTagReceived) != 0 && mapGetCardValue(ofcdLimitsCRCReaded) != mapGetCardValue(ofcdLimitsCRCReceived))
			               || ocdLimitsConsumptionWasChanged());
}

static void ocdParseCardLimitReceived(byte *Buffer, card BufferSize, word ZeroIndex)
{
	tlvReader tr;
    tlvrInit(&tr, Buffer, BufferSize);
    byte tag;
    card len;
    byte *value;

    mapMove(ofclBeg, ZeroIndex); // move to current index (Index SHOULD BE 0-based)
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag) // на 1 уже не обращаем внимания - мы его увидели раньше
    	{
    		case scltID: mapPutCard(ofclID, tlv2card(value)); break;
    		case scltPurseID: mapPutCard(ofclPurseIDLocal, tlv2card(value)); break;
    		case scltPurseType: mapPutByte(ofclPurseType, tlv2byte(value)); break;
    		case scltPurseName:
    			if (len != 0)
				{
					value[len] = 0;
					mapPut(ofclPurseName, value, len + 1);
				}
				break;
    		case scltDuration: mapPutByte(ofclDuration, tlv2byte(value)); break;
    		case scltLimitValue: mapPutCard(ofclLimit, tlv2card(value)); break;
    		case scltFlags: mapPutByte(ofclFlags, tlv2byte(value)); break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }
}


// Парсит присланный список аварийных лимитов карты
void ocdParseCardLimitsListReceived(byte *Buffer, card BufferSize)
{
	tlvReader tr;
    tlvrInit(&tr, Buffer, BufferSize);
    byte tag;
    card len;
    byte *value;
    word CurrentIndex = 0;

    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case sclltCRC: mapPutCard(ofcdLimitsCRCReceived, tlv2card(value)); break;
			case sclltLimit:
				ocdParseCardLimitReceived(value, len, CurrentIndex);
				CurrentIndex++;
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }
	mapPutWord(ofcdLimitsCount, CurrentIndex);
	mapPutByte(ofcdLimitsTagReceived, 1); // ставим признак того, что мы приняли и разобрали тег с аварийными лимитами карты
}

// Парсит присланные аварийные данные карты
void ocdParseCardDataReceived(byte *Buffer, card BufferSize)
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
    	{
			case scdtCRC: mapPutCard(ofcdCRCReceived, tlv2card(value)); break;
			case scdtType: mapPutByte(ofcdType, tlv2byte(value)); break;
			case scdtState: mapPutByte(ofcdState, tlv2byte(value)); break;
			case scdtOwnerID: mapPutCard(ofcdOwnerID, tlv2card(value)); break;
			case scdtPINBlock: mapPut(ofcdPINBlock, value, lenPINBlock); break;
			case scdtStatus: mapPutCard(ofcdStatus, tlv2card(value)); break;
			case scdtDiscountPercent: mapPutCard(ofcdDiscountPercent, tlv2card(value)); break;
			case scdtCurrencyID: mapPutCard(ofcdCurrencyIDLocal, tlv2card(value)); break;
			case scdtCurrencyIntCode: mapPutWord(ofcdCurrencyIntCode, tlv2word(value)); break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }
	mapPutByte(ofcdTagReceived, 1); // ставим признак того, что мы приняли и разобрали тег с аварийными данными карты
}

#ifdef OFFLINE_ALLOWED

// возвращает перекодированный ID кошелька для переданнго ПЦ и локального кошелька
static card GetRemotePCPurseCode(card LocalPurseID, card RemotePCID)
{
	int i, Count = mapGetWordValue(regPCPursesRecodeCount);
	for (i = 0; i < Count; i++)
	{
		mapMove(pcprBeg, i);
		if (mapGetCardValue(pcprOwnPurseID) == LocalPurseID && mapGetCardValue(pcprRemotePCID) == RemotePCID)
			return mapGetCardValue(pcprRemotePurseID);
	}
	return -1; // не нашли перекодировку. По идее можно возвращать LocalPurseID, но тогда возможны случаи когда перекодированные ID будут перемешаны с неперекодированными и будет хаос
}

// Перекодирует локальный кошельки в кошельки ПЦ, обслуживающего терминал, после чего записывает результат в non-Local теги (ofclPurseID, ofcdCurrencyID)
// Если перекодировка не найдена, то записывает -1
static void ocdRecodeIDs()
{
	// если аварийная конфигурация не загружена - ничего не делаем
	if (!ocHasConfig()) return;

	char CardNumber[lenCardNumber];
	mapGet(traCardNumber, CardNumber, lenCardNumber);

	int RangeIndex = ocGetCardRangeIndex(CardNumber);
	if (RangeIndex >= 0)
	{
		mapMove(crdrBeg, RangeIndex);
		card CardPCID = mapGetCardValue(crdrPCID), TerminalPCID = mapGetCardValue(trmePCID);
		Bool IsLocalCard = CardPCID == TerminalPCID;

		if (IsLocalCard)
			mapCopyCard(ofcdCurrencyIDLocal, ofcdCurrencyID);
		else
			mapPutCard(ofcdCurrencyID, GetRemotePCPurseCode(ofcdCurrencyIDLocal, CardPCID)); // перекодировка валюты карты

		int i, Count = mapGetWordValue(ofcdLimitsCount);
		for (i = 0; i < Count; i++)
		{
			mapMove(ofclBeg, i);
			if (IsLocalCard)
				mapCopyCard(ofclPurseIDLocal, ofclPurseID);
			else
				mapPutCard(ofclPurseID, GetRemotePCPurseCode(ofclPurseIDLocal, CardPCID)); // перекодировка кошельков и -1 если не найдено
		}
	}
	else
	{
		displayLS(2, CardNumber);
		usrInfo(infUnknownRangeForCardNumber);
	}
}

#endif

#define OPS2_KEY_ADM1 0x0A
#define OPS2_KEY_ADM2 0x0B

// tChipFileHeader
typedef struct
{
	word Size; // размер значимых данных, которые нужно прочитать из файла
	byte Reserved[6]; // резерв
} __attribute__((packed)) tChipFileHeader;

// читает DataSize байт из текущего файла, открытого на чипе карты, используя ReadCommand и готовые буферы Response и ResponseSize для экономии памяти
static Bool OPS2ReadChipFile(byte Reader, tAPDUCommand *ReadCommand, byte *Response, int *ResponseSize, word DataSize, byte *Data)
{
	word FileIndex = sizeof(tChipFileHeader), DataIndex = 0;
	byte FileIndexBytes[sizeof(FileIndex)];
	while (DataIndex < DataSize)
	{
		byte Portion = DataSize - DataIndex > 0xFF ? 0xFF: DataSize - DataIndex;
		memcpy(FileIndexBytes, &FileIndex, sizeof(FileIndexBytes));
		apducmdSetP1P2Le(ReadCommand, FileIndexBytes[1], FileIndexBytes[0], Portion);
		if (apducmdExecute(Reader, ReadCommand, Response, ResponseSize)) // читаем данные и копируем их в буфер
			memcpy(Data + DataIndex, Response, Portion);
		else
			break;
		FileIndex += Portion;
		DataIndex += Portion;
	}
	return DataIndex == DataSize;
}

// записывает DataSize байт из Data в текущий файл. Сам формирует fileheader. Использует UpdateCommand и готовые буферы Response и ResponseSize
static Bool OPS2WriteChipFile(byte Reader, tAPDUCommand *UpdateCommand, byte *Response, int *ResponseSize, word DataSize, byte *Data)
{
	byte HeaderSize = sizeof(tChipFileHeader);
	word FileIndex = HeaderSize, DataIndex = 0;
	byte FileIndexBytes[sizeof(FileIndex)];

	// записываем заголовок файла
	tChipFileHeader Header;
	memset(&Header, 0, HeaderSize);
	Header.Size = DataSize;
	apducmdSetP1P2Data(UpdateCommand, 0, 0, HeaderSize, (byte *)&Header);
	if (apducmdExecute(Reader, UpdateCommand, Response, ResponseSize)) // если успешно записали заголовок
	{
		// записываем данные
		while (DataIndex < DataSize)
		{
			byte Portion = DataSize - DataIndex > 0xFF ? 0xFF: DataSize - DataIndex;
			memcpy(FileIndexBytes, &FileIndex, sizeof(FileIndexBytes));
			apducmdSetP1P2Data(UpdateCommand, FileIndexBytes[1], FileIndexBytes[0], Portion, Data + DataIndex);
			if (!apducmdExecute(Reader, UpdateCommand, Response, ResponseSize)) // записываем данные
				break;
			FileIndex += Portion;
			DataIndex += Portion;
		}
	}

	return DataIndex == DataSize;
}

// парсим данные карты
static void OPS2ParseCardDataReaded(word DataSize, byte *Data)
{
	tlvReader tr;
    tlvrInit(&tr, Data, DataSize);
    byte tag, *value;
    card len;
    if (DataSize > 0) mapPutByte(ofcdHasData, 1);
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag) // на 1 уже не обращаем внимания - мы его увидели раньше
    	{
			case cdtCRCData: mapPutCard(ofcdCRCReaded, tlv2card(value)); break;
			case cdtType: mapPutByte(ofcdType, value[0]); break;
			case cdtState: mapPutByte(ofcdState, value[0]); break;
			case cdtOwnerID: mapPutCard(ofcdOwnerID, tlv2card(value)); break;
			case cdtPINBlock: mapPut(ofcdPINBlock, value, len); break;
			case cdtHasPIN: break; // skip
			case cdtStatus: mapPutCard(ofcdStatus, tlv2card(value)); break;
			case cdtDiscountPercent: mapPutCard(ofcdDiscountPercent, tlv2card(value)); break;
			case cdtCurrencyID: mapPutCard(ofcdCurrencyIDLocal, tlv2card(value)); break;
			case cdtCurrencyIntCode: mapPutWord(ofcdCurrencyIntCode, tlv2word(value)); break;
			case cdtDateTime: mapPutCard(ofcdDateTime, tlv2card(value)); break;
			case cdtCRCLimits: mapPutCard(ofcdLimitsCRCReaded, tlv2card(value)); break;
    	}
    	memFree(value); // FREE MEMORY
    }

#ifdef OFFLINE_CARD_TEST
    qprintS("CARD DATA\n");
    qprintS("CRC: %u\n", mapGetCardValue(ofcdCRCReaded));
    qprintS("Type: %u\n", mapGetByteValue(ofcdType));
    qprintS("Stat: %u\n", mapGetByteValue(ofcdState));
    qprintS("Ownr: %u\n", mapGetCardValue(ofcdOwnerID));
    qprintS("Sta: %u\n", mapGetCardValue(ofcdStatus));
    qprintS("Disc: %u\n", mapGetCardValue(ofcdDiscountPercent));
    qprintS("CID: %u\n", mapGetCardValue(ofcdCurrencyIDLocal));
    qprintS("CIN: %u\n", mapGetWordValue(ofcdCurrencyIntCode));
    qprintS("LDT: %u\n", mapGetCardValue(ofcdDateTime));
    qprintS("CRCL: %u\n", mapGetCardValue(ofcdLimitsCRCReaded));*/
    printWait();
#endif
}

// Формирует пакет данных для записи в файл на карте. в tlvWriter.DataSize возвращает размер этих данных.
// Caller should init tlvWriter & free memory after use
static void OPS2BuildPrivateCardData(tlvWriter *tw)
{
	tlvwAppendCard(tw, cdtCRCData, ZVL(mapGetCardValue(ofcdCRCReceived), mapGetCardValue(ofcdCRCReaded)));
	tlvwAppendByte(tw, cdtType, mapGetByteValue(ofcdType));
	tlvwAppendByte(tw, cdtState, mapGetByteValue(ofcdState));
	tlvwAppendCard(tw, cdtOwnerID, mapGetCardValue(ofcdOwnerID));
	tlvwAppendKey(tw, cdtPINBlock, lenPINBlock, ofcdPINBlock);
	tlvwAppendByte(tw, cdtStatus, mapGetCardValue(ofcdStatus));
	tlvwAppendCard(tw, cdtDiscountPercent, mapGetCardValue(ofcdDiscountPercent));
	tlvwAppendCard(tw, cdtCurrencyID, mapGetCardValue(ofcdCurrencyIDLocal));
	tlvwAppendWord(tw, cdtCurrencyIntCode, mapGetWordValue(ofcdCurrencyIntCode));
	tlvwAppendCard(tw, cdtDateTime, mapGetCardValue(ofcdDateTime));
	tlvwAppendCard(tw, cdtCRCLimits, ZVL(mapGetCardValue(ofcdLimitsCRCReceived), mapGetCardValue(ofcdLimitsCRCReaded)));
}

// Creates a data packet, which contains public part of card data. tlvWriter.DataSize contains data size
static void OPS2BuildPublicCardData(tlvWriter *tw)
{
	tlvwAppendByte(tw, cdtHasPIN, mapIsEmpty(ofcdPINBlock, lenPINBlock) ? 0 : 1);
	tlvwAppendByte(tw, cdtType, mapGetByteValue(ofcdType));
}

// парсим лимиты карты
static void OPS2ParseCardLimitReaded(int Index, word DataSize, byte *Data)
{
	tlvReader tr;
    tlvrInit(&tr, Data, DataSize);
    byte tag, *value;
    card len;

    mapMove(ofclBeg, Index); // переключаемся за указанную запись для записи лимитов в неё
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
		value[len] = 0; // trailing zero for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag) // на 1 уже не обращаем внимания - мы его увидели раньше
    	{
			case cltID: mapPutCard(ofclID, tlv2card(value)); break;
			case cltPurseID: mapPutCard(ofclPurseIDLocal, tlv2card(value)); break;
			case cltPurseType: mapPutByte(ofclPurseType, value[0]); break;
			case cltPurseName: if (len != 0) mapPut(ofclPurseName, value, len + 1); break;
			case cltDuration: mapPutByte(ofclDuration, value[0]); break;
			case cltLimit: mapPutCard(ofclLimit, tlv2card(value)); break;
			case cltFlags: mapPutByte(ofclFlags, value[0]); break;
			case cltConsumption: mapPutCard(ofclConsumptionInitial, tlv2card(value)); mapPutCard(ofclConsumptionChange, 0); break;
    	}
    	memFree(value); // FREE MEMORY
    }

#ifdef OFFLINE_CARD_TEST
    qprintS("LIMIT\n");
    qprintS("ID: %u\n", mapGetCardValue(ofclID));
    qprintS("Purs: %u\n", mapGetCardValue(ofclPurseIDLocal));
    qprintS("Type: %u\n", mapGetByteValue(ofclPurseType));

    char PurseName[lenPurseName];
    mapGet(ofclPurseName, PurseName, lenPurseName);
    qprintS("Name: %s\n", PurseName);

    qprintS("Dura: %u\n", mapGetByteValue(ofclDuration));
    qprintS("Limi: %u\n", mapGetCardValue(ofclLimit));
    qprintS("Flag: %u\n", mapGetByteValue(ofclFlags));
    qprintS("Cons: %d\n", (int)mapGetCardValue(ofclConsumptionInitial));*/
    printWait();
#endif
}

// парсим лимиты карты
static void OPS2ParseCardLimitsListReaded(word DataSize, byte *Data)
{
	tlvReader tr;
    tlvrInit(&tr, Data, DataSize);
    byte tag, *value;
    card len;
    word LimitsCount = 0;

    if (DataSize > 0) mapPutByte(ofcdHasLimits, 1); // сам факт того, что мы сюда пришли означает, что данные по лимитам есть на карте
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag) // на 1 уже не обращаем внимания - мы его увидели раньше
    	{
    		case clltLimit:
    			OPS2ParseCardLimitReaded(LimitsCount, len, value);
    			LimitsCount++;
    			break;
    	}
    	memFree(value); // FREE MEMORY
    }
    mapPutWord(ofcdLimitsCount, LimitsCount);
}

// формирует пакет для текущего лимита
static void OPS2BuildCardLimit(tlvWriter *tw)
{
	tlvwAppendKeyCard(tw, cltID, ofclID);
	tlvwAppendKeyCard(tw, cltPurseID, ofclPurseIDLocal);
	tlvwAppendKeyByte(tw, cltPurseType, ofclPurseType);

	char PurseName[lenPurseName];
	mapGet(ofclPurseName, PurseName, lenPurseName);
	tlvwAppendString(tw, cltPurseName, PurseName);
	tlvwAppendKeyByte(tw, cltDuration, ofclDuration);
	tlvwAppendKeyCard(tw, cltLimit, ofclLimit);
	tlvwAppendKeyByte(tw, cltFlags, ofclFlags);
	tlvwAppendCard(tw, cltConsumption, oclConsumption());
}

// Формирует пакет данных для записи лимитов в файл на карте. в tlvWriter.DataSize возвращает размер этих данных.
// Caller should init tlvWriter & free memory after use
static void OPS2BuildCardLimits(tlvWriter *tw)
{
	// вот тут записываем данные по изменившимся лимитам на карту
	// Записываем лимиты в чек
	int i = 0, Count = mapGetWordValue(ofcdLimitsCount);
	for (i = 0; i < Count; i++)
	{
		mapMove(ofclBeg, i); // переходим на запись
		tlvWriter LimitTW;
		tlvwInit(&LimitTW);
		OPS2BuildCardLimit(&LimitTW);
		if (LimitTW.DataSize > 0)
			tlvwAppend(tw, clltLimit, LimitTW.DataSize, LimitTW.Data);
		tlvwFree(&LimitTW);
	}
}


static byte OPS2ApplicationID[] = { 0x52, 0x6f, 0x73, 0x54, 0x6f, 0x70, 0x53, 0x6f, 0x79, 0x75, 0x7a, 0x01 };

// читаем данные с карты типа OPS2
static Bool OPS2ReadCardData()
{
	byte Reader = mapGetByteValue(cfgChipReader);

	Bool Result = FALSE;
	if (iccStart(Reader) > 0)
	{
		int ResponseSize;
		byte Response[ICC_RESPONSE_SIZE];
		byte FileNameUID[] = { 0x2F, 0x00 }, FileNamePrivateCardData[] = { 0x2F, 0x02 }, FileNameCardLimits[] = { 0x2F, 0x03 },
			 ADM1[8];

		tAPDUCommand SelectApplicationCommand, SelectFileCommand, ReadCommand, VerifyCommand;
		apducmdInit(&SelectApplicationCommand, apductHasInputNoOutput, 0x00, 0xA4, 0x04, 0x00, sizeof(OPS2ApplicationID), OPS2ApplicationID, 0x00);
		apducmdInit(&SelectFileCommand, apductHasInputNoOutput, 0x00, 0xA4, 0x00, 0x00, 0x00, NULL, 0x00);
		apducmdInit(&ReadCommand, apductNoInputHasOutput, 0x00, 0xB0, 0x00, 0x00, 0x00, NULL, 0x00);
		apducmdInit(&VerifyCommand, apductHasInputNoOutput, 0x00, 0x20, 0x00, OPS2_KEY_ADM1, 0x08, NULL, 0x00);

		if (iccCommand(Reader, NULL, NULL, Response) > 0) // power on
			if (apducmdExecute(Reader, &SelectApplicationCommand, Response, &ResponseSize)) // выбрали приложение
			{
				apducmdSetData(&SelectFileCommand, sizeof(FileNameUID), FileNameUID);
				if (apducmdExecute(Reader, &SelectFileCommand, Response, &ResponseSize)) // выбрали файл с UID
				{
					ReadCommand.Le = 16;
					if (apducmdExecute(Reader, &ReadCommand, Response, &ResponseSize)) // прочитали UID из файла
						if (ResponseSize > ReadCommand.Le)
						{
							OPS2GetKey(OPS2_KEY_ADM1, Response, ADM1);
							apducmdSetData(&VerifyCommand, sizeof(ADM1), ADM1);
							if (apducmdExecute(Reader, &VerifyCommand, Response, &ResponseSize)) // аутентификация на чтение
							{
								mapPutByte(ofcdCanStore, 1); // если успешно авторизовались на чтение карты, то карта может хранить offline-данные
								// Now we can read the first part of card data (private)
								apducmdSetData(&SelectFileCommand, sizeof(FileNamePrivateCardData), FileNamePrivateCardData);
								if (apducmdExecute(Reader, &SelectFileCommand, Response, &ResponseSize)) // выбираем файл с данными карты
								{
									apducmdSetP1P2Le(&ReadCommand, 0, 0, sizeof(tChipFileHeader)); // читаем sizeof байт с начала файла
									if (apducmdExecute(Reader, &ReadCommand, Response, &ResponseSize)) // читаем заголовок файла с данными карты
									{
										tChipFileHeader FileHeader;
										memcpy(&FileHeader, Response, ReadCommand.Le); // преобразуем массив байт в хидер
										if (FileHeader.Size > 0) // если есть данные, то продолжаем. если нет - то и лимиты не читаем
										{
											byte *FileData = memAllocate(FileHeader.Size);
											if (OPS2ReadChipFile(Reader, &ReadCommand, Response, &ResponseSize, FileHeader.Size, FileData))
												OPS2ParseCardDataReaded(FileHeader.Size, FileData); // парсим данные карты
											memFree(FileData);
											Result = TRUE; // данные есть на карте

											apducmdSetData(&SelectFileCommand, sizeof(FileNameCardLimits), FileNameCardLimits);
											if (apducmdExecute(Reader, &SelectFileCommand, Response, &ResponseSize)) // выбираем файл с лимитами
											{
												apducmdSetP1P2Le(&ReadCommand, 0, 0, sizeof(tChipFileHeader)); // читаем sizeof байт с начала файла
												if (apducmdExecute(Reader, &ReadCommand, Response, &ResponseSize)) // читаем заголовок файла с лимитами
												{
													memcpy(&FileHeader, Response, ReadCommand.Le); // преобразуем массив байт в хидер
													if (FileHeader.Size > 0) // если есть лимиты, то продолжаем
													{
														FileData = memAllocate(FileHeader.Size);
														if (OPS2ReadChipFile(Reader, &ReadCommand, Response, &ResponseSize, FileHeader.Size, FileData))
															OPS2ParseCardLimitsListReaded(FileHeader.Size, FileData); // парсим данные карты
														memFree(FileData);
													}
												}
											}
										}
									}
								}
							}
						}
				}
			}
	}
	iccStop(Reader);
	return Result;
}

// В зависимости от типа карты (из opsCard), читаем или нет аварийные данные
// Возвращает TRUE, если данные карты были прочитаны
Bool ocdReadCardData(int ChipCardType)
{
	Bool Result = FALSE;

	traResetOfflineCardData();
    switch (ChipCardType)
    {
    	case phctOPS2:
			{
				//mapPutByte(ofcdCanStore, 1); // ВРЕМЕННО
				//unsigned long ticks1 = get_tick_counter();
				Result = OPS2ReadCardData();
				//printS("ReadTime: %d\n", get_tick_counter() - ticks1);
			}
    		break;
    	default:
    		// фейк!!!
#ifdef OFFLINE_CARD_TEST
    		Result = TRUE;

    		mapPutCard(ofcdCRCReaded, 0xA1A4DC93); // fake-crc для передачи на сервер
    		mapPutCard(ofcdLimitsCRCReaded, 0x0);

    		mapPutByte(ofcdType, ctFuel);
    		mapPutByte(ofcdState, csAllowed); // карта работает
    		mapPutCard(ofcdOwnerID, 1); // владелец карты == 1
    		mapPut(ofcdPINBlock, "\x4E\x84\x8F\xF6\x78\x31\x10\x1D", 8);
    		mapPutCard(ofcdStatus, 0); // неизвестный статус
    		mapPutCard(ofcdDiscountPercent, 1000); // скидка 10%
    		mapPutCard(ofcdCurrencyIDLocal, 1); // валюта карты с кодом 1
    		mapPutWord(ofcdCurrencyIntCode, 643);

    		mapPutCard(ofcdDateTime, utGetDateTime() - 1*(24*60*60)); // Дата и время последнего обслуживания по карте (2 дня назад)

    		int LimitCounter = 0;
    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 18); // лимиты
    		mapPutCard(ofclPurseIDLocal, 16);
    		mapPutByte(ofclPurseType, ptyProduct);
    		mapPut(ofclPurseName, "\xB3\xD0\xD7", 3); // Газ
    		mapPutByte(ofclDuration, cldWeek);
    		mapPutCard(ofclLimit, 100 * 1000);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 1000);

    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 4);
    		mapPutCard(ofclPurseIDLocal, 20);
    		mapPutByte(ofclPurseType, ptyProductGroup);
    		mapPut(ofclPurseName, "\xC2\xDE\xDF\xDB\xD8\xD2\xDE", 7); // Топливо
    		mapPutByte(ofclDuration, cldMonth);
    		mapPutCard(ofclLimit, 200 * 1000);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 1000);

    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 3);
    		mapPutCard(ofclPurseIDLocal, 12);
    		mapPutByte(ofclPurseType, ptyProduct);
    		mapPut(ofclPurseName, "\xB0\xD8\x2D\x39\x32", 5); // Аи-92 Н
    		mapPutByte(ofclDuration, cldWeek);
    		mapPutCard(ofclLimit, 3000 * 1000);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 1000);

    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 3);
    		mapPutCard(ofclPurseIDLocal, 12);
    		mapPutByte(ofclPurseType, ptyProduct);
    		mapPut(ofclPurseName, "\xB0\xD8\x2D\x39\x32", 5); // Аи-92 С
    		mapPutByte(ofclDuration, cldDaily);
    		mapPutCard(ofclLimit, 300 * 1000);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 1000);

    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 16);
    		mapPutCard(ofclPurseIDLocal, 15);
    		mapPutByte(ofclPurseType, ptyProduct);
    		mapPut(ofclPurseName, "\xB4\xC2", 2); // ДТ
    		mapPutByte(ofclDuration, cldDaily);
    		mapPutCard(ofclLimit, 100 * 1000);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 101);

    		mapMove(ofclBeg, LimitCounter); LimitCounter++;
    		mapPutCard(ofclID, 18);
    		mapPutCard(ofclPurseIDLocal, 1);
    		mapPutByte(ofclPurseType, ptyCurrency);
    		mapPut(ofclPurseName, "\xC0\xE3\xD1\xDB\xD8", 5); // Рубли
    		mapPutByte(ofclDuration, cldMonth);
    		mapPutCard(ofclLimit, 1000 * 100);
    		mapPutByte(ofclFlags, clfVisible);
    		//mapPutCard(ofclConsumptionInitial, 101);

    		mapPutWord(ofcdLimitsCount, LimitCounter); // количество лимитов
    		mapPutByte(ofcdHasLimits, 1); // лимиты с карты прочитаны
#else
    		Result = FALSE;
#endif
    		break;
    }

    if (ocdCanStore())
    	mapPutByte(ofcdChipCardType, (byte)ChipCardType); // сохраняем тип карты, с которой эта информация была прочитана

    if (Result) // TRUE, если на карте есть аварийные данные
    {
		#ifdef OFFLINE_ALLOWED
  			ocdRecodeIDs(); // заполняем поля ofcdCurrencyID и ofclPurseID
		#endif
		oclAnalyzeForReset(); // обнуляем просроченные расходы по лимитам
    }

	return Result;
}

// Передаётся открытый ридер для чтения карты
static Bool OPS2UpdateCardData(byte Reader, byte *Response, int *ResponseSize)
{
	Bool Result = FALSE, NeedUpdateCardData = ocdNeedUpdate(), NeedUpdateCardLimits = ocdLimitsNeedUpdate();

	if (NeedUpdateCardData || NeedUpdateCardLimits)
	{

		byte FileNameUID[] = { 0x2F, 0x00 }, FileNamePrivateCardData[] = { 0x2F, 0x02 }, FileNamePublicCardData[] = { 0x2F, 0x04 },
			FileNameCardLimits[] = { 0x2F, 0x03 }, ADM2[8];

		tAPDUCommand SelectApplicationCommand, SelectFileCommand, ReadCommand, VerifyCommand, UpdateCommand;
		apducmdInit(&SelectApplicationCommand, apductHasInputNoOutput, 0x00, 0xA4, 0x04, 0x00, sizeof(OPS2ApplicationID), OPS2ApplicationID, 0x00);
		apducmdInit(&SelectFileCommand, apductHasInputNoOutput, 0x00, 0xA4, 0x00, 0x00, 0x00, NULL, 0x00);
		apducmdInit(&ReadCommand, apductNoInputHasOutput, 0x00, 0xB0, 0x00, 0x00, 0x00, NULL, 0x00);
		apducmdInit(&VerifyCommand, apductHasInputNoOutput, 0x00, 0x20, 0x00, OPS2_KEY_ADM2, 0x08, NULL, 0x00);
		apducmdInit(&UpdateCommand, apductNoInputHasOutput, 0x00, 0xD6, 0x00, 0x00, 0x00, NULL, 0x00);

		// выбор приложения и авторизация на карте
		Result = FALSE;
		if (apducmdExecute(Reader, &SelectApplicationCommand, Response, ResponseSize)) // выбрали приложение
		{
			apducmdSetData(&SelectFileCommand, sizeof(FileNameUID), FileNameUID);
			if (apducmdExecute(Reader, &SelectFileCommand, Response, ResponseSize)) // выбрали файл с UID
			{
				ReadCommand.Le = 16;
				if (apducmdExecute(Reader, &ReadCommand, Response, ResponseSize)) // прочитали UID из файла
					if (*ResponseSize > ReadCommand.Le)
					{
						OPS2GetKey(OPS2_KEY_ADM2, Response, ADM2);
						apducmdSetData(&VerifyCommand, sizeof(ADM2), ADM2);
						if (apducmdExecute(Reader, &VerifyCommand, Response, ResponseSize)) // аутентификация на чтение
						{
							Result = TRUE;
						}
					}
			}
		}

		if (Result)
		{
			Result = FALSE;
			tlvWriter tw;
			if (NeedUpdateCardData) // обновляем основные данные карты
			{
				// update private card data
				apducmdSetData(&SelectFileCommand, sizeof(FileNamePrivateCardData), FileNamePrivateCardData);
				if (apducmdExecute(Reader, &SelectFileCommand, Response, ResponseSize)) // выбрали файл с данными карты
				{
					tlvwInit(&tw);
					OPS2BuildPrivateCardData(&tw); // заполняет tlvWriter данными карты для обновления
					if (tw.DataSize > 0)
						Result = OPS2WriteChipFile(Reader, &UpdateCommand, Response, ResponseSize, tw.DataSize, tw.Data);
					tlvwFree(&tw);
				}

				// update public info about the card
				apducmdSetData(&SelectFileCommand, sizeof(FileNamePublicCardData), FileNamePublicCardData);
				if (apducmdExecute(Reader, &SelectFileCommand, Response, ResponseSize)) // выбрали файл с данными карты
				{
					tlvwInit(&tw);
					OPS2BuildPublicCardData(&tw); // заполняет tlvWriter данными карты для обновления
					if (tw.DataSize > 0)
						Result = OPS2WriteChipFile(Reader, &UpdateCommand, Response, ResponseSize, tw.DataSize, tw.Data);
					tlvwFree(&tw);
				}
			}

			if (NeedUpdateCardLimits) // перезаписываем лимиты карты, стараясь при этом сохранить расходы по лимиту
			{
				apducmdSetData(&SelectFileCommand, sizeof(FileNameCardLimits), FileNameCardLimits);
				if (apducmdExecute(Reader, &SelectFileCommand, Response, ResponseSize)) // выбрали файл с лимитами карты
				{
					tlvwInit(&tw);
					OPS2BuildCardLimits(&tw); // заполняет tlvWriter аварийными лимитами карты для обновления данных на карте
					if (tw.DataSize > 0)
						Result = OPS2WriteChipFile(Reader, &UpdateCommand, Response, ResponseSize, tw.DataSize, tw.Data);
					tlvwFree(&tw);
				}
			}
		}
	}
	return Result;
}

// Вызывается во всех режимах:
// a) когда были получены обновленные данные по лимитам карты и новые характиристики карты
// б) когда лимиты были использованы для покупки и теперь нужно обновить offline-расход по лимитам
Bool ocdUpdateCardData()
{
	Bool Result = FALSE;
	if (!ocdCanStore()) return Result; // если карта не может хранить аварийную информацию - выходим

	mapPutCard(ofcdDateTime, ZVL(mapGetCardValue(traDateTimeUnix), utGetDateTime())); // дата последнего обслуживания по карте берется из транзакции или текущая

	//unsigned long ticks1 = get_tick_counter();
	byte Reader = mapGetByteValue(cfgChipReader);
	Result = iccStart(Reader) > 0; // открытие ридера и авторизация на карте для записи
	if (Result)
	{
		int ResponseSize;
		byte Response[ICC_RESPONSE_SIZE];

		Result = iccCommand(Reader, NULL, NULL, Response) > 0; // PowerOn
		if (Result)
		{
			byte ChipCardType = mapGetByteValue(ofcdChipCardType);
			switch (ChipCardType)
			{
				case phctOPS2:
					Result = OPS2UpdateCardData(Reader, Response, &ResponseSize);
					break;
				default:
					printS("Chip card type (%d) does not supported in ocdUpdateCardData()\n", ChipCardType);
					break;
			}
		}
	}
	iccStop(Reader); // закрытие ридера
	//printS("UpdTime: %d\n", get_tick_counter() - ticks1);
	return Result;
}

// TRUE, если расход по текущему лимиту был изменен в течение транзакции
Bool oclHasChanges()
{
	return mapGetIntegerValue(ofclConsumptionChange) != 0;
}

#ifdef OFFLINE_ALLOWED

Bool ocdHasPIN()
{
	if (ocdHasData())
		return !mapIsEmpty(ofcdPINBlock, lenPINBlock);
	else
		return FALSE;
}

// Возвращает текстовую метку продолжительности текущего лимита
// Размер строки - 2 байта
void oclDurationSign(char *DurationSign)
{
	clcGetLimitDurationSign(mapGetByteValue(ofclDuration), DurationSign);
}

// Возвращает текстовую продолжительность текущего лимита
// Размер строки - 15 байт
void oclDurationString(char *DurationString)
{
	clcGetLimitDurationString(mapGetByteValue(ofclDuration), DurationString);
}

#endif

// Возвращает текущий расход по лимиту
int oclConsumption()
{
	return mapGetIntegerValue(ofclConsumptionInitial) + mapGetIntegerValue(ofclConsumptionChange);
}

// Возвращает доступное количество текущего лимита карты
int oclAvaivable()
{
	return mapGetCardValue(ofclLimit) - oclConsumption();
}

// Увеличивает текущий расход по лимиту на указанную величину
void oclIncrease(int ChangeValue)
{
	mapPutCard(ofclConsumptionChange, mapGetIntegerValue(ofclConsumptionChange) + ChangeValue);
}

// обнуляет расход по текущему лимиту карты
void oclResetConsumption()
{
	oclIncrease(-oclConsumption());
}

// Анализирует текущие лимиты карты для необходимости обнуления расхода
void oclAnalyzeForReset()
{
	card CurrentDate = utExtractDate(utGetDateTime());
	card LastDate = utExtractDate(mapGetCardValue(ofcdDateTime));
	int i = 0, Count = mapGetWordValue(ofcdLimitsCount);
	for (i = 0; i < Count; i++)
	{
		mapMove(ofclBeg, i); // переходим на запись
		if (oclConsumption() != 0) // если есть расход по лимиту
		{
			cmpCardLimitDurations Duration = mapGetByteValue(ofclDuration);
			Bool AllowReset = FALSE;
			switch (Duration)
			{
				case cldNonRenewable: break; // не обнуляем расход по единовременному лимиту
				case cldDaily: AllowReset = CurrentDate > LastDate; break;
				case cldWeek: AllowReset = utGetFirstDayOfWeek(CurrentDate) > utGetFirstDayOfWeek(LastDate); break;
				case cldMonth: AllowReset = utGetFirstDayOfMonth(CurrentDate) > utGetFirstDayOfMonth(LastDate); break;
				case cldQuarter: AllowReset = utGetFirstDayOfQuarter(CurrentDate) > utGetFirstDayOfQuarter(LastDate); break;
				case cldYear: AllowReset = utGetFirstDayOfYear(CurrentDate) > utGetFirstDayOfYear(LastDate); break;
				default:
					printS("ALERT! Неизвестная продолжительность действия лимита: %d\n", Duration);
					break;
			}
			if (AllowReset)
				oclResetConsumption();
		}
	}

}

// возвращает количество изменённых лимитов, которые сохраняются в offline-архиве
int oclChangedCount()
{
	int i = 0, Count = mapGetWordValue(ofcdLimitsCount), Result = 0;
	for (i = 0; i < Count; i++)
	{
		mapMove(ofclBeg, i); // переходим на запись
		if (oclHasChanges())
			Result++;
	}
	return Result;
}

#ifdef OFFLINE_ALLOWED

// добавляет состояние лимитов текущей offline-транзакции в offline-архив (используется при отмене транзакции)
Bool oclAddToArchive(char *ArchiveName, byte *searchID)
{
	int ItemSize = 1+1+4 + 1+1+1 + 1+1+4; // PurseIDLocal, Duration, ChangeValue
	int ChangedCount = oclChangedCount(), DataSize = (1+2)*ItemSize*ChangedCount;
	if (ChangedCount == 0) return TRUE;

	// формируем буфер для записи
	tBuffer DataBuffer;
	byte Data[DataSize];
	bufInit(&DataBuffer, Data, DataSize);

	int i = 0, Count = mapGetWordValue(ofcdLimitsCount);
	for (i = 0; i < Count; i++)
	{
		mapMove(ofclBeg, i); // переходим на запись
		if (oclHasChanges())
		{
			tBuffer ItemBuffer;
			byte Item[ItemSize];
			bufInit(&ItemBuffer, Item, ItemSize);

			bufAppTLVCard(&ItemBuffer, scltPurseID, mapGetCardValue(ofclPurseIDLocal));
			bufAppTLVByte(&ItemBuffer, scltDuration, mapGetByteValue(ofclDuration));
			bufAppTLVInt(&ItemBuffer, ocltConsumptionChange, mapGetCardValue(ofclConsumptionChange));

			bufAppTLV(&DataBuffer, sclltLimit, ItemBuffer.pos, ItemBuffer.ptr);
		}
	}

	tDataFile df;
	Bool Result = dfInit(&df, ArchiveName);
	if (dfOpen(&df))
		Result = dfAdd(&df, DataBuffer.ptr, DataBuffer.pos, searchID); // сохраняем буфер для дальнейшей передачи в ПЦ
	dfClose(&df);

	return Result;
}

#endif
