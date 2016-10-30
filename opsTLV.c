/*
 * opsTLV.c

 *
 *  Created on: 19.04.2011
 *      Author: pavel
 */
#include "sdk30.h"
#include "log.h"

// ------------------------------------- TLV operations --------------------------------------------------------

// return tag, length and offset from start of ptr for reading data
static Bool tlvParseTL(byte* ptr, byte *tag, card *len, int *ofs)
{
	*tag = (byte)ptr[0];
	memset(len, 0, sizeof(card));
	if (ptr[1] < 0x80)
	{
		*len = (word)ptr[1];
		*ofs = (int)2;
	}
	else // if > 128 then low bits of value contains real length
	{
		byte lensize = ptr[1] & 0x0F;
		memcpy(len, ptr+2, lensize);
		if (lensize > 1)
		{
			if (IsLittleEndian())
				memrev(len, lensize);
		}
		*ofs = (int)(2+lensize);
	}

	//*value = data[2];
	return TRUE;
}

// just setup buffer values
void tlvBufferInit(tBuffer *buf, byte *ptr, word dim)
{
	buf->ptr = ptr;
    buf->dim = dim;
    buf->pos = dim;
}

// prepare buffer for reading TLV
void tlvrInit(tlvReader *reader, byte *ptr, word dim)
{
	reader->ptr = ptr;
	reader->dim = dim;
    reader->pos = 0;
    reader->ofs = 0;
}

// get next TLV tag and length from the buffer
// return FALSE if end of buffer or no tags found
// use tlvReadNextValue for getting value from buffer and prepare for search next tag
Bool tlvrNext(tlvReader *reader, byte *tag, card *len)
{
    int ofs;
    if (reader && reader->ptr && reader->pos < reader->dim)
    {
        if (tlvParseTL(reader->ptr + reader->pos, tag, len, &ofs))
        {
            if (*len == 0)
            	return FALSE;
            reader->ofs = ofs;
            return TRUE;
        }
    }
    return FALSE;
}

Bool tlvrMoveToNextTag(tlvReader *reader, card len)
{
	reader->pos += len + reader->ofs; // move to next tag
    return TRUE;
}

// get len bytes from buf and increase pos counter
Bool tlvrNextValue(tlvReader *reader, card len, byte *data)
{
    memcpy(data, reader->ptr + reader->pos + reader->ofs, len);
    tlvrMoveToNextTag(reader, len);
    return TRUE;
}

// get card value from tlv value
uint64 tlv2uint64(byte *value)
{
	uint64 result;
	memcpy(&result, value, sizeof(result));
	if (IsLittleEndian())
		memrev(&result, sizeof(result));
    return result;
}

// get card value from tlv value
card tlv2card(byte *value)
{
	card result;
	memcpy(&result, value, sizeof(result));
	if (IsLittleEndian())
		memrev(&result, sizeof(result));
    return result;
}

// get word value from tlv value
word tlv2word(byte *value)
{
	word result;
	memcpy(&result, value, sizeof(result));
	if (IsLittleEndian())
		memrev(&result, sizeof(result));
    return result;
}

byte tlv2byte(byte *value)
{
    return value[0];
}

// get string from tlv value
int tlv2string(byte *value, card len, char *string)
{
	if (len != 0)
	{
		memcpy(string, value, len);
		string[len] = 0;
		return strlen(string);
	}
	else
		return 0;
}

/* using example
   comReadBuf(&buf, ....);
   tlvReadInit(&buf);

   while (tlvReadNext(&buf, &tag, &len) == TRUE)
   {
   	  byte *data = memAllocate(len);
   	  tlvReadNextValue(&buf, len, data);

   	  switch (tag)
   	  {
		  case 0x01: memcpy(&value1, data, len); break;
		  case 0x02: memcpy(&value2, data, len); break;
		  default: break;
   	  }

   	  memFree(data);
   }
 */

