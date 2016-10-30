/*
 * opsUpdateSoftware.c
 *
 *  Created on: 05.07.2011
 *      Author: Pavel
 */
// Обновление ПО и конфигурации. ПО загружается в каталог SWAP, конфигурация и прочие файлы в HOST. Активация делается для всех дисков, кроме SWAP
#include <stdio.h>
#include "SDK30.h"
#include "log.h"

#define UPD_MAX_FILES_COUNT 90
#define UPD_MAX_DISKS_COUNT 2

typedef struct
{
	char FileName[lenFileName]; // file name for updating
	char DiskName[lenDiskName]; // disk name for storage file
	card FileSize; // size of file
} updFile_t;

typedef struct
{
	char DiskName[lenDiskName]; // disk name
	card DiskSize; // size of disk
} updDisk_t;

typedef struct
{
	updFile_t Files[UPD_MAX_FILES_COUNT];
	updDisk_t Disks[UPD_MAX_DISKS_COUNT];

	S_FS_FILE *CurrentFile; // указатель на текущий файл
	word CurrentIndex; // ожидаемый файл
	word CurrentPart; // ожидаемая часть файла
	card CurrentFileBytesWrited; // сколько байт из текущего файла записаны на диск

	card TotalBytesWrited; // сколько всего записано на диск
	word FilesCount; // количество файлов
	card FilesTotalSize; // размер файлов
	word DisksCount; // количество дисков
} updInfo_t;

updInfo_t *UpdateInfo = NULL;

static void updDisplayInitial()
{
	trcS("updDisplayInitial()");
	dspClear();
	displayLS8859(0, "Загрузка..."); // Обновление ПО
	displayLS8859(1, "Всего  %d/%dКб", UpdateInfo->FilesCount, UpdateInfo->FilesTotalSize/1024); // Всего  88/9999Кб
}

static void updDisplayStatus()
{
	trcS("updDisplayStatus()");
	displayLS8859(2, "Статус %d/%dКб", UpdateInfo->CurrentIndex + 1, UpdateInfo->TotalBytesWrited/1024); // Статус 88/9999Кб
	//displayLS(3, "\x20\x20\x20\x20\x20\x20 %d%%", updTotalBytesWrited*100/updFilesTotalSize); //      100%
}

// получаем размер флешдиска
card flshGetSize()
{
	return FS_AreaGetSize(FS_WO_ZONE_DATA);
}

// получаем свободное место на флешдиске
card flshGetFree()
{
	return FS_AreaGetFreeSize(FS_WO_ZONE_DATA);

	/*S_FS_DIAG diag;
	memset(&diag, 0, sizeof(diag));
	FS_Diag(&diag);
	card DeviceFree = 0, i;
	for (i = 0; i < NB_AREA_DEFINE; i++)
	{
		printS("%d: %d\n", i, diag.Area[i].Free);
		DeviceFree += diag.Area[i].Free;
	}
	return DeviceFree;*/
}

static Bool updMountDisk(char *Name, card Size)
{
	trcS("updMountDisk()");
    unsigned int AccessMode;
    S_FS_PARAM_CREATE CreateParam;
    char SlashName[lenDiskName+1];

    // check flash disk availible size
    card FlashFree = flshGetFree();
    if (Size > FlashFree) return FALSE;

    sprintf(SlashName, "/%s", Name);
    FS_unmount(SlashName); // mask any errors
    int ret = FS_mount(SlashName, &AccessMode);
    if (ret == FS_OK)
    	return TRUE;
    else
    {
        strcpy(CreateParam.Label, Name);
        CreateParam.Mode = FS_WRITEONCE;
        CreateParam.AccessMode = FS_WRTMOD;
        CreateParam.NbFichierMax = 10 + UPD_MAX_FILES_COUNT;
        CreateParam.IdentZone = FS_WO_ZONE_DATA; // нужно создавать диски только в зоне DATA!!! Перед созданием контролировать её FreeSize
        if (FS_dskcreate(&CreateParam, &Size) == FS_OK)
        	if (FS_mount(SlashName, &AccessMode) == FS_OK)
        		return TRUE;
    }
    qprintS("Disk: %s\n", Name);
    qprintS("Disk size: %u\n", Size);
    qprintS("Create&Mount failed\n");
    printWait();
    return FALSE;
}

static Bool updCreateAndMountDisks()
{
	trcS("updCreateAndMountDisks()");
	int i;
	Bool result = TRUE;
	for (i = 0; i < UpdateInfo->DisksCount; i++)
		result &= updMountDisk(UpdateInfo->Disks[i].DiskName, UpdateInfo->Disks[i].DiskSize*1.1);
	return result;
}

