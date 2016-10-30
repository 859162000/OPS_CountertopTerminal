/*
 * opsDataFiles.c
 *  Функции для записи и последовательного чтения любых структурированных данных переменной длины
 *  Created on: 18.01.2012
 *      Author: Pavel
 */

#include "SDK30.h" // странно, но это это объявление должно стоять первым, иначе не работает
#include "log.h"

static tDiskInfo dfDiskInfo = { "\0", 0 };

// ========================= работа с диском =========================

// Возвращает текущие настройки диска
tDiskInfo *dfDiskGet() { return &dfDiskInfo; }

// размонтировать текущий диск
Bool dfDiskClose()
{
	Bool Result = FALSE;
	if (strlen(dfDiskInfo.DiskName) != 0) // если уже какой-то диск был инициализирован - размонтируем его
	{
		Result = FS_unmount(dfDiskInfo.DiskName) == FS_OK;
		memset(&dfDiskInfo, 0, sizeof(dfDiskInfo));
	}
	return Result;
}

// удалить ранее открытый диск
Bool dfDiskKill()
{
	Bool Result = dfDiskClose();
	if (Result)
	{
		Result = FS_dskkill(dfDiskInfo.DiskName) == FS_OK;
		/*if (Result)
			Result = FS_dskdelete(tmp) == FS_OK;*/
	}
	return Result;
}

Bool dfDiskSizes(card *Size, card *Free)
{
	*Size = FS_dsksize(dfDiskInfo.DiskName);
	*Free = FS_dskfree(dfDiskInfo.DiskName);
	return *Size >= *Free;
}

// смонтировать или создать новый диск
Bool dfDiskOpen(char *DiskName, card DiskSize)
{
	Bool Result = strlen(DiskName) <= DF_DISK_NAME_LENGTH;
	if (!Result) // если имя диска > разрешённого, то fault
	{
		printS("Disk name is too big: %s\n", DiskName);
		return Result;
	}

	if (strlen(dfDiskInfo.DiskName) != 0) // если уже какой-то диск был инициализирован - размонтируем его
		Result = dfDiskClose();

	sprintf(dfDiskInfo.DiskName, "/%s", DiskName);
	dfDiskInfo.DiskSize = DiskSize;

	unsigned int AccessMode;
	Result = FS_mount(dfDiskInfo.DiskName, &AccessMode) == FS_OK;

	if (!Result)
	{
		S_FS_PARAM_CREATE CreateParam;
		memset(&CreateParam, 0, sizeof(CreateParam));
		strcpy(CreateParam.Label, dfDiskInfo.DiskName + 1); // +1 - skip first slash
		CreateParam.Mode = FS_WRITEONCE;
		CreateParam.AccessMode = FS_WRTMOD;
		CreateParam.NbFichierMax = 16;
		CreateParam.IdentZone = FS_WO_ZONE_DATA;
		Result = FS_dskcreate(&CreateParam, &dfDiskInfo.DiskSize) == FS_OK;
		if (Result)
			Result = FS_mount(dfDiskInfo.DiskName, &AccessMode) == FS_OK;
	}

	return Result;
}

// удаление всех файлов с диска
// Видимо, не работает в NON-DIR файловой системе
Bool dfDiskClear()
{
	Bool Result = TRUE;
	char Pattern[1 + DF_DISK_NAME_LENGTH + 4 + 1];
	sprintf(Pattern, "%s/", dfDiskInfo.DiskName);

    S_FS_FILEINFO FileInfo;
	S_FS_DIR *Directory = FS_opendir(Pattern);
	while (Directory != NULL)
	{
		tDataFile df;
		if (FS_readdir(Directory, &FileInfo) == FS_OK)
			Result &= dfInit(&df, FileInfo.FileName) && dfDelete(&df);
		else
			break;
	}
	FS_closedir(Directory);
	return Result;
}

// ========================= работа с файлами (общее) =========================

Bool dfExists(tDataFile *df)
{
	return FS_exist(df->FileName) == FS_OK;
}

Bool dfExists2(char *FileName)
{
	tDataFile df;
	dfInit(&df, FileName);
	return dfExists(&df);
}

Bool dfDelete(tDataFile *df)
{
	Bool Result = dfExists(df);
	if (Result)
	{
		Result = FS_unlink(df->FileName) == FS_OK;
		if (Result)
			df->File = NULL;
	}
	return Result;
}

Bool dfDelete2(char *FileName)
{
	tDataFile df;
	dfInit(&df, FileName);
	return dfDelete(&df);
}

static card dfCountOfDeletedItems(tDataFile *df);

