//
// $Id: SIutils.cpp,v 1.15 2005/11/03 21:08:52 mogway Exp $
//
// utility functions for the SI-classes (dbox-II-project)
//
//    Homepage: http://dbox2.elxsi.de
//
//    Copyright (C) 2001 fnbrd (fnbrd@gmx.de)
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Log: SIutils.cpp,v $
// Revision 1.15  2005/11/03 21:08:52  mogway
// sectionsd update by Houdini
//
// Changes:
// - EIT und SDT DMX buffer verändert
// -> keine(weniger) POLLER, kostet Speicher beim EITDMX, spart Speicher beim SDTDMX
//
// - vor dem Parsen der Sections werden die Buffer nicht mehr (unnötig) ein zweites mal allokiert und umkopiert -> mehr Performance, weniger Speicherfragmentierung
//
// - unnötige/unbenutze Funktionen auskommentiert -> das gestrippte sectionsd binary wird 23kB kleiner, Test-/Beispielprogramme wie sdt, epg, nit, ... können dann nicht mehr kompiliert werden.
//
// Revision 1.14  2003/03/03 13:38:33  obi
// - cleaned up changeUTCtoCtime a bit
// - finish pthreads using pthread_exit(NULL) instead of return 0
// - use settimeofday() instead of stime()
//
// Revision 1.13  2002/11/03 22:26:54  thegoodguy
// Use more frequently types defined in zapittypes.h(not complete), fix some warnings, some code cleanup
//
// Revision 1.12  2001/07/17 14:15:52  fnbrd
// Kleine Aenderung damit auch static geht.
//
// Revision 1.11  2001/07/14 16:38:46  fnbrd
// Mit workaround fuer defektes mktime der glibc
//
// Revision 1.10  2001/07/12 22:55:51  fnbrd
// Fehler behoben
//
// Revision 1.9  2001/07/12 22:51:25  fnbrd
// Time-Thread im sectionsd (noch disabled, da prob mit mktime)
//
// Revision 1.8  2001/07/06 11:09:56  fnbrd
// Noch ne Kleinigkeit gefixt.
//
// Revision 1.7  2001/07/06 09:46:01  fnbrd
// Kleiner Fehler behoben
//
// Revision 1.6  2001/07/06 09:27:40  fnbrd
// Kleine Anpassung
//
// Revision 1.5  2001/06/10 14:55:51  fnbrd
// Kleiner Aenderungen und Ergaenzungen (epgMini).
//
// Revision 1.4  2001/05/19 22:46:50  fnbrd
// Jetzt wellformed xml.
//
// Revision 1.3  2001/05/18 13:11:46  fnbrd
// Fast komplett, fehlt nur noch die Auswertung der time-shifted events
// (Startzeit und Dauer der Cinedoms).
//
// Revision 1.2  2001/05/17 01:53:35  fnbrd
// Jetzt mit lokaler Zeit.
//
// Revision 1.1  2001/05/16 15:23:47  fnbrd
// Alles neu macht der Mai.
//
//

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <time.h>
#include <string.h>

#include "SIutils.hpp"

// Thanks to kwon
time_t changeUTCtoCtime(const unsigned char *buffer, int local_time)
{
	int year, month, day, y_, m_, k, hour, minutes, seconds, mjd;

	if (!memcmp(buffer, "\xff\xff\xff\xff\xff", 5))
		return 0; // keine Uhrzeit

	mjd = (buffer[0] << 8) | buffer[1];
	hour = buffer[2];
	minutes = buffer[3];
	seconds = buffer[4];

	y_   = (int) ((mjd - 15078.2) / 365.25);
	m_   = (int) ((mjd - 14956.1 - (int) (y_ * 365.25)) / 30.6001);
	day  = mjd - 14956 - (int) (y_ * 365.25) - (int) (m_ * 30.60001);

	k = !!((m_ == 14) || (m_ == 15));

	year  = y_ + k + 1900;
	month = m_ - 1 - k * 12;

	struct tm time;
	memset(&time, 0, sizeof(struct tm));

	time.tm_mday = day;
	time.tm_mon = month - 1;
	time.tm_year = year - 1900;
	time.tm_hour = (hour >> 4) * 10 + (hour & 0x0f);
	time.tm_min = (minutes >> 4) * 10 + (minutes & 0x0f);
	time.tm_sec = (seconds >> 4) * 10 + (seconds & 0x0f);

#if 0
	printf ("Startzeit: GMT: %.2d.%.2d.%.4d  %.2x:%.2x:%.2x\n",
		day, month, year, hour, minutes, seconds);
	printf ("Startzeit: GMT: %.2d.%.2d.%.4d  %.2d:%.2d:%.2d\n",
		time.tm_mday, time.tm_mon + 1, time.tm_year + 1900,
		time.tm_hour, time.tm_min, time.tm_sec);
#endif

	return mktime(&time) + (local_time ? -timezone : 0);
}