static Bool updUnmountAndKillDisks()
{
	trcS("updUnmountAndKillDisks()");
	int i;
	Bool result = TRUE;
	for (i = 0; i < UpdateInfo->DisksCount; i++)
	{
	    char SlashName[lenDiskName+1];
	    sprintf(SlashName, "/%s", UpdateInfo->Disks[i].DiskName);
		result &= FS_unmount(SlashName) == FS_OK;
		if (strcmp(SlashName, "/SWAP") == 0)
			result &= FS_dskkill(SlashName) == FS_OK;
	}
	return result;
}

static Bool updDiskExists(char *DiskName)
{
	trcS("updDiskExists()");
	int i;
	for (i = 0; i < UpdateInfo->DisksCount; i++)
		if (strcmp(UpdateInfo->Disks[i].DiskName, DiskName) == 0)
			return TRUE;
	return FALSE;
}

/*static void PrintFilesForUpdate()
{
	int i;
    card TotalSize = 0;
    for (i = 0; i < UpdateInfo->FilesCount; i++)
    {
    	qprintS("Name: /%s/%s\n", UpdateInfo->Files[i].DiskName, UpdateInfo->Files[i].FileName);
    	qprintS("Size: %u\n", UpdateInfo->Files[i].FileSize);
    	TotalSize+=UpdateInfo->Files[i].FileSize;
    }
    qprintS("Total: %u\n", TotalSize);
    printWait();
}*/

static Bool updCreateNewFile()
{
	trcS("updCreateNewFile()");
	char FullFileName[lenFileName+lenDiskName+3];
	sprintf(FullFileName, "/%s/%s", UpdateInfo->Files[UpdateInfo->CurrentIndex].DiskName, UpdateInfo->Files[UpdateInfo->CurrentIndex].FileName);

	FS_unlink(FullFileName); // пытаемся удалить файл пере открытием
	UpdateInfo->CurrentFile = NULL;
	UpdateInfo->CurrentFile = FS_open(FullFileName, "a");
	Bool result = UpdateInfo->CurrentFile != NULL;
	if (!result)
	{
		displayLS(3, "                            ");
		displayLS(3, FullFileName);
		usrInfo(infFileCreationError);
	}
	return result;
}

static Bool updCloseCurrentFile()
{
	trcS("updCloseCurrentFile()");
	Bool result = TRUE;
	if (UpdateInfo->CurrentFile)
	{
		result = FS_close(UpdateInfo->CurrentFile) == FS_OK;
		if (result)
			UpdateInfo->CurrentFile = NULL;
	}
	return result;
}

void updInitialize()
{
	trcS("updInitialize()");
	UpdateInfo = memAllocate(sizeof(updInfo_t));
    memset(UpdateInfo, 0, sizeof(updInfo_t)); // готовим таблицу для сохранения списка файлов
}

void updDeinitialize()
{
	trcS("updDeinitialize()");
	updCloseCurrentFile();
	updUnmountAndKillDisks();
	memFree(UpdateInfo);
	UpdateInfo = NULL;
}

static Bool updSendGetNextFile()
{
	trcS("updSendGetNextFile()");
	// сбрасываем переменные
	UpdateInfo->CurrentFileBytesWrited = 0;
	UpdateInfo->CurrentPart = 0;
    // отправляем запрос на получение следующеЙ части
	word MessageSize = 1+1+1 + 1+1+2; // packet + file index
	byte Message[MessageSize];
	tBuffer sendbuf;
	bufInit(&sendbuf, Message, MessageSize);
	bufAppTLVByte(&sendbuf, srtPackageID, pcpNextPartOfFile);
	bufAppTLVWord(&sendbuf, srtFileIndex, UpdateInfo->CurrentIndex);
	return opsSendMessage(cslotPC, sendbuf.ptr, sendbuf.pos);
}

static int updDiskIndexByName(const char *DiskName)
{
	int i;
	for (i = 0; i < UpdateInfo->DisksCount; i++)
		if (strcmp(UpdateInfo->Disks[i].DiskName, DiskName) == 0)
			return i;
	return -1;
}