Bool dfClose(tDataFile *df)
{
	Bool Result = FALSE;
	if (df->File != NULL)
	{
		Bool CanDelete = FALSE;
		if (df->Header.Count > 0 && (df->Header.Count == dfCountOfDeletedItems(df))) // если количество удалённых совпадает с общим количеством, то удаляем файл
			CanDelete = TRUE;
		Result = FS_close(df->File) == FS_OK;
		df->File = NULL;
		if (CanDelete)
			dfDelete(df);
	}
	return Result;
}

unsigned long dfFileSize(tDataFile *df)
{
	unsigned long Size = FS_length(df->File);
	return Size == -1 ? 0 : Size;
}

Bool dfFileExists(tDataFile *df)
{
	return FS_exist(df->FileName);
}

static Bool dfWriteFileHeader(tDataFile *df)
{
	Bool Result = dfFileSize(df) >= 0;
	if (dfFileSize(df) > 0)
		Result = FS_seek(df->File, 0, FS_SEEKSET) == FS_OK;
	if (Result)
		Result = FS_write(&df->Header, sizeof(df->Header), 1, df->File) == 1;
	return Result;
}

static Bool dfReadFileHeader(tDataFile *df)
{
	Bool Result = dfFileSize(df) > 0;
	if (Result)
		Result = FS_seek(df->File, 0, FS_SEEKSET) == FS_OK;
	if (Result)
		Result = FS_read(&df->Header, sizeof(df->Header), 1, df->File) == 1;
	return Result;
}

// инициализирует датафайл
Bool dfInit(tDataFile *df, char *FileName)
{
	memset(df, 0, sizeof(tDataFile));
	Bool Result = strlen(FileName) <= DF_FILE_NAME_LENGTH;
	if (!Result)
		printS("File name is too big: %s\n", FileName);
	else
	{
		sprintf(df->FileName, "%s/%s", dfDiskInfo.DiskName, FileName);
		df->FindOffset = 0;
	}
	return Result;
}

// открывает датафайл или создаёт его, если он не был создан
// возвращает TRUE в случае успешного открытия
Bool dfOpen(tDataFile *df)
{
	Bool Result = TRUE;
	if (df->File)
		Result = dfClose(df);
	if (Result)
	{
		df->File = FS_open(df->FileName, "r+");
		if (!df->File) // create a new file
		{
			df->File = FS_open(df->FileName, "a");
			Result = df->File != NULL;
			if (Result)
			{
				FS_close(df->File); // close file after creation
				df->File = FS_open(df->FileName, "r+"); // open file with r+ mode (for seeking)
				Result = df->File != NULL;
				if (Result)
				{
					memset(&df->Header, 0, sizeof(df->Header));
					Result = dfWriteFileHeader(df);
				}
			}
		}
		if (Result)
			Result = dfReadFileHeader(df); // get information from file header
	}
	return (df->File != NULL) && Result;
}

// ========================= работа с файлами (файлы с записями переменной длины) =========================

// Добавляет в файл запись с данными (buffer, size) и устанавливает идентификатор поиска searchID (размера DF_SEARCHID_SIZE)
// Если searchID не используется, то указывать (NULL, 0)
Bool dfAdd(tDataFile *df, byte *buffer, card size, byte *searchID)
{
	Bool Result = df->File != NULL && dfFileSize(df) >= sizeof(tDataFileHeader);
	if (Result)
	{
		Result = FS_seek(df->File, 0, FS_SEEKEND) == FS_OK; // go to EOF
		if (Result)
		{
			tDataItemHeader header;
			memset(&header, 0, sizeof(header));
			header.Size = size;
			header.Offset = FS_tell(df->File);
			if (searchID != NULL)
				memcpy(header.SearchID, searchID, DF_SEARCHID_SIZE);
			// write the header
			Result = FS_write(&header, sizeof(header), 1, df->File) == 1;
			if (Result)
			{
				// write the item data
				Result = FS_write(buffer, header.Size, 1, df->File) == 1;
				if (Result) // correct the header and rewrite it
				{
					df->Header.Count++;
					Result = dfWriteFileHeader(df);
				}
			}
		}
	}
	return Result;
}

// Инициализирует поиск в файле данных
Bool dfFindFirst(tDataFile *df)
{
	Bool Result = df->File != NULL;
	if (Result)
		df->FindOffset = DF_FIRST_DATA_POSITION;
	return Result;
}