time_t parseDVBtime(uint16_t mjd, uint32_t bcd)
{
	int year, month, day, y_, m_, k, hour, minutes, seconds;

	y_   = (int) ((mjd - 15078.2) / 365.25);
	m_   = (int) ((mjd - 14956.1 - (int) (y_ * 365.25)) / 30.6001);
	day  = mjd - 14956 - (int) (y_ * 365.25) - (int) (m_ * 30.60001);

	hour = (bcd >> 16) & 0xFF;
	minutes = (bcd >> 8) & 0xFF;
	seconds = bcd & 0xFF;

	k = !!((m_ == 14) || (m_ == 15));

	year  = y_ + k + 1900;
	month = m_ - 1 - k * 12;

	struct tm time;
	memset(&time, 0, sizeof(struct tm));

	time.tm_mday = day;
	time.tm_mon = month - 1;
	time.tm_year = year - 1900;
	time.tm_hour = (hour >> 4) * 10 + (hour & 0x0f);
	time.tm_min = (minutes >> 4) * 10 + (minutes & 0x0f);
	time.tm_sec = (seconds >> 4) * 10 + (seconds & 0x0f);

	return mktime(&time) - timezone;
}

// Thanks to tmbinc
int saveStringToXMLfile(FILE *out, const char *c, int /*withControlCodes*/)
{
	if(!c)
		return 1;
	// Die Umlaute sind ISO-8859-9 [5]
	/*
	  char buf[6000];
	  int inlen=strlen(c);
	  int outlen=sizeof(buf);
	//  UTF8Toisolat1((unsigned char *)buf, &outlen, (const unsigned char *)c, &inlen);
	  isolat1ToUTF8((unsigned char *)buf, &outlen, (const unsigned char *)c, &inlen);
	  buf[outlen]=0;
	  c=buf;
	*/
	for(; *c; c++) {
		switch ((unsigned char)*c) {
		case '<':
			fprintf(out, "&lt;");
			break;
		case '>':
			fprintf(out, "&gt;");
			break;
		case '&':
			fprintf(out, "&amp;");
			break;
		case '\"':
			fprintf(out, "&quot;");
			break;
		case '\'':
			fprintf(out, "&apos;");
			break;
#if 0
		case 0x81:
		case 0x82:
			break;
		case 0x86:
			if(withControlCodes)
				fprintf(out, "<b>");
			break;
		case 0x87:
			if(withControlCodes)
				fprintf(out, "</b>");
			break;
		case 0x8a:
			if(withControlCodes)
				fprintf(out, "<br/>");
			break;
		default:
			if (*c<32)
				break;
			if ((*c>=32) && (((unsigned char)*c)<128))
				fprintf(out, "%c", *c);
			else
				fprintf(out, "&#%d;", *c);
#else
		default:
			if ((unsigned char)*c<32)
				break;
			fprintf(out, "%c", *c);
#endif
		} // case

	} // for
	return 0;
}

// Entfernt die ControlCodes aus dem String (-> String wird evtl. kuerzer)
void removeControlCodes(char *string)
{
	if(!string)
		return;
	for(; *string; )
		if (!((*string>=32) && (((unsigned char)*string)<128)))
			memmove(string, string+1, strlen(string+1)+1);
		else
			string++;
	return ;
}

#ifdef ENABLE_FREESATEPG
#include "FreesatTables.hpp"
std::string freesatHuffmanDecode(std::string input)
{
	const char *src = input.c_str();
	uint size = input.length();

	if (src[1] == 1 || src[1] == 2)
	{
		std::string uncompressed(size * 3, ' ');
		uint p = 0;
		struct hufftab *table;
		unsigned table_length;
		if (src[1] == 1)
		{
			table = fsat_huffman1;
			table_length = sizeof(fsat_huffman1) / sizeof(fsat_huffman1[0]);
		}
		else
		{
			table = fsat_huffman2;
			table_length = sizeof(fsat_huffman2) / sizeof(fsat_huffman2[0]);
		}
		unsigned value = 0, byte = 2, bit = 0;
		while (byte < 6 && byte < size)
		{
			value |= src[byte] << ((5-byte) * 8);
			byte++;
		}
		char lastch = START;

		do
		{
			bool found = false;
			unsigned bitShift = 0;
			if (lastch == ESCAPE)
			{
				found = true;
				// Encoded in the next 8 bits.
				// Terminated by the first ASCII character.
				char nextCh = (value >> 24) & 0xff;
				bitShift = 8;
				if ((nextCh & 0x80) == 0)
					lastch = nextCh;
				if (p >= uncompressed.length())
					uncompressed.resize(p+10);
				uncompressed[p++] = nextCh;
			}
			else
			{
				for (unsigned j = 0; j < table_length; j++)
				{
					if (table[j].last == lastch)
					{
						unsigned mask = 0, maskbit = 0x80000000;
						for (short kk = 0; kk < table[j].bits; kk++)
						{
							mask |= maskbit;
							maskbit >>= 1;
						}
						if ((value & mask) == table[j].value)
						{
							char nextCh = table[j].next;
							bitShift = table[j].bits;
							if (nextCh != STOP && nextCh != ESCAPE)
							{
								if (p >= uncompressed.length())
									uncompressed.resize(p+10);
								uncompressed[p++] = nextCh;
							}
							found = true;
							lastch = nextCh;
							break;
						}
					}
				}
			}
			if (found)
			{
				// Shift up by the number of bits.
				for (unsigned b = 0; b < bitShift; b++)
				{
					value = (value << 1) & 0xfffffffe;
					if (byte < size)
						value |= (src[byte] >> (7-bit)) & 1;
					if (bit == 7)
					{
						bit = 0;
						byte++;
					}
					else bit++;
				}
			}
			else
			{
				// Entry missing in table.
				uncompressed.resize(p);
				uncompressed.append("...");
				return uncompressed;
			}
		} while (lastch != STOP && value != 0);

		uncompressed.resize(p);
		return uncompressed;
	}
	else return input;
}
#endif