// get length and value for tag
Bool tlvGetValue(tBuffer *buf, byte tag, card *len, byte *value)
{
	Bool result = FALSE;
    // pos
    byte curtag;
    card curlen = 0;
    tlvReader reader;

    tlvrInit(&reader, buf->ptr, buf->dim);
    while (tlvrNext(&reader, &curtag, &curlen) == TRUE)
    {
    	if (curlen != 0)
		{
			if (tag == curtag) // tag founded
			{
				*len = curlen;
				tlvrNextValue(&reader, *len, value);
				result = TRUE;
				break;
			}
			else
				tlvrMoveToNextTag(&reader, curlen);
		}
    }
    return result;
}

Bool tlvGetValue2(void *data, card datasize, byte tag, card *len, byte *value)
{
	tBuffer buf;
	tlvBufferInit(&buf, data, datasize);
	return tlvGetValue(&buf, tag, len, value);
}

// -------------------------------------------- buffer operations ------------------------------

int bufAppByte(tBuffer *buf, byte Data)
{
	return bufApp(buf, &Data, sizeof(Data));
}

int bufAppWord(tBuffer *buf, word Data)
{
	return bufApp(buf, (byte *)&Data, sizeof(Data));
}

int bufAppCard(tBuffer *buf, card Data)
{
	return bufApp(buf, (byte *)&Data, sizeof(Data));
}

// формирует поле LengthField в буфере buf. Макс длина - 5 байт
void tlvMakeLengthField(card length, byte *buf, byte *buflen)
{
	if (length >= 0 && length < 0x80) // <=128
	{
		buf[0] = (byte)length;
		*buflen = 1;
	}
	else if (length >= 0x80 && length <= 0xFF) // >=128 && <=255 byte
	{
		buf[0] = 0x81;
		buf[1] = (byte)length;
		*buflen = 2;
	}
	else if (length > 0xFF && length <= 0xFFFF) // word
	{
		buf[0] = 0x82;
		word wlen = (word)length;
		if (IsLittleEndian())
			memrev(&wlen, sizeof(wlen));
		memcpy(buf + 1, &wlen, sizeof(wlen));
		*buflen = 3;
	}
	else                                        // card
	{
		buf[0] = 0x84;
		card clen = length;
		if (IsLittleEndian())
			memrev(&clen, sizeof(clen));
		memcpy(buf + 1, &clen, sizeof(clen));
		*buflen = 5;
	}
}

void bufAppTLV(tBuffer *buf, byte tag, card length, byte *value)
{
	if (length == 0) return;
	bufAppByte(buf, tag);
	byte LengthField[MAX_LENGTH_FIELD_SIZE], LengthFieldSize;
	tlvMakeLengthField(length, LengthField, &LengthFieldSize);
	bufApp(buf, LengthField, LengthFieldSize);
	bufApp(buf, value, length);

	/*if (length >= 0 && length < 0x80) // <=128
	{
		bufAppByte(buf, (byte)length);
	}
	else if (length >= 0x80 && length <= 0xFF) // >=128 && <=255 byte
	{
		bufAppByte(buf, 0x81);
		bufAppByte(buf, (byte)length);
	}
	else if (length > 0xFF && length <= 0xFFFF) // word
	{
		bufAppByte(buf, 0x82);
		word wlen = (word)length;
		memrev(&wlen, sizeof(wlen));
		bufAppWord(buf, wlen);
	}
	else                                        // card
	{
		bufAppByte(buf, 0x84);
		card clen = length;
		memrev(&clen, sizeof(clen));
		bufAppCard(buf, clen);
	}*/

}

void bufAppTLVByte(tBuffer *buf, byte tag, byte value)
{
	bufAppTLV(buf, tag, sizeof(value), (byte *)&value);
}

void bufAppTLVWord(tBuffer *buf, byte tag, word value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	bufAppTLV(buf, tag, sizeof(value), (byte *)&value);
}

void bufAppTLVCard(tBuffer *buf, byte tag, card value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	bufAppTLV(buf, tag, sizeof(value), (byte *)&value);
}

void bufAppTLVShort(tBuffer *buf, byte tag, short value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	bufAppTLV(buf, tag, sizeof(value), (byte *)&value);
}

void bufAppTLVInt(tBuffer *buf, byte tag, int value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	bufAppTLV(buf, tag, sizeof(value), (byte *)&value);
}

