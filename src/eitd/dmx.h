/*
 * $Header: /cvs/tuxbox/apps/tuxbox/neutrino/daemons/sectionsd/dmx.h,v 1.17 2009/05/23 16:50:12 seife Exp $
 *
 * DMX class (sectionsd) - d-box2 linux project
 *
 * (C) 2003 by thegoodguy <thegoodguy@berlios.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __sectionsd__dmx_h__
#define __sectionsd__dmx_h__

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <vector>
#include <config.h>
#if HAVE_COOL_HARDWARE
#include <dmx_cs.h>
#endif
#if HAVE_TRIPLEDRAGON
#include <dmx_td.h>
#endif

#include <set>
#include <map>

typedef uint64_t sections_id_t;
typedef unsigned char version_number_t;

typedef std::set<sections_id_t> section_map_t;
typedef std::map<sections_id_t, version_number_t, std::less<sections_id_t> > MyDMXOrderUniqueKey;

class DMX
{
protected:

	int		fd;
	cDemux *	dmx;
	int		dmx_num;
	unsigned short  pID;
	unsigned short  dmxBufferSizeInKB;
	sections_id_t	first_skipped;
	int		current_service;
	unsigned char	eit_version;
	bool		cache; /* should read sections be cached? true for all but dmxCN */
	int		real_pauseCounter;

	inline bool isOpen(void) {
		return (fd != -1);
	}

	int immediate_start(void); /* mutex must be locked before and unlocked after this method */
	int immediate_stop(void);  /* mutex must be locked before and unlocked after this method */

	bool next_filter();
	void init();

	section_map_t seenSections;
	section_map_t calcedSections;
	bool cache_section(sections_id_t sectionNo, uint8_t number, uint8_t last, uint8_t segment_last);
public:
	struct s_filters
	{
		unsigned char filter;
		unsigned char mask;
	};

	std::vector<s_filters> filters;
	int                    filter_index;
	time_t                 lastChanged;

	pthread_cond_t         change_cond;
	pthread_mutex_t        start_stop_mutex;


	DMX();
	DMX(const unsigned short p, const unsigned short bufferSizeInKB, const bool cache = true, int dmx_source = 0);
	~DMX();

	int start(void);
	void closefd(void);
	void addfilter(const unsigned char filter, const unsigned char mask);
	int stop(void);
	void close(void);

	int real_pause(void);
	int real_unpause(void);

	int request_pause(void);
	int request_unpause(void);

	int change(const int new_filter_index, const int new_current_service = -1); // locks while changing

	void lock(void);
	void unlock(void);

	// section with size < 3 + 5 are skipped !
	int getSection(uint8_t *buf, const unsigned timeoutInMSeconds, int &timeouts);
	int setPid(const unsigned short new_pid);
	int setCurrentService(int new_current_service);
	int dropCachedSectionIDs();

	unsigned char get_eit_version(void);
	// was useful for debugging...
	unsigned int get_current_service(void);
};

#endif /* __sectionsd__dmx_h__ */

