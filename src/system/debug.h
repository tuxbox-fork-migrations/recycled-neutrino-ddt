/*
	NeutrinoNG  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#ifndef __neutrino_debug__
#define __neutrino_debug__
#include <zapit/debug.h>
extern int debug;

enum
{
	DEBUG_NORMAL	, // 0
	DEBUG_INFO	, // 1
	DEBUG_DEBUG	, // 2

	DEBUG_MODES	  // 3 count of available modes
};


void setDebugLevel(int level);

#define dprintf(debuglevel, fmt, args...) \
	do { \
		if (debug >= debuglevel) \
			printf("[neutrino] " fmt, ## args); \
	} while(0)

/* dont add \n when using this */
#define dprintf_colored(debuglevel, fmt, args...) do { \
		if (debug >= debuglevel) { \
			printf("%c[%d;%d;%dm", 0x1B, 1, 31, 40);\
			printf("[neutrino] " fmt, ## args); \
			printf("%c[%dm\n", 0x1B, 0); \
		}\
	}  while (0)

#define dperror(str) {perror("[neutrino] " str);}

#endif