void bufAppTLVString(tBuffer *buf, byte tag, char *value)
{
	bufAppTLV(buf, tag, strlen(value), value);
}

// добавить в буфер содержимое ячейки БД терминала (без реверса порядка байт
void bufAppTLVKey(tBuffer *buf, byte tag, card length, word key)
{
	byte value[length];
	mapGet(key, value, length);
	bufAppTLV(buf, tag, length, value);
}

// ================================================== Native TLV Writer by Pavel Alexeyuk ===============================================

void tlvwInit(tlvWriter *tw)
{
	memset(tw, 0, sizeof(tlvWriter));
}

// освобождает память, выделенную под Data, обнуляет DataSize и заново инициализирует tlvWriter
void tlvwFree(tlvWriter *tw)
{
	if (tw->Data != NULL)
	{
		memFree(tw->Data);
		tlvwInit(tw); // обнуляем всё
	}
}

static void tlvwAppendBytes(tlvWriter *tw, word length, byte *value)
{
	int NewSize = tw->DataSize + length;
	if (NewSize > 0)
	{
		tw->Data = memReallocate(tw->Data, NewSize);
		if (tw->Data != NULL)
		{
			memcpy(tw->Data + tw->DataSize, value, length);
			tw->DataSize = NewSize;
		}
	}
	else if (NewSize < 0)
	{
		Click(); Click(); Click();
		printS("tlvWriter: invalid size: %d\n", NewSize);
	}
}

void tlvwAppend(tlvWriter *tw, byte tag, word length, byte *value)
{
	if (length == 0) return;
	byte TagLengthField[1 + MAX_LENGTH_FIELD_SIZE], LengthFieldSize; /* +1 - for tag to reduce tlvwAppendBytes calls and memory fragmentation*/
	TagLengthField[0] = tag;
	tlvMakeLengthField(length, TagLengthField + 1, &LengthFieldSize);
	tlvwAppendBytes(tw, 1 + LengthFieldSize, TagLengthField); // +1 for tag
	tlvwAppendBytes(tw, length, value);
}

void tlvwAppendByte(tlvWriter *tw, byte tag, byte value)
{
	tlvwAppend(tw, tag, sizeof(value), (byte *)&value);
}

void tlvwAppendWord(tlvWriter *tw, byte tag, word value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	tlvwAppend(tw, tag, sizeof(value), (byte *)&value);
}

void tlvwAppendCard(tlvWriter *tw, byte tag, card value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	tlvwAppend(tw, tag, sizeof(value), (byte *)&value);
}

void tlvwAppendUInt64(tlvWriter *tw, byte tag, uint64 value)
{
	if (IsLittleEndian())
		memrev(&value, sizeof(value));
	tlvwAppend(tw, tag, sizeof(value), (byte *)&value);
}

void tlvwAppendString(tlvWriter *tw, byte tag, char *value)
{
	tlvwAppend(tw, tag, strlen(value), value);
}

void tlvwAppendBCDString(tlvWriter *tw, byte tag, const char *value)
{
	if (value)
	{
		int len = strlen(value);
		if (len > 0)
		{
			char BCD[len + 1]; // BCD будет максимум такой же по длине, что и изначальная строка
			int lenBCD = StringToBCD(value, BCD);
			tlvwAppend(tw, tag, lenBCD, BCD);
		}
	}
}

// добавить в буфер содержимое ячейки БД терминала (без реверса порядка байт
void tlvwAppendKey(tlvWriter *tw, byte tag, word length, word key)
{
	byte value[length];
	mapGet(key, value, length);
	tlvwAppend(tw, tag, length, value);
}

void tlvwAppendKeyByte(tlvWriter *tw, byte tag, word key)
{
	tlvwAppendByte(tw, tag, mapGetByteValue(key));
}

void tlvwAppendKeyWord(tlvWriter *tw, byte tag, word key)
{
	tlvwAppendWord(tw, tag, mapGetWordValue(key));
}

void tlvwAppendKeyCard(tlvWriter *tw, byte tag, word key)
{
	tlvwAppendCard(tw, tag, mapGetCardValue(key));
}