// разбирает сообщение с файлом и вставляет его в структуре
static void updParseAndInsertSingleFile(tBuffer *buf)
{
	trcS("updParseAndInsertSingleFile()");
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
    	{
			case 0x01: // file name
				if (len != 0)
				{
					value[len] = 0;
					strcpy(UpdateInfo->Files[UpdateInfo->CurrentIndex].FileName, value);
				}
				break;
			case 0x02: // disk name
				if (len != 0)
				{
					value[len] = 0;
					strcpy(UpdateInfo->Files[UpdateInfo->CurrentIndex].DiskName, value);
				}
				break;
			case 0x03: // file size
				UpdateInfo->Files[UpdateInfo->CurrentIndex].FileSize = tlv2card(value);
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }

    // get a size of current file
	if (updDiskExists(UpdateInfo->Files[UpdateInfo->CurrentIndex].DiskName) != TRUE)
	{
		strcpy(UpdateInfo->Disks[UpdateInfo->DisksCount].DiskName, UpdateInfo->Files[UpdateInfo->CurrentIndex].DiskName);
		UpdateInfo->DisksCount++;
	}
	int DiskIndex = updDiskIndexByName(UpdateInfo->Files[UpdateInfo->CurrentIndex].DiskName);
	UpdateInfo->Disks[DiskIndex].DiskSize += UpdateInfo->Files[UpdateInfo->CurrentIndex].FileSize;

	UpdateInfo->FilesTotalSize += UpdateInfo->Files[UpdateInfo->CurrentIndex].FileSize;
}

// ответ за запрос "получить список файлов для обновления"
void pcHandleAnswerFileList(tBuffer *buf, byte *PCState, byte *RSState)
{
	trcS("pcHandleAnswerFileList()");
	// read the buffer
	tBuffer filebuf;
	tlvReader tr;
    tlvrInit(&tr, buf->ptr, buf->dim);
    byte tag;
    card len;
    byte *value;

	*PCState = lssProtocolError;

    updInitialize();
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case srtFileInfo: // file record
				filebuf.ptr = value;
				filebuf.dim = len;
				UpdateInfo->CurrentIndex = UpdateInfo->FilesCount;
				updParseAndInsertSingleFile(&filebuf); // записываем файл в БД и сохраняем общий размер
				UpdateInfo->FilesCount++; // количество сохранённых файлов
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }

    // после получения всех файлов отправляем пакет для получения первого файла
    if (UpdateInfo->FilesCount > 0 || UpdateInfo->DisksCount > 0)
    {
    	// создаём все диски, нужные для обновления
    	if (updCreateAndMountDisks())
    	{
        	// устанавливаем индекс текущего файла в 0
        	UpdateInfo->CurrentIndex = 0;
        	updDisplayInitial();

        	*PCState = updCreateNewFile() && updSendGetNextFile() ? lssPrepareWaitForData : lssProtocolError;
    	}
    	else
    		printS("Cant create disks\n");
    }
    else
    	usrInfo(infNoFilesForUpdate);
    //PrintUpdateFiles();
	if (*PCState == lssPrepareWaitForData) // если начали обновление - выставляем флаг
		mapPutByte(traIsUpdateStarted, 1);
}

static Bool FinalizeUpdate()
{
	trcS("FinalizeUpdate()");
	int i;

	pcDisconnectFromServer();

	Bool result = TRUE;
	for (i = 0; i < UpdateInfo->DisksCount; i++)
		if (strlen(UpdateInfo->Disks[i].DiskName) > 0 && strcmp(UpdateInfo->Disks[i].DiskName, "HOST") != 0)
		{
			displayLS8859(0, "Установка...");
			int ret = SoftwareActivate(UpdateInfo->Disks[i].DiskName);
			result &= ret == 0;
			if (!result)
			{
				qprintS("Activation error %d\n", ret);
				qprintS("on disk %s\n", UpdateInfo->Disks[i].DiskName);
				printWait();
				// читаем TraceFile.lst
				#ifdef __TEST__
				char LogName[lenDiskName+lenFileName+3];
				sprintf(LogName, "/%s/TRACEFILE.LST", UpdateInfo->Disks[i].DiskName);
				S_FS_FILE *LogFile = FS_open(LogName, "r");
				if (LogFile)
				{
					char Buffer[4096];
					int Readed = FS_read(Buffer, sizeof(Buffer), 1, LogFile);
					if (Readed > 0)
					{
						qprintS("TRACEFILE.LST:\n");
						PrintChars(FALSE, Buffer, Readed, TRUE);
						qprintS("\n\n\n");
						printWait();
					}
					else
						printS("Read log error\n\n\n", ret);
				}
				else
					printS("Log not found\n\n\n", ret);
				#endif
			}
		}

	updDeinitialize();
	if (result)
	{
		cfgSave("HOST", "OPSTT.PAR", FALSE, FALSE, TRUE); // сохраняем текущую конфигурацию приложения перед перезагрузкой, если он не был сохранён ранее

		// уведомляем оператора о перезагрузке
		musUpdateSoftware();
	    /*StartBuzzer(0, 0x100, 0x6600); tmrPause(1);
	    StartBuzzer(0, 0x6800, 0x9600); tmrPause(1);
	    StopBuzzer();*/

		dspClear();
                        //123456789012345678901 234
        displayLS8859(0, "Загрузка завершена\n"); //
        displayLS8859(1, "Нажмите любую кнопку\n"); //
        displayLS8859(2, "для перезагрузки.\n"); // кнопку для

        kbdWait(60);

		SystemFioctl(SYS_FIOCTL_SYSTEM_RESTART, NULL); // restart the terminal
		return TRUE;
	}
	return FALSE;
}