// ищем первую неудалённю запись в файле и читаем её заголовок для определения размера
Bool dfFindNext(tDataFile *df, tDataItemHeader *header, Bool SkipDeletedItems)
{
	Bool Result = df->File != NULL, Continue = TRUE;
	if (Result)
	{
		Result = FS_seek(df->File, df->FindOffset, FS_SEEKSET) == FS_OK; // go to current position
		if (Result)
		{
			while (Continue && Result)
			{
				tDataItemHeader tmp;
				Result = FS_read(&tmp, sizeof(tDataItemHeader), 1, df->File) == 1;
				df->FindOffset += sizeof(tDataItemHeader) + tmp.Size;

				if (!Result)
					Continue = FALSE; // чтение не удалось, выходим
				else
				{
					if ((tmp.Flags & difDeleted) != 0) // если запись удалена
					{
						if (SkipDeletedItems) // пропускаем её
						{
							Continue = TRUE;
							Result = FS_seek(df->File, tmp.Size, FS_SEEKCUR) == FS_OK;
						}
						else
						{
							Continue = FALSE;
							memcpy(header, &tmp, sizeof(tDataItemHeader));
						}
					}
					else
					{
						Continue = FALSE;
						memcpy(header, &tmp, sizeof(tDataItemHeader));
					}
				}
			}
		}
	}
	return Result;
}

// Осуществляет поиск среди неудалённых итемов в датафайле по переданному ID, начиная с первой записи. Возвращает первый найденный итем.
// Возвращает TRUE, если итем был найден и хидер найденного итема
// Размер searchID должен быть DF_SEARCHID_SIZE
Bool dfFindByID(tDataFile *df, tDataItemHeader *header, byte *searchID)
{
	if (df->File != NULL && dfFindFirst(df))
		while (dfFindNext(df, header, TRUE))
			if (memcmp(header->SearchID, searchID, DF_SEARCHID_SIZE) == 0)
				return TRUE;
	return FALSE;
}

// read item data into the buffer. Buffer should be allocated on header->size bytes
Bool dfReadItemData(tDataFile *df, tDataItemHeader *header, byte *buffer)
{
	Bool Result = df->File != NULL;
	if (Result)
	{
		Result = FS_seek(df->File, header->Offset + sizeof(tDataItemHeader), FS_SEEKSET) == FS_OK; // go to item data position
		if (Result)
			Result = FS_read(buffer, header->Size, 1, df->File) == 1;
	}
	return Result;
}

// mark item as deleted
Bool dfDeleteItem(tDataFile *df, tDataItemHeader *header)
{
	Bool Result = df->File != NULL;
	if (Result)
	{
		header->Flags = header->Flags | difDeleted;
		Result = FS_seek(df->File, header->Offset, FS_SEEKSET) == FS_OK; // go to item data position
		if (Result)
			Result = FS_write(header, sizeof(tDataItemHeader), 1, df->File) == 1;
	}
	return Result;
}

Bool dfDeleteItemByID(tDataFile *df, byte *searchID)
{
	tDataItemHeader Item;
	if (dfFindByID(df, &Item, searchID))
		return dfDeleteItem(df, &Item);
	else
		return FALSE;
}

static card dfCountOfDeletedItems(tDataFile *df)
{
	card Count = 0;
	if (dfFindFirst(df))
	{
    	tDataItemHeader header;
    	while (dfFindNext(df, &header, FALSE))
    		if ((header.Flags & difDeleted) != 0) // если итем удалён
    			Count++;
	}
	return Count;
}

// count of undeleted items
card dfCount(tDataFile *df)
{
	return df->Header.Count - dfCountOfDeletedItems(df);
}

Bool dfIsInitialized(tDataFile *df)
{
	return df->File != NULL;
}

// ========================= работа с файлами (файлы с записями постоянной длины) =========================

/*
Bool dfAdd(tDataFile *df, void *buffer, card size)
{
	Bool Result = df->File != NULL && dfFileSize(df) >= sizeof(tDataFileHeader);
	if (Result)
	{
		Result = FS_seek(df->File, 0, FS_SEEKEND) == FS_OK; // go to EOF
		if (Result)
		{
			tDataItemHeader header;
			memset(&header, 0, sizeof(header));
			header.Size = size;
			header.Offset = FS_tell(df->File);
			// write the header
			Result = FS_write(&header, sizeof(header), 1, df->File) == 1;
			if (Result)
			{
				// write the item data
				Result = FS_write(buffer, header.Size, 1, df->File) == 1;
				if (Result) // correct the header and rewrite it
				{
					df->Header.Count++;
					Result = dfWriteFileHeader(df);
				}
			}
		}
	}
	return Result;
}
*/