// обработка части файла и при необходимости заказ следующего файла или завершение обновления
static Bool updProcessFilePart()
{
	trcS("updProcessFilePart()");
	trcFN("Free space: %u", flshGetFree());

	Bool result = FALSE;

	// получаем данные о размере файла
	card FileSize = UpdateInfo->Files[UpdateInfo->CurrentIndex].FileSize;

	UpdateInfo->CurrentPart++; // ждём следующую часть файла
	// пишем часть файла на диск
	int ret = FS_write(dsGetData(dslFilePart), dsGetDataSize(dslFilePart), 1, UpdateInfo->CurrentFile);
	if (ret == 1)
	{
		FS_flush(UpdateInfo->CurrentFile);
		UpdateInfo->CurrentFileBytesWrited += dsGetDataSize(dslFilePart);
		UpdateInfo->TotalBytesWrited += dsGetDataSize(dslFilePart);
		updDisplayStatus();
		result = TRUE;

		// проверяем конец записи в текущий файл
		if (UpdateInfo->CurrentFileBytesWrited >= FileSize)
		{
			// всё, этот файл закончили, закрываем
			FS_close(UpdateInfo->CurrentFile);

			// пытаемся отправить запрос на следующий файл
	    	UpdateInfo->CurrentIndex++;
	    	if (UpdateInfo->CurrentIndex < UpdateInfo->FilesCount) // запрашиваем следующий файл
	    	{
	    		updDisplayStatus();
	    		result = updCreateNewFile() && updSendGetNextFile();
	    	}
	    	else // иначе вызываем процедуру завершения обновления
	    		FinalizeUpdate();
		}
	}
	else
	{
		FS_close(UpdateInfo->CurrentFile);
		usrInfo(infFileWriteError);
	}

	return result;
}

// следующая часть файла
void pcHandleAnswerNextPart(tBuffer *buf, byte *PCState, byte *RSState)
{
	trcS("pcHandleAnswerNextPart()");
	Bool result = FALSE;
	// read the buffer
	tlvReader tr;
    tlvrInit(&tr, buf->ptr, buf->dim);
    byte tag;
    card len;
    byte *value;

    word ReceivedFileIndex = 0, ReceivedPartIndex = 0;
    while (tlvrNext(&tr, &tag, &len) == TRUE)
    {
    	value = memAllocate(len + 1); // +1 for strings
    	tlvrNextValue(&tr, len, value);
    	switch (tag)
    	{
			case srtFileIndex: // current file index
				ReceivedFileIndex = tlv2word(value);
				/*memcpy(&ReceivedFileIndex, value, len);
				memrev(&ReceivedFileIndex, len);*/
				break;
			case srtPartIndex: // current part index
				ReceivedPartIndex = tlv2word(value);
				/*memcpy(&ReceivedPartIndex, value, len);
				memrev(&ReceivedPartIndex, len);*/
				break;
			case srtPartBuffer: // part of file
				dsAllocate(dslFilePart, value, len); // в датаслот положить часть файла для дальнейшей записи на диск
				break;
    	}
    	value = memFree(value); // FREE MEMORY OF VALUE
    }

    //printS("F: %d, P: %d\n", ReceivedFileIndex, ReceivedPartIndex);
    //printS("CWB: %d\n", updCurrentBytesWrited);

    // проверка пакета (если полученные файл и часть равны ожидаемым и есть что-то для записи)
    if (ReceivedFileIndex == UpdateInfo->CurrentIndex && ReceivedPartIndex == UpdateInfo->CurrentPart && (!dsIsFree(dslFilePart)) && UpdateInfo->CurrentFile != NULL)
    	result = updProcessFilePart();
    else
    	PrintRefusal8859("Контроль целостности сообщил о сбое обновления.\n");

	dsFree(dslFilePart);

	*PCState = result ? lssPrepareWaitForData : lssProtocolError;
}
