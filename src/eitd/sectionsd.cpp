//
//    sectionsd.cpp (network daemon for SI-sections)
//    (dbox-II-project)
//
//    Copyright (C) 2001 by fnbrd
//
//    Homepage: http://dbox2.elxsi.de
//
//    Copyright (C) 2008, 2009 Stefan Seyfried
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

#include <config.h>
#include <malloc.h>
#include <debug.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include <set>
#include <map>
#include <algorithm>
#include <string>
#include <limits>

#include <sys/wait.h>
#include <sys/time.h>

#include <connection/basicsocket.h>
#include <connection/basicserver.h>

#include <xmltree/xmlinterface.h>
#include <configfile.h>
#include <zapit/client/zapittools.h>

#include <sectionsdclient/sectionsdMsg.h>
#include <sectionsdclient/sectionsdclient.h>
#include <eventserver.h>
#include <driver/abstime.h>

#include "dmxapi.h"
#include "eitd.h"
#include "edvbstring.h"
#include "xmlutil.h"

//#define ENABLE_SDT //FIXME

//#define DEBUG_SDT_THREAD
#define DEBUG_EIT_THREAD
#define DEBUG_CN_THREAD

static bool sectionsd_ready = false;
/*static*/ bool reader_ready = true;
static unsigned int max_events;

/* period to remove old events */
//#define HOUSEKEEPING_SLEEP (5 * 60) // sleep 5 minutes
#define HOUSEKEEPING_SLEEP (30) // FIXME 1 min for testing
/* period to clean cached sections and force restart sections read */
#define META_HOUSEKEEPING (24 * 60 * 60) / HOUSEKEEPING_SLEEP // meta housekeeping after XX housekeepings - every 24h -

// Timeout bei tcp/ip connections in ms
#define READ_TIMEOUT_IN_SECONDS  2
#define WRITE_TIMEOUT_IN_SECONDS 2

// Time in seconds we are waiting for an EIT version number
//#define TIME_EIT_VERSION_WAIT		3 // old
#define TIME_EIT_VERSION_WAIT		10
// number of timeouts after which we stop waiting for an EIT version number
#define TIMEOUTS_EIT_VERSION_WAIT	(2 * CHECK_RESTART_DMX_AFTER_TIMEOUTS)

static long secondsToCache;
static long secondsExtendedTextCache;
static long oldEventsAre;
static int scanning = 1;

extern bool epg_filter_is_whitelist;
extern bool epg_filter_except_current_next;

static bool messaging_zap_detected = false;
/*static*/ bool dvb_time_update = false;

//NTP-Config
#define CONF_FILE "/var/tuxbox/config/neutrino.conf"

#ifdef USE_BB_NTPD
const std::string ntp_system_cmd_prefix = "/sbin/ntpd -q -p ";
#else
const std::string ntp_system_cmd_prefix = "/sbin/ntpdate ";
#endif

std::string ntp_system_cmd;
CConfigFile ntp_config(',');
std::string ntpserver;
int ntprefresh;
int ntpenable;

std::string		epg_dir("");

/* messaging_current_servicekey does probably not need locking, since it is
   changed from one place */
static t_channel_id    messaging_current_servicekey = 0;
static bool channel_is_blacklisted = false;

bool timeset = false;
pthread_cond_t timeIsSetCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t timeIsSetMutex = PTHREAD_MUTEX_INITIALIZER;

static int	messaging_have_CN = 0x00;	// 0x01 = CURRENT, 0x02 = NEXT
static int	messaging_got_CN = 0x00;	// 0x01 = CURRENT, 0x02 = NEXT
static bool	messaging_neutrino_sets_time = false;
// EVENTS...

static CEventServer *eventServer;

/*static*/ pthread_rwlock_t eventsLock = PTHREAD_RWLOCK_INITIALIZER; // Unsere (fast-)mutex, damit nicht gleichzeitig in die Menge events geschrieben und gelesen wird
static pthread_rwlock_t servicesLock = PTHREAD_RWLOCK_INITIALIZER; // Unsere (fast-)mutex, damit nicht gleichzeitig in die Menge services geschrieben und gelesen wird
static pthread_rwlock_t messagingLock = PTHREAD_RWLOCK_INITIALIZER;

static pthread_cond_t timeThreadSleepCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t timeThreadSleepMutex = PTHREAD_MUTEX_INITIALIZER;

static CEitThread threadEIT;
static CCNThread threadCN;

#ifdef ENABLE_FREESATEPG
static CFreeSatThread threadFSEIT;
#endif

#ifdef ENABLE_SDT
#define TIME_SDT_NONEWDATA      15
#define RESTART_DMX_AFTER_TIMEOUTS 5
#define TIME_SDT_SCHEDULED_PAUSE 2* 60* 60
CSdtThread threadSDT;
#endif

int sectionsd_stop = 0;

static bool slow_addevent = true;

inline void readLockServices(void)
{
	pthread_rwlock_rdlock(&servicesLock);
}

inline void writeLockServices(void)
{
	pthread_rwlock_wrlock(&servicesLock);
}

inline void unlockServices(void)
{
	pthread_rwlock_unlock(&servicesLock);
}

inline void readLockMessaging(void)
{
	pthread_rwlock_rdlock(&messagingLock);
}

inline void writeLockMessaging(void)
{
	pthread_rwlock_wrlock(&messagingLock);
}

inline void unlockMessaging(void)
{
	pthread_rwlock_unlock(&messagingLock);
}

inline void readLockEvents(void)
{
	pthread_rwlock_rdlock(&eventsLock);
}

inline void writeLockEvents(void)
{
	pthread_rwlock_wrlock(&eventsLock);
}

inline void unlockEvents(void)
{
	pthread_rwlock_unlock(&eventsLock);
}

inline bool waitForTimeset(void)
{
	pthread_mutex_lock(&timeIsSetMutex);
	while(!timeset)
		pthread_cond_wait(&timeIsSetCond, &timeIsSetMutex);
	pthread_mutex_unlock(&timeIsSetMutex);
	/* we have time synchronization issues, at least on kernel 2.4, so
	   sometimes the time in the threads is still 1.1.1970, even after
	   waitForTimeset() returns. Let's hope that we work around this issue
	   with this sleep */
	sleep(1);
	return true;
}

static const SIevent nullEvt; // Null-Event

static MySIeventsOrderUniqueKey mySIeventsOrderUniqueKey;
static MySIeventsOrderUniqueKey mySIeventsNVODorderUniqueKey;
/*static*/ MySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey;
static MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey;

static SIevent * myCurrentEvent = NULL;
static SIevent * myNextEvent = NULL;

// Hier landen alle Service-Ids von Meta-Events inkl. der zugehoerigen Event-ID (nvod)
// d.h. key ist der Unique Service-Key des Meta-Events und Data ist der unique Event-Key
static MySIeventUniqueKeysMetaOrderServiceUniqueKey mySIeventUniqueKeysMetaOrderServiceUniqueKey;

static MySIservicesOrderUniqueKey mySIservicesOrderUniqueKey;
static MySIservicesNVODorderUniqueKey mySIservicesNVODorderUniqueKey;

static bool deleteEvent(const event_id_t uniqueKey)
{
	bool ret = false;
	writeLockEvents();
	MySIeventsOrderUniqueKey::iterator e = mySIeventsOrderUniqueKey.find(uniqueKey);

	if (e != mySIeventsOrderUniqueKey.end()) {
		if (e->second->times.size()) {
			mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.erase(e->second);
			mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.erase(e->second);
		}

#ifndef USE_BOOST_SHARED_PTR
		delete e->second;
#endif
		mySIeventsOrderUniqueKey.erase(uniqueKey);
		mySIeventsNVODorderUniqueKey.erase(uniqueKey);
		ret = true;
	}
	unlockEvents();
	return ret;
}

/* if cn == true (if called by cnThread), then myCurrentEvent and myNextEvent is updated, too */
/*static*/ void addEvent(const SIevent &evt, const time_t zeit, bool cn = false)
{
	bool EPG_filtered = checkEPGFilter(evt.original_network_id, evt.transport_stream_id, evt.service_id);

	/* more readable in "plain english":
	   if current/next are not to be filtered and table_id is current/next -> continue
	   else {
		if epg filter is blacklist and filter matched -> stop. (return)
		if epg filter is whitelist and filter did not match -> stop also.
	   }
	 */
	if (!(epg_filter_except_current_next && (evt.table_id == 0x4e || evt.table_id == 0x4f)) &&
			(evt.table_id != 0xFF)) {
		if (!epg_filter_is_whitelist && EPG_filtered) {
			//dprintf("addEvent: blacklist and filter did match\n");
			return;
		}
		if (epg_filter_is_whitelist && !EPG_filtered) {
			//dprintf("addEvent: whitelist and filter did not match\n");
			return;
		}
	}

	if (cn) { // current-next => fill current or next event...
//xprintf("addEvent: current %016llx event %016llx messaging_got_CN %d\n", messaging_current_servicekey, evt.get_channel_id(), messaging_got_CN);
		readLockMessaging();
		// only if it is the current channel... and if we don't have them already.
		if (evt.get_channel_id() == messaging_current_servicekey && 
				(messaging_got_CN != 0x03)) { 
xprintf("addEvent: current %016llx event %016llx running %d messaging_got_CN %d\n", messaging_current_servicekey, evt.get_channel_id(), evt.runningStatus(), messaging_got_CN);

			unlockMessaging();
			writeLockEvents();
			if (evt.runningStatus() > 2) { // paused or currently running
				//TODO myCurrentEvent/myNextEvent without pointers.
				if (!myCurrentEvent || (myCurrentEvent && (*myCurrentEvent).uniqueKey() != evt.uniqueKey())) {
					delete myCurrentEvent;
					myCurrentEvent = new SIevent(evt);
					writeLockMessaging();
					messaging_got_CN |= 0x01;
					if (myNextEvent && (*myNextEvent).uniqueKey() == evt.uniqueKey()) {
						dprintf("addevent-cn: removing next-event\n");
						/* next got "promoted" to current => trigger re-read */
						delete myNextEvent;
						myNextEvent = NULL;
						messaging_got_CN &= 0x01;
					}
					unlockMessaging();
					dprintf("addevent-cn: added running (%d) event 0x%04x '%s'\n",
						evt.runningStatus(), evt.eventID, evt.getName().c_str());
				} else {
					writeLockMessaging();
					messaging_got_CN |= 0x01;
					unlockMessaging();
					dprintf("addevent-cn: not add runn. (%d) event 0x%04x '%s'\n",
						evt.runningStatus(), evt.eventID, evt.getName().c_str());
				}
			} else {
				if ((!myNextEvent    || (myNextEvent    && (*myNextEvent).uniqueKey()    != evt.uniqueKey() && (*myNextEvent).times.begin()->startzeit < evt.times.begin()->startzeit)) &&
						(!myCurrentEvent || (myCurrentEvent && (*myCurrentEvent).uniqueKey() != evt.uniqueKey()))) {
					delete myNextEvent;
					myNextEvent = new SIevent(evt);
					writeLockMessaging();
					messaging_got_CN |= 0x02;
					unlockMessaging();
					dprintf("addevent-cn: added next    (%d) event 0x%04x '%s'\n",
						evt.runningStatus(), evt.eventID, evt.getName().c_str());
				} else {
					dprintf("addevent-cn: not added next(%d) event 0x%04x '%s'\n",
						evt.runningStatus(), evt.eventID, evt.getName().c_str());
					writeLockMessaging();
					messaging_got_CN |= 0x02;
					unlockMessaging();
				}
			}
			unlockEvents();
		} else
			unlockMessaging();
	}

	readLockEvents();
	MySIeventsOrderUniqueKey::iterator si = mySIeventsOrderUniqueKey.find(evt.uniqueKey());
	bool already_exists = (si != mySIeventsOrderUniqueKey.end());
	if (already_exists && (evt.table_id < si->second->table_id))
	{
		/* if the new event has a lower (== more recent) table ID, replace the old one */
		already_exists = false;
		dprintf("replacing event %016llx:%02x with %04x:%02x '%.40s'\n", si->second->uniqueKey(),
			si->second->table_id, evt.eventID, evt.table_id, evt.getName().c_str());
	}
	else if (already_exists && ( (evt.table_id == 0x51 || evt.table_id == 0x50 || evt.table_id == 0x4e) && evt.table_id == si->second->table_id && evt.version != si->second->version ))
	{
		//replace event if new version
		dprintf("replacing event version old 0x%02x new 0x%02x'\n", si->second->version, evt.version );
		already_exists = false;
	}

	/* Check size of some descriptors of the new event before comparing
	   them with the old ones, because the same event can be complete
	   on one German Sky channel and incomplete on another one. So we
	   make sure to keep the complete event, if applicable. */

	if (already_exists && (!evt.components.empty()) && (evt.components != si->second->components))
		already_exists = false;

	if (already_exists && (!evt.linkage_descs.empty()) && (evt.linkage_descs != si->second->linkage_descs))
		already_exists = false;

	if (already_exists && (!evt.ratings.empty()) && (evt.ratings != si->second->ratings))
		already_exists = false;

	if (already_exists && (evt.times != si->second->times))
		already_exists = false;

	if ((already_exists) && (SIlanguage::getMode() == CSectionsdClient::LANGUAGE_MODE_OFF)) {
		si->second->contentClassification = evt.contentClassification;
		si->second->userClassification = evt.userClassification;
		si->second->itemDescription = evt.itemDescription;
		si->second->item = evt.item;
		si->second->vps = evt.vps;
		if ((evt.getExtendedText().length() > 0) &&
				(evt.times.begin()->startzeit < zeit + secondsExtendedTextCache))
			si->second->setExtendedText("OFF",evt.getExtendedText().c_str());
		if (evt.getText().length() > 0)
			si->second->setText("OFF",evt.getText().c_str());
		if (evt.getName().length() > 0)
			si->second->setName("OFF",evt.getName().c_str());
	}
	else {

		SIevent *eptr = new SIevent(evt);

		if (!eptr)
		{
			printf("[sectionsd::addEvent] new SIevent failed.\n");
			unlockEvents();
			return;
		}

		SIeventPtr e(eptr);

		//Strip ExtendedDescription if too far in the future
		if ((e->times.begin()->startzeit > zeit + secondsExtendedTextCache) &&
				(SIlanguage::getMode() == CSectionsdClient::LANGUAGE_MODE_OFF) && (zeit != 0))
			e->setExtendedText("OFF","");

		/*
		 * this is test code, so indentation is deliberately wrong :-)
		 * we'll hopefully remove this if clause after testing is done
		 */
		if (slow_addevent)
		{
			std::vector<event_id_t> to_delete;
			unsigned short eventID = e->eventID;
			event_id_t e_key = e->uniqueKey();
			t_channel_id e_chid = e->get_channel_id();
			time_t start_time = e->times.begin()->startzeit;
			time_t end_time = e->times.begin()->startzeit + (long)e->times.begin()->dauer;
			/* create an event that's surely behind the one to check in the sort order */
			e->eventID = 0xFFFF; /* lowest order sort criteria is eventID */
			/* returns an iterator that's behind 'e' */
			MySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey::iterator x =
				mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.upper_bound(e);
			e->eventID = eventID;

			/* the first decrement of the iterator gives us an event that's a potential
			 * match *or* from a different channel, then no event for this channel is stored */
			while (x != mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.begin())
			{
				--x;
				if ((*x)->get_channel_id() != e_chid)
					break;
				else
				{
					event_id_t x_key = (*x)->uniqueKey();
					/* do we need this check? */
					if (x_key == e_key)
						continue;
					if ((*x)->times.begin()->startzeit >= end_time)
						continue;
					/* iterating backwards: if the endtime of the stored events
					 * is earlier than the starttime of the new one, we'll never
					 * find an identical one => bail out */
					if ((*x)->times.begin()->startzeit + (long)(*x)->times.begin()->dauer <= start_time)
						break;
					/* here we have an overlapping event */
					dprintf("%s: delete 0x%016llx.%02x time = 0x%016llx.%02x\n", __func__,
						x_key, (*x)->table_id, e_key, e->table_id);
					to_delete.push_back(x_key);
				}
			}
			unlockEvents();

			while (! to_delete.empty())
			{
				deleteEvent(to_delete.back());
				to_delete.pop_back();
			}
		} else {
			// Damit in den nicht nach Event-ID sortierten Mengen
			// Mehrere Events mit gleicher ID sind, diese vorher loeschen
			unlockEvents();
		}
		deleteEvent(e->uniqueKey());
		readLockEvents();
		if (mySIeventsOrderUniqueKey.size() >= max_events) {
			MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator lastEvent =
				mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin();

/* if you don't want the new "delete old events first" method but
 * the old-fashioned "delete future events always", invert this */
#if 0
			bool back = true;
#else
			time_t now = time(NULL);
			bool back = false;
			if ((*lastEvent)->times.size() == 1)
			{
				if ((*lastEvent)->times.begin()->startzeit + (long)(*lastEvent)->times.begin()->dauer >= now - oldEventsAre)
					back = true;
			} else
				printf("[sectionsd] addevent: times.size != 1, please report\n");
#endif
			if (back)
			{
				// fprintf(stderr, "<");
				lastEvent = mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end();
				--lastEvent;

				//preserve events of current channel
				readLockMessaging();
				while ((lastEvent != mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin()) &&
					((*lastEvent)->get_channel_id() == messaging_current_servicekey)) {
					--lastEvent;
				}
				unlockMessaging();
			}
			// else fprintf(stderr, ">");
			unlockEvents();
			deleteEvent((*lastEvent)->uniqueKey());
		}
		else
			unlockEvents();
		readLockEvents();
		// Pruefen ob es ein Meta-Event ist
		MySIeventUniqueKeysMetaOrderServiceUniqueKey::iterator i = mySIeventUniqueKeysMetaOrderServiceUniqueKey.find(e->get_channel_id());

		if (i != mySIeventUniqueKeysMetaOrderServiceUniqueKey.end())
		{
			// ist ein MetaEvent, d.h. mit Zeiten fuer NVOD-Event

			if (e->times.size())
			{
				// D.h. wir fuegen die Zeiten in das richtige Event ein
				MySIeventsOrderUniqueKey::iterator ie = mySIeventsOrderUniqueKey.find(i->second);

				if (ie != mySIeventsOrderUniqueKey.end())
				{

					// Event vorhanden
					// Falls das Event in den beiden Mengen mit Zeiten nicht vorhanden
					// ist, dieses dort einfuegen
					MySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey::iterator i2 = mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.find(ie->second);
					unlockEvents();
					writeLockEvents();

					if (i2 == mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.end())
					{
						// nicht vorhanden -> einfuegen
						mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.insert(ie->second);
						mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.insert(ie->second);
					}

					// Und die Zeiten im Event updaten
					ie->second->times.insert(e->times.begin(), e->times.end());
				}
			}
		}
		unlockEvents();
		writeLockEvents();
//		printf("Adding: %04x\n", (int) e->uniqueKey());

		// normales Event
		mySIeventsOrderUniqueKey.insert(std::make_pair(e->uniqueKey(), e));

		if (e->times.size())
		{
			// diese beiden Mengen enthalten nur Events mit Zeiten
			mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.insert(e);
			mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.insert(e);
		}
	}
	unlockEvents();
}

static void addNVODevent(const SIevent &evt)
{
	SIevent *eptr = new SIevent(evt);

	if (!eptr)
	{
		printf("[sectionsd::addNVODevent] new SIevent failed.\n");
		return;
	}

	SIeventPtr e(eptr);

	readLockEvents();
	MySIeventsOrderUniqueKey::iterator e2 = mySIeventsOrderUniqueKey.find(e->uniqueKey());

	if (e2 != mySIeventsOrderUniqueKey.end())
	{
		// bisher gespeicherte Zeiten retten
		unlockEvents();
		writeLockEvents();
		e->times.insert(e2->second->times.begin(), e2->second->times.end());
	}
	unlockEvents();

	// Damit in den nicht nach Event-ID sortierten Mengen
	// mehrere Events mit gleicher ID sind, diese vorher loeschen
	deleteEvent(e->uniqueKey());
	readLockEvents();
	if (mySIeventsOrderUniqueKey.size() >= max_events) {
		//TODO: Set Old Events to 0 if limit is reached...
		MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator lastEvent =
			mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end();
		lastEvent--;

		//preserve events of current channel
		readLockMessaging();
		while ((lastEvent != mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin()) &&
				((*lastEvent)->get_channel_id() == messaging_current_servicekey)) {
			--lastEvent;
		}
		unlockMessaging();
		unlockEvents();
		deleteEvent((*lastEvent)->uniqueKey());
	}
	else
		unlockEvents();
	writeLockEvents();
	mySIeventsOrderUniqueKey.insert(std::make_pair(e->uniqueKey(), e));

	mySIeventsNVODorderUniqueKey.insert(std::make_pair(e->uniqueKey(), e));
	unlockEvents();
	if (e->times.size())
	{
		// diese beiden Mengen enthalten nur Events mit Zeiten
		writeLockEvents();
		mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.insert(e);
		mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.insert(e);
		unlockEvents();
	}
}

static void removeOldEvents(const long seconds)
{
	std::vector<event_id_t> to_delete;

	// Alte events loeschen
	time_t zeit = time(NULL);

	readLockEvents();

	MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator e = mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin();

	while ((e != mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end()) && (!messaging_zap_detected)) {
		bool goodtimefound = false;
		for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t) {
			if (t->startzeit + (long)t->dauer >= zeit - seconds) {
				goodtimefound=true;
				// one time found -> exit times loop
				break;
			}
		}

		if (!goodtimefound)
			to_delete.push_back((*e)->uniqueKey());
		++e;
	}
	unlockEvents();

	for (std::vector<event_id_t>::iterator i = to_delete.begin(); i != to_delete.end(); ++i)
		deleteEvent(*i);

	return;
}

//------------------------------------------------------------
// misc. functions
//------------------------------------------------------------
static const SIevent& findSIeventForEventUniqueKey(const event_id_t eventUniqueKey)
{
	// Event (eventid) suchen
	MySIeventsOrderUniqueKey::iterator e = mySIeventsOrderUniqueKey.find(eventUniqueKey);

	if (e != mySIeventsOrderUniqueKey.end())
		return *(e->second);

	return nullEvt;
}

static const SIevent& findActualSIeventForServiceUniqueKey(const t_channel_id serviceUniqueKey, SItime& zeit, long plusminus = 0, unsigned *flag = 0)
{
	time_t azeit = time(NULL);

	if (flag != 0)
		*flag = 0;

	for (MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator e = mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin(); e != mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end(); ++e)
		if ((*e)->get_channel_id() == serviceUniqueKey)
		{
			if (flag != 0)
				*flag |= CSectionsdClient::epgflags::has_anything; // berhaupt was da...

//			for (SItimes::reverse_iterator t = (*e)->times.rend(); t != (*e)->times.rbegin(); t--) {
			for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t) {
				if ((long)(azeit + plusminus) < (long)(t->startzeit + t->dauer))
				{
					if (flag != 0)
						*flag |= CSectionsdClient::epgflags::has_later; // later events are present...

					if (t->startzeit <= (long)(azeit + plusminus))
					{
						//printf("azeit %d, startzeit+t->dauer %d \n", azeit, (long)(t->startzeit+t->dauer) );

						if (flag != 0)
							*flag |= CSectionsdClient::epgflags::has_current; // aktuelles event da...

						zeit = *t;

						return *(*e);
					}
				}
			}
		}

	return nullEvt;
}

static const SIevent& findNextSIeventForServiceUniqueKey(const t_channel_id serviceUniqueKey, SItime& zeit)
{
	time_t azeit = time(NULL);

	for (MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator e = mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin(); e != mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end(); ++e)
		if ((*e)->get_channel_id() == serviceUniqueKey)
		{
			for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t)
				if ((long)(azeit) < (long)(t->startzeit + t->dauer))
				{
					zeit = *t;
					return *(*e);
				}
		}

	return nullEvt;
}

// Sucht das naechste Event anhand unique key und Startzeit
static const SIevent &findNextSIevent(const event_id_t uniqueKey, SItime &zeit)
{
	MySIeventsOrderUniqueKey::iterator eFirst = mySIeventsOrderUniqueKey.find(uniqueKey);

	if (eFirst != mySIeventsOrderUniqueKey.end())
	{
		SItimes::iterator nextnvodtimes = eFirst->second->times.end();
		SItimes::iterator nexttimes = eFirst->second->times.end();

		if (eFirst->second->times.size() > 1)
		{
			//find next nvod
			nextnvodtimes = eFirst->second->times.begin();
			while ( nextnvodtimes != eFirst->second->times.end() ) {
				if ( nextnvodtimes->startzeit == zeit.startzeit )
					break;
				else
					++nextnvodtimes;
			}
		}

		MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator eNext;

		//if ((nextnvodtimes != eFirst->second->times.begin()) && (nextnvodtimes != eFirst->second->times.end())) {
		//Startzeit not first - we can't use the ordered list...
		for (MySIeventsOrderFirstEndTimeServiceIDEventUniqueKey::iterator e = mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin(); e !=
				mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end(); ++e ) {
			if ((*e)->get_channel_id() == eFirst->second->get_channel_id()) {
				for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t) {
					if (t->startzeit > zeit.startzeit) {
						if (nexttimes != eFirst->second->times.end()) {
							if (t->startzeit < nexttimes->startzeit) {
								eNext = e;
								nexttimes = t;
							}
						}
						else {
							eNext = e;
							nexttimes = t;
						}
					}
				}
			}
		}
		/*		} else {
					//find next normal
					eNext = mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.find(eFirst->second);
					eNext++;

					if (eNext != mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.end())
					{
						if ((*eNext)->get_channel_id() == eFirst->second->get_channel_id())
							nexttimes = (*eNext)->times.begin();
					}
				}
		*/
		if (nextnvodtimes != eFirst->second->times.end())
			++nextnvodtimes;
		//Compare
		if (nexttimes != eFirst->second->times.end()) {
			if (nextnvodtimes != eFirst->second->times.end()) {
				//both times are set - take the first
				if (nexttimes->startzeit < nextnvodtimes->startzeit) {
					zeit = *nexttimes;
					return *(*eNext);

				} else {
					zeit = *nextnvodtimes;
					return *(eFirst->second);
				}
			} else {
				//only nexttimes set
				zeit = *nexttimes;
				return *(*eNext);
			}
		} else if (nextnvodtimes != eFirst->second->times.end()) {
			//only nextnvodtimes set
			zeit = *nextnvodtimes;
			return *(eFirst->second);
		}
	}

	return nullEvt;
}

/*
 * communication with sectionsdclient:
 */

inline bool readNbytes(int fd, char *buf, const size_t numberOfBytes, const time_t timeoutInSeconds)
{
	timeval timeout;
	timeout.tv_sec  = timeoutInSeconds;
	timeout.tv_usec = 0;
	return receive_data(fd, buf, numberOfBytes, timeout);
}

inline bool writeNbytes(int fd, const char *buf,  const size_t numberOfBytes, const time_t timeoutInSeconds)
{
	timeval timeout;
	timeout.tv_sec  = timeoutInSeconds;
	timeout.tv_usec = 0;
	return send_data(fd, buf, numberOfBytes, timeout);
}

/* send back an empty response */
static void sendEmptyResponse(int connfd, char *, const unsigned)
{
	struct sectionsd::msgResponseHeader msgResponse;
	msgResponse.dataLength = 0;
	writeNbytes(connfd, (const char *)&msgResponse, sizeof(msgResponse), WRITE_TIMEOUT_IN_SECONDS);
	return;
}

//---------------------------------------------------------------------
//			connection-thread
// handles incoming requests
//---------------------------------------------------------------------

static void commandPauseScanning(int connfd, char *data, const unsigned dataLength)
{
	if (dataLength != sizeof(int))
		return;

	int pause = *(int *)data;

	xprintf("Request of %s scanning (now %s).\n", pause ? "stop" : "continue", scanning ? "scanning" : "idle");

	if (scanning && pause)
	{
#if 0
		threadCN.request_pause();
		threadEIT.request_pause();
#ifdef ENABLE_FREESATEPG
		dmxFSEIT.request_pause();
#endif
#ifdef ENABLE_SDT
		dmxSDT.request_pause();
#endif
#endif
		scanning = 0;
	}
	else if (!pause && !scanning)
	{
#if 0
		threadCN.request_unpause();
		threadEIT.request_unpause();
#ifdef ENABLE_FREESATEPG
		dmxFSEIT.request_unpause();
#endif
#ifdef ENABLE_SDT
		dmxSDT.request_unpause();
#endif
#endif
		writeLockEvents();
		delete myCurrentEvent;
		myCurrentEvent = NULL;
		delete myNextEvent;
		myNextEvent = NULL;
		unlockEvents();

		writeLockMessaging();
		messaging_have_CN = 0x00;
		messaging_got_CN = 0x00;
		unlockMessaging();

		scanning = 1;
		if (!ntpenable)
		{
			pthread_mutex_lock(&timeThreadSleepMutex);
			pthread_cond_broadcast(&timeThreadSleepCond);
			pthread_mutex_unlock(&timeThreadSleepMutex);
		}

		threadCN.change(0);
		threadEIT.change(0);
#ifdef ENABLE_FREESATEPG
		dmxFSEIT.change(0);
#endif
#ifdef ENABLE_SDT
		threadSDT.change(0);
#endif
	}
	sendEmptyResponse(connfd, NULL, 0);
}

static void commandserviceChanged(int connfd, char *data, const unsigned dataLength)
{
	t_channel_id uniqueServiceKey = 0;

	sendEmptyResponse(connfd, NULL, 0);
	if (dataLength != sizeof(sectionsd::commandSetServiceChanged))
		goto out;

	uniqueServiceKey = (((sectionsd::commandSetServiceChanged *)data)->channel_id);
	uniqueServiceKey &= 0xFFFFFFFFFFFFULL;

	dprintf("[sectionsd] commandserviceChanged: Service changed to " PRINTF_CHANNEL_ID_TYPE "\n", uniqueServiceKey);
xprintf("[sectionsd] commandserviceChanged: Service change to " PRINTF_CHANNEL_ID_TYPE "\n\n", uniqueServiceKey);

	static t_channel_id time_trigger_last = 0;

	if (uniqueServiceKey && messaging_current_servicekey != uniqueServiceKey) {
		dvb_time_update = !checkNoDVBTimelist(uniqueServiceKey);
		dprintf("[sectionsd] commandserviceChanged: DVB time update is %s\n", dvb_time_update ? "allowed" : "blocked!");

		channel_is_blacklisted = checkBlacklist(uniqueServiceKey);
		dprintf("[sectionsd] commandserviceChanged: service is %s\n", channel_is_blacklisted ? "filtered!" : "not filtered");

		writeLockEvents();
		delete myCurrentEvent;
		myCurrentEvent = NULL;
		delete myNextEvent;
		myNextEvent = NULL;
		unlockEvents();

		writeLockMessaging();
		messaging_current_servicekey = uniqueServiceKey;
		messaging_have_CN = 0x00;
		messaging_got_CN = 0x00;
		messaging_zap_detected = true;
		unlockMessaging();

		threadCN.setCurrentService(messaging_current_servicekey);
		threadEIT.setCurrentService(messaging_current_servicekey);
#ifdef ENABLE_FREESATEPG
		dmxFSEIT.setCurrentService(messaging_current_servicekey);
#endif
#ifdef ENABLE_SDT
		threadSDT.setCurrentService(messaging_current_servicekey);
#endif
		if (time_trigger_last != (messaging_current_servicekey & 0xFFFFFFFF0000ULL)) {
			time_trigger_last = messaging_current_servicekey & 0xFFFFFFFF0000ULL;
			pthread_mutex_lock(&timeThreadSleepMutex);
			pthread_cond_broadcast(&timeThreadSleepCond);
			pthread_mutex_unlock(&timeThreadSleepMutex);
		}
	}
	else
		dprintf("[sectionsd] commandserviceChanged: no change...\n");

out:
	//sendEmptyResponse(connfd, NULL, 0);
xprintf("[sectionsd] commandserviceChanged: Service changed to " PRINTF_CHANNEL_ID_TYPE "\n\n", uniqueServiceKey);
}

static void commandGetIsScanningActive(int connfd, char* /*data*/, const unsigned /*dataLength*/)
{
	struct sectionsd::msgResponseHeader responseHeader;

	responseHeader.dataLength = sizeof(scanning);

	if (writeNbytes(connfd, (const char *)&responseHeader, sizeof(responseHeader), WRITE_TIMEOUT_IN_SECONDS) == true)
	{
		writeNbytes(connfd, (const char *)&scanning, responseHeader.dataLength, WRITE_TIMEOUT_IN_SECONDS);
	}
	else
		dputs("[sectionsd] Fehler/Timeout bei write");
}

static void commandDumpStatusInformation(int /*connfd*/, char* /*data*/, const unsigned /*dataLength*/)
{
	dputs("Request of status information");

	readLockEvents();

	unsigned anzEvents = mySIeventsOrderUniqueKey.size();

	unsigned anzNVODevents = mySIeventsNVODorderUniqueKey.size();

	unsigned anzMetaServices = mySIeventUniqueKeysMetaOrderServiceUniqueKey.size();

	unlockEvents();

	readLockServices();

	unsigned anzServices = mySIservicesOrderUniqueKey.size();

	unsigned anzNVODservices = mySIservicesNVODorderUniqueKey.size();

	//  unsigned anzServices=services.size();
	unlockServices();

	struct mallinfo speicherinfo = mallinfo();

	//  struct rusage resourceUsage;
	//  getrusage(RUSAGE_CHILDREN, &resourceUsage);
	//  getrusage(RUSAGE_SELF, &resourceUsage);
	time_t zeit = time(NULL);

#define MAX_SIZE_STATI	2024
	char stati[MAX_SIZE_STATI];

	snprintf(stati, MAX_SIZE_STATI,
		 "$Id: sectionsd.cpp,v 1.305 2009/07/30 12:41:39 seife Exp $\n"
		 "Current time: %s"
		 "Hours to cache: %ld\n"
		 "Hours to cache extended text: %ld\n"
		 "Events are old %ldmin after their end time\n"
		 "Number of cached services: %u\n"
		 "Number of cached nvod-services: %u\n"
		 "Number of cached events: %u\n"
		 "Number of cached nvod-events: %u\n"
		 "Number of cached meta-services: %u\n"
		 //    "Resource-usage: maxrss: %ld ixrss: %ld idrss: %ld isrss: %ld\n"
		 "Total size of memory occupied by chunks handed out by malloc: %d (%dkb)\n"
		 "Total bytes memory allocated with `sbrk' by malloc, in bytes: %d (%dkb)\n"
#ifdef ENABLE_FREESATEPG
		 "FreeSat enabled\n"
#else
		 ""
#endif
		 ,ctime(&zeit),
		 secondsToCache / (60*60L), secondsExtendedTextCache / (60*60L), oldEventsAre / 60, anzServices, anzNVODservices, anzEvents, anzNVODevents, anzMetaServices,
		 //    resourceUsage.ru_maxrss, resourceUsage.ru_ixrss, resourceUsage.ru_idrss, resourceUsage.ru_isrss,
		 speicherinfo.uordblks, speicherinfo.uordblks / 1024,
		 speicherinfo.arena, speicherinfo.arena / 1024
		);
	printf("%s\n", stati);
	return ;
}


static void commandGetIsTimeSet(int connfd, char* /*data*/, const unsigned /*dataLength*/)
{
	sectionsd::responseIsTimeSet rmsg;

	rmsg.IsTimeSet = timeset;

	dprintf("Request of Time-Is-Set %d\n", rmsg.IsTimeSet);

	struct sectionsd::msgResponseHeader responseHeader;

	responseHeader.dataLength = sizeof(rmsg);

	if (writeNbytes(connfd, (const char *)&responseHeader, sizeof(responseHeader), WRITE_TIMEOUT_IN_SECONDS) == true)
	{
		writeNbytes(connfd, (const char *)&rmsg, responseHeader.dataLength, WRITE_TIMEOUT_IN_SECONDS);
	}
	else
		dputs("[sectionsd] Fehler/Timeout bei write");
}


static void commandRegisterEventClient(int /*connfd*/, char *data, const unsigned dataLength)
{
	if (dataLength == sizeof(CEventServer::commandRegisterEvent))
	{
		eventServer->registerEvent2(((CEventServer::commandRegisterEvent*)data)->eventID, ((CEventServer::commandRegisterEvent*)data)->clientID, ((CEventServer::commandRegisterEvent*)data)->udsName);

		if (((CEventServer::commandRegisterEvent*)data)->eventID == CSectionsdClient::EVT_TIMESET)
			messaging_neutrino_sets_time = true;
	}
}

static void commandUnRegisterEventClient(int /*connfd*/, char *data, const unsigned dataLength)
{
	if (dataLength == sizeof(CEventServer::commandUnRegisterEvent))
		eventServer->unRegisterEvent2(((CEventServer::commandUnRegisterEvent*)data)->eventID, ((CEventServer::commandUnRegisterEvent*)data)->clientID);
}

static void commandSetConfig(int connfd, char *data, const unsigned /*dataLength*/)
{
	sendEmptyResponse(connfd, NULL, 0);

	struct sectionsd::commandSetConfig *pmsg;

	pmsg = (struct sectionsd::commandSetConfig *)data;

	/* writeLockEvents not needed because write lock will block if read lock active */
	readLockEvents();
	secondsToCache = (long)(pmsg->epg_cache)*24*60L*60L;
	oldEventsAre = (long)(pmsg->epg_old_events)*60L*60L;
	secondsExtendedTextCache = (long)(pmsg->epg_extendedcache)*60L*60L;
	max_events = pmsg->epg_max_events;
	unlockEvents();

	bool time_wakeup = false;
	if (ntpserver.compare((std::string)&data[sizeof(struct sectionsd::commandSetConfig)])) {
		time_wakeup = true;
	}
	if (ntprefresh != pmsg->network_ntprefresh) {
		dprintf("new network_ntprefresh = %d\n", pmsg->network_ntprefresh);
		time_wakeup = true;
	}
	if (ntpenable ^ (pmsg->network_ntpenable == 1))	{
		dprintf("new network_ntpenable = %d\n", pmsg->network_ntpenable);
		time_wakeup = true;
	}

	if(time_wakeup) {
		pthread_mutex_lock(&timeThreadSleepMutex);

		ntpserver = (std::string)&data[sizeof(struct sectionsd::commandSetConfig)];
		dprintf("new network_ntpserver = %s\n", ntpserver.c_str());
		ntp_system_cmd = ntp_system_cmd_prefix + ntpserver;
		ntprefresh = pmsg->network_ntprefresh;
		ntpenable = (pmsg->network_ntpenable == 1);
		if (timeset) {
			// wake up time thread
			pthread_cond_broadcast(&timeThreadSleepCond);
		}
		pthread_mutex_unlock(&timeThreadSleepMutex);
	}

	epg_dir= (std::string)&data[sizeof(struct sectionsd::commandSetConfig) + strlen(&data[sizeof(struct sectionsd::commandSetConfig)]) + 1];
}

static void deleteSIexceptEPG()
{
	threadEIT.dropCachedSectionIDs();
	threadEIT.change(0);
#ifdef ENABLE_SDT
	writeLockServices();
	mySIservicesOrderUniqueKey.clear();
	unlockServices();
	threadSDT.dropCachedSectionIDs();
	threadSDT.change(0);
#endif
#ifdef ENABLE_FREESATEPG
	dmxFSEIT.setCurrentService(messaging_current_servicekey);
	dmxFSEIT.change(0);
#endif
}

static void commandFreeMemory(int connfd, char * /*data*/, const unsigned /*dataLength*/)
{
	sendEmptyResponse(connfd, NULL, 0);

	deleteSIexceptEPG();

	writeLockEvents();

#ifndef USE_BOOST_SHARED_PTR
	std::set<SIeventPtr> allevents;

	allevents.insert(mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.begin(), mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.end());
	/* this probably not needed, but takes only additional ~2 seconds
	 * with even count > 70000 */
	allevents.insert(mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.begin(), mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.end());
	MySIeventsOrderUniqueKey::iterator it;
	for(it = mySIeventsOrderUniqueKey.begin(); it != mySIeventsOrderUniqueKey.end(); ++it)
		allevents.insert(it->second);
	for(it = mySIeventsNVODorderUniqueKey.begin(); it != mySIeventsNVODorderUniqueKey.end(); ++it)
		allevents.insert(it->second);

	for(std::set<SIeventPtr>::iterator ait = allevents.begin(); ait != allevents.end(); ++ait)
		delete (*ait);
#endif
	mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.clear();
	mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.clear();
	mySIeventsOrderUniqueKey.clear();
	mySIeventsNVODorderUniqueKey.clear();

	unlockEvents();

	//FIXME debug
	struct mallinfo meminfo = mallinfo();
	printf("total size of memory occupied by chunks handed out by malloc: %d\n"
			"total bytes memory allocated with `sbrk' by malloc, in bytes: %d (%dkB)\n",
			meminfo.uordblks, meminfo.arena, meminfo.arena / 1024);
}

static void commandReadSIfromXML(int connfd, char *data, const unsigned dataLength)
{
	pthread_t thrInsert;

	sendEmptyResponse(connfd, NULL, 0);

	if (dataLength > 100)
		return ;

	writeLockMessaging();
	data[dataLength] = '\0';
	epg_dir = (std::string)data + "/";
	unlockMessaging();


	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (pthread_create (&thrInsert, &attr, insertEventsfromFile, (void *)epg_dir.c_str() ))
	{
		perror("sectionsd: pthread_create()");
	}

	pthread_attr_destroy(&attr);
}

static void commandWriteSI2XML(int connfd, char *data, const unsigned dataLength)
{
	char epgdir[100] = "";

	sendEmptyResponse(connfd, NULL, 0);

	if (dataLength > 100)
		return;

	strncpy(epgdir, data, dataLength);
	epgdir[dataLength] = '\0';

	writeEventsToFile(epgdir);

	eventServer->sendEvent(CSectionsdClient::EVT_WRITE_SI_FINISHED, CEventServer::INITID_SECTIONSD);
}

struct s_cmd_table
{
	void (*cmd)(int connfd, char *, const unsigned);
	std::string sCmd;
};

static s_cmd_table connectionCommands[sectionsd::numberOfCommands] = {
	{	commandDumpStatusInformation,		"commandDumpStatusInformation"		},
	{	sendEmptyResponse,        "commandAllEventsChannelIDSearch"	},
	{	commandPauseScanning,                   "commandPauseScanning"			},
	{	commandGetIsScanningActive,             "commandGetIsScanningActive"		},
	{	sendEmptyResponse,              "commandActualEPGchannelID"		},
	{	sendEmptyResponse,                  "commandEventListTVids"			},
	{	sendEmptyResponse,               "commandEventListRadioIDs"		},
	{	sendEmptyResponse,				"commandCurrentNextInfoChannelID"	},
	{	sendEmptyResponse,                        "commandEPGepgID"			},
	{	sendEmptyResponse,                   "commandEPGepgIDshort"			},
	{	sendEmptyResponse,          "commandComponentTagsUniqueKey"		},
	{	sendEmptyResponse,              "commandAllEventsChannelID"		},
	{	sendEmptyResponse,                "commandTimesNVODservice"		},
	{	commandGetIsTimeSet,                    "commandGetIsTimeSet"			},
	{	commandserviceChanged,                  "commandserviceChanged"			},
	{	sendEmptyResponse,     "commandLinkageDescriptorsUniqueKey"	},
	{	commandRegisterEventClient,             "commandRegisterEventClient"		},
	{	commandUnRegisterEventClient,           "commandUnRegisterEventClient"		},
	{	sendEmptyResponse,                          "commandSetPrivatePid"			},
	{	commandFreeMemory,			"commandFreeMemory"			},
	{	commandReadSIfromXML,			"commandReadSIfromXML"			},
	{	commandWriteSI2XML,			"commandWriteSI2XML"			},
	{	commandSetConfig,			"commandSetConfig"			},
};

bool sectionsd_parse_command(CBasicMessage::Header &rmsg, int connfd)
{
	dprintf("Connection from UDS\n");

	struct sectionsd::msgRequestHeader header;

	memmove(&header, &rmsg, sizeof(CBasicMessage::Header));
	memset(((char *)&header) + sizeof(CBasicMessage::Header), 0, sizeof(header) - sizeof(CBasicMessage::Header));

	bool readbytes = readNbytes(connfd, ((char *)&header) + sizeof(CBasicMessage::Header), sizeof(header) - sizeof(CBasicMessage::Header), READ_TIMEOUT_IN_SECONDS);

	if (readbytes == true)
	{
		dprintf("version: %hhd, cmd: %hhd, numbytes: %d\n", header.version, header.command, readbytes);

		if (header.command < sectionsd::numberOfCommands)
		{
			dprintf("data length: %hd\n", header.dataLength);
			char *data = new char[header.dataLength + 1];

			if (!data)
				fprintf(stderr, "low on memory!\n");
			else
			{
				bool rc = true;

				if (header.dataLength)
					rc = readNbytes(connfd, data, header.dataLength, READ_TIMEOUT_IN_SECONDS);

				if (rc == true)
				{
					dprintf("%s\n", connectionCommands[header.command].sCmd.c_str());
					if(connectionCommands[header.command].cmd == sendEmptyResponse)
						printf("sectionsd_parse_command: UNUSED cmd used: %d (%s)\n", header.command, connectionCommands[header.command].sCmd.c_str());
					connectionCommands[header.command].cmd(connfd, data, header.dataLength);
				}

				delete[] data;
			}
		}
		else
			dputs("Unknown format or version of request!");
	}

	return true;
}

static void dump_sched_info(std::string label)
{
	int policy;
	struct sched_param parm;
	int rc = pthread_getschedparam(pthread_self(), &policy, &parm);
	printf("%s: getschedparam %d policy %d prio %d\n", label.c_str(), rc, policy, parm.sched_priority);
}

//---------------------------------------------------------------------
//			Time-thread
// updates system time according TOT every 30 minutes
//---------------------------------------------------------------------

static void sendTimeEvent(bool dvb, time_t tim = 0)
{
	time_t actTime = time(NULL);
	if(dvb) {
		struct tm *tmTime = localtime(&actTime);
		xprintf("[%sThread] - current: %02d.%02d.%04d %02d:%02d:%02d, dvb: %s", "time",
				tmTime->tm_mday, tmTime->tm_mon+1, tmTime->tm_year+1900, tmTime->tm_hour, tmTime->tm_min, tmTime->tm_sec, ctime(&tim));
		actTime = tim;
	}
	pthread_mutex_lock(&timeIsSetMutex);
	timeset = true;
	pthread_cond_broadcast(&timeIsSetCond);
	pthread_mutex_unlock(&timeIsSetMutex );
	eventServer->sendEvent(CSectionsdClient::EVT_TIMESET, CEventServer::INITID_SECTIONSD, &actTime, sizeof(actTime) );
}

static void *timeThread(void *)
{
	UTC_t UTC;
	time_t tim;
	unsigned int seconds;
	bool first_time = true; /* we don't sleep the first time (we try to get a TOT header) */
	struct timespec restartWait;
	struct timeval now;
	bool time_ntp = false;
	bool success = true;

	dprintf("[%sThread] pid %d (%lu) start\n", "time", getpid(), pthread_self());

	while(!sectionsd_stop)
	{
		while (!scanning || !reader_ready) {
			if(sectionsd_stop)
				break;
			sleep(1);
		}

		if ( ntpenable && system( ntp_system_cmd.c_str() ) == 0)
		{
			first_time = false;
			time_ntp = true;
			sendTimeEvent(false);
		} else {
			if (dvb_time_update) {
xprintf("timeThread: getting UTC\n");
				success = getUTC(&UTC, first_time); // for first time, get TDT, then TOT
xprintf("timeThread: getting UTC done : %d\n", success);
				if (success)
				{
					tim = changeUTCtoCtime((const unsigned char *) &UTC);
					time_ntp = false;
					sendTimeEvent(true, tim);

					if (tim) {
						if ((!messaging_neutrino_sets_time) && (geteuid() == 0)) {
							struct timeval tv;
							tv.tv_sec = tim;
							tv.tv_usec = 0;
							if (settimeofday(&tv, NULL) < 0) {
								perror("[sectionsd] settimeofday");
								pthread_exit(NULL);
							}
						}
					}
				}
			}
		}

		if (timeset && dvb_time_update) {
			if (first_time)
				seconds = 5; /* retry a second time immediately */
			else
				seconds = ntprefresh * 60;

			if(time_ntp) {
				xprintf("[%sThread] Time set via NTP, going to sleep for %d seconds.\n", "time", seconds);
			}
			else {
				xprintf("[%sThread] Time %sset via DVB(%s), going to sleep for %d seconds.\n",
					"time", success?"":"not ", first_time?"TDT":"TOT", seconds);
			}
			first_time = false;
		}
		else {
			if (!first_time) {
				/* time was already set, no need to do it again soon when DVB time-blocked channel is tuned */
				seconds = ntprefresh * 60;
			}
			else if (!scanning) {
				seconds = 60;
			}
			else {
				seconds = 1;
			}
			if (!dvb_time_update && !first_time) {
				xprintf("[%sThread] Time NOT set via DVB due to blocked channel, going to sleep for %d seconds.\n", "time", seconds);
			}
		}
		if(sectionsd_stop)
			break;

xprintf("timeThread: going to sleep for %d sec\n\n", seconds);
		gettimeofday(&now, NULL);
		TIMEVAL_TO_TIMESPEC(&now, &restartWait);
		restartWait.tv_sec += seconds;
		pthread_mutex_lock( &timeThreadSleepMutex );
		int ret = pthread_cond_timedwait( &timeThreadSleepCond, &timeThreadSleepMutex, &restartWait );
		pthread_mutex_unlock( &timeThreadSleepMutex );
		if (ret == ETIMEDOUT)
		{
			dprintf("TDT-Thread sleeping is over - no signal received\n");
		}
		else if (ret == EINTR)
		{
			dprintf("TDT-Thread sleeping interrupted\n");
		}
	}

	printf("[sectionsd] timeThread ended\n");
	pthread_exit(NULL);
}

/********************************************************************************/
/* abstract CSectionThread functions						*/
/********************************************************************************/
/* sleep for sleep_time seconds, forever if sleep_time = 0 */
int CSectionThread::Sleep()
{
	int rs;
	struct timespec abs_wait;
	struct timeval now;

	if(sleep_time) {
		gettimeofday(&now, NULL);
		TIMEVAL_TO_TIMESPEC(&now, &abs_wait);
		abs_wait.tv_sec += sleep_time;
	}
	xprintf("%s: going to sleep for %d seconds...\n", name.c_str(), sleep_time);
	pthread_mutex_lock(&start_stop_mutex);

	beforeWait();
	if(sleep_time)
		rs = pthread_cond_timedwait( &change_cond, &start_stop_mutex, &abs_wait );
	else
		rs = pthread_cond_wait(&change_cond, &start_stop_mutex);

	afterWait();

	pthread_mutex_unlock( &start_stop_mutex );
	return rs;
}

/* common thread main function */
void CSectionThread::run()
{
	xprintf("%s::run:: starting, pid %d (%lu)\n", name.c_str(), getpid(), pthread_self());
	if (sections_debug)
		dump_sched_info(name.c_str());

	addFilters();

	if (wait_for_time) {
		waitForTimeset();
		xprintf("%s::run:: time set.\n", name.c_str());
	}

	DMX::start();

	while (running) {
		if (shouldSleep()) {
#ifdef DEBUG_EIT_THREAD
			xprintf("%s: going to sleep %d seconds, running %d scanning %d blacklisted %d events %d\n",
					name.c_str(), sleep_time, running, scanning, channel_is_blacklisted, event_count);
#endif
			event_count = 0;

			beforeSleep();
			int rs = 0;
			do {
				real_pause();
				rs = Sleep();
#ifdef DEBUG_EIT_THREAD
				xprintf("%s: wakeup, running %d scanning %d blacklisted %d reason %d\n\n",
						name.c_str(), running, scanning, channel_is_blacklisted, rs);
#endif
			} while (checkSleep());

			if(!running)
				break;

			afterSleep();

			if (rs == ETIMEDOUT)
				change(0); // -> restart, FIXME

			sendToSleepNow = false;
		}

		int rc = getSection(static_buf, timeoutInMSeconds, timeoutsDMX);

		processSection(rc);

		time_t zeit = time_monotonic();
		bool need_change = false;

		if(timeoutsDMX < 0 || timeoutsDMX >= skipTimeouts) {
#ifdef DEBUG_EIT_THREAD
			xprintf("%s: skipping to next filter %d from %d (timeouts %d)\n",
					name.c_str(), filter_index+1, filters.size(), timeoutsDMX);
#endif
			timeoutsDMX = 0;
			need_change = true;
		}
		if (zeit > lastChanged + skipTime) {
#ifdef DEBUG_EIT_THREAD
			xprintf("%s: skipping to next filter %d from %d (seconds %d)\n", 
					name.c_str(), filter_index+1, filters.size(), (int) (zeit - lastChanged));
#endif
			need_change = true;
		}
		if(running && need_change && scanning) {
			readLockMessaging();
			if (!next_filter())
				sendToSleepNow = true;
			unlockMessaging();
		}
	} // while running
	delete[] static_buf;
	cleanup();
	printf("[sectionsd] %s ended\n", name.c_str());
	pthread_exit(NULL);
}

/********************************************************************************/
/* abstract CEventsThread functions						*/
/********************************************************************************/
bool CEventsThread::addEvents()
{
	SIsectionEIT eit(static_buf);

	if (!eit.is_parsed())
		return false;

	dprintf("[eitThread] adding %d events [table 0x%x] (begin)\n", eit.events().size(), eit.getTableId());
	time_t zeit = time(NULL);

	for (SIevents::iterator e = eit.events().begin(); e != eit.events().end(); ++e) {
		if (!(e->times.empty())) {
			if ( ( e->times.begin()->startzeit < zeit + secondsToCache ) &&
					( ( e->times.begin()->startzeit + (long)e->times.begin()->dauer ) > zeit - oldEventsAre ) )
			{
				addEvent(*e, zeit, e->table_id == 0x4e);
				event_count++;
			}
		} else {
			// pruefen ob nvod event
			readLockServices();
			MySIservicesNVODorderUniqueKey::iterator si = mySIservicesNVODorderUniqueKey.find(e->get_channel_id());

			if (si != mySIservicesNVODorderUniqueKey.end()) {
				// Ist ein nvod-event
				writeLockEvents();

				for (SInvodReferences::iterator i = si->second->nvods.begin(); i != si->second->nvods.end(); ++i)
					mySIeventUniqueKeysMetaOrderServiceUniqueKey.insert(std::make_pair(i->uniqueKey(), e->uniqueKey()));

				unlockEvents();
				addNVODevent(*e);
			}
			unlockServices();
		}
	} // for
	return true;
}

/* default check if thread should go to sleep */
bool CEventsThread::shouldSleep()
{
	return (sendToSleepNow || !scanning || channel_is_blacklisted);
}

/* default check if thread should continue to sleep */
bool CEventsThread::checkSleep()
{
	return (running && (!scanning || channel_is_blacklisted));
}

/* default section process */
void CEventsThread::processSection(int rc)
{
	if(rc <= 0)
		return;
	addEvents();
}

/********************************************************************************/
/* EIT thread to read other TS CN + all scheduled events 			*/
/********************************************************************************/
CEitThread::CEitThread()
	: CEventsThread("eitThread")
{
}

/* EIT thread hooks */
void CEitThread::addFilters()
{
	/* These filters are a bit tricky (index numbers):
	   - 0   Dummy filter, to make this thread sleep for some seconds
	   - 1   then get other TS's current/next (this TS's cur/next are
	   handled in dmxCN)
	   - 2/3 then get scheduled events on this TS
	   - 4   then get the other TS's scheduled events,
	   - 4ab (in two steps to reduce the POLLERRs on the DMX device)
	   */
	//addfilter(0x00, 0x00); //0 dummy filter
	addfilter(0x50, 0xf0); //1  current TS, scheduled
	addfilter(0x4f, 0xff); //2  other TS, current/next
#if 1
	addfilter(0x60, 0xf1); //3a other TS, scheduled, even
	addfilter(0x61, 0xf1); //3b other TS, scheduled, odd
#else
	addfilter(0x60, 0xf0); //3  other TS, scheduled
#endif
}

void CEitThread::beforeSleep()
{
	xprintf("%s: CScheduledThread::beforeSleep()\n", name.c_str());
	writeLockMessaging();
	messaging_zap_detected = false;
	unlockMessaging();
}

/********************************************************************************/
/* CN thread to read current TS CN events 					*/
/********************************************************************************/
CCNThread::CCNThread()
	: CEventsThread("cnThread")
{
	sleep_time = 0;
	cache = false;
	skipTimeouts = 5000 / timeoutInMSeconds; // 5 seconds
	skipTime = TIME_EIT_VERSION_WAIT;

	updating = false;
	eitDmx = new cDemux(0);
	eitDmx->Open(DMX_PSI_CHANNEL);
}

/* CN thread hooks */
void CCNThread::cleanup()
{
	delete eitDmx;
}

void CCNThread::addFilters()
{
	addfilter(0x4e, 0xff); //0  current TS, current/next
}

void CCNThread::beforeWait()
{
	xprintf("%s: set eit update filter, service = 0x%016llx, current version 0x%x got events %d (%s)\n",
			name.c_str(), messaging_current_servicekey, eit_version, messaging_have_CN,
			updating ? "active" : "not active");

	if (updating || eit_version == 0xff)
		return;

	updating = true;

	unsigned char filter[DMX_FILTER_SIZE];
	unsigned char mask[DMX_FILTER_SIZE];
	unsigned char mode[DMX_FILTER_SIZE];
	memset(&filter, 0, DMX_FILTER_SIZE);
	memset(&mask, 0, DMX_FILTER_SIZE);
	memset(&mode, 0, DMX_FILTER_SIZE);

	filter[0] = 0x4e;   /* table_id */
	filter[1] = (unsigned char)(current_service >> 8);
	filter[2] = (unsigned char) current_service;

	mask[0] = 0xFF;
	mask[1] = 0xFF;
	mask[2] = 0xFF;

	filter[3] = (eit_version << 1) | 0x01;
	mask[3] = (0x1F << 1) | 0x01;
	mode[3] = 0x1F << 1;
	eitDmx->sectionFilter(0x12, filter, mask, 4, 0 /*timeout*/, mode);
}

void CCNThread::afterWait()
{
	xprintf("%s: stop eit update filter (%s)\n", name.c_str(), updating ? "active" : "not active");
	if(updating) {
		updating = false;
		eitDmx->Stop();
	}
}

void CCNThread::beforeSleep()
{
	if (sendToSleepNow && messaging_have_CN == 0x00) {
		/* send a "no epg" event anyway before going to sleep */
		sendCNEvent();
	}
}

void CCNThread::processSection(int rc)
{
	if(rc <= 0)
		return;

	addEvents();
	readLockMessaging();
	if (messaging_got_CN != messaging_have_CN) {
		unlockMessaging();
		writeLockMessaging();
		messaging_have_CN = messaging_got_CN;
		unlockMessaging();

#ifdef DEBUG_CN_THREAD
		xprintf("%s: have CN: timeoutsDMX %d messaging_have_CN %x messaging_got_CN %x\n\n",
				name.c_str(), timeoutsDMX, messaging_have_CN, messaging_got_CN);
#endif
		dprintf("[cnThread] got current_next (0x%x) - sending event!\n", messaging_have_CN);
		sendCNEvent();
		lastChanged = time_monotonic();
	}
	else
		unlockMessaging();
}

/* CN private functions */
bool CCNThread::checkUpdate()
{
	if(!updating)
		return false;

	unsigned char buf[MAX_SECTION_LENGTH];
	int ret = eitDmx->Read(buf, MAX_SECTION_LENGTH, 10);

	if (ret > 0) {
		LongSection section(buf);
		printdate_ms(stdout);
		xprintf("%s: eit update filter: ### new version 0x%02x ###, Activate thread\n",
				name.c_str(), section.getVersionNumber());

		writeLockMessaging();
		messaging_have_CN = 0x00;
		messaging_got_CN = 0x00;
		unlockMessaging();
		return true;
	}
	return false;
}

void CCNThread::sendCNEvent()
{
	eventServer->sendEvent(CSectionsdClient::EVT_GOT_CN_EPG,
			CEventServer::INITID_SECTIONSD,
			&messaging_current_servicekey,
			sizeof(messaging_current_servicekey));
}

#ifdef ENABLE_FREESATEPG
/********************************************************************************/
/* Freesat EIT thread 								*/
/********************************************************************************/
CFreeSatThread()
	: CEventsThread("freeSatThread", 3842)
{
	skipTime = TIME_FSEIT_SKIPPING;
};

/* Freesat hooks */
void CFreeSatThread::addFilters()
{
	//other TS, scheduled, freesat epg is only broadcast using table_ids 0x60 (scheduled) and 0x61 (scheduled later)
	addfilter(0x60, 0xfe); 
}
#endif

#ifdef ENABLE_SDT
static bool addService(const SIservice &s, const int is_actual)
{
	bool already_exists;
	bool is_new = false;

	readLockServices();
	MySIservicesOrderUniqueKey::iterator si = mySIservicesOrderUniqueKey.find(s.uniqueKey());
	already_exists = (si != mySIservicesOrderUniqueKey.end());
	unlockServices();

	if ( (!already_exists) || ((is_actual & 7) && (!si->second->is_actual)) ) {
		if (already_exists)
		{
			writeLockServices();
			mySIservicesOrderUniqueKey.erase(s.uniqueKey());
			unlockServices();
		}

		SIservice *sp = new SIservice(s);

		if (!sp)
		{
			printf("[sectionsd::addService] new SIservice failed.\n");
			return false;
		}

		SIservicePtr sptr(sp);

		sptr->is_actual = is_actual;

		writeLockServices();
		mySIservicesOrderUniqueKey.insert(std::make_pair(sptr->uniqueKey(), sptr));
		unlockServices();

		if (sptr->nvods.size())
		{
			writeLockServices();
			mySIservicesNVODorderUniqueKey.insert(std::make_pair(sptr->uniqueKey(), sptr));
			unlockServices();
		}
		is_new = true;
	}
	return is_new;
}

void CSdtThread::run()
{
	name = "sdtThread";
	xprintf("%s::run:: starting, pid %d (%lu)\n", name.c_str(), getpid(), pthread_self());
	pID = 0x11;
	dmx_num = 0;
	cache = false;

	timeoutInMSeconds = EIT_READ_TIMEOUT;

	bool sendToSleepNow = false;

	//addfilter(0x42, 0xf3 ); //SDT actual = 0x42 + SDT other = 0x46 + BAT = 0x4A
	addfilter(0x42, 0xfb ); //SDT actual = 0x42 + SDT other = 0x46

	waitForTimeset();
	DMX::start(); // -> unlock

	time_t lastData = time_monotonic();

	while (running) {
		if(sendToSleepNow || !scanning) {
#ifdef DEBUG_SDT_THREAD
			xprintf("%s: going to sleep %d seconds, running %d scanning %d services %d\n",
					name.c_str(), TIME_SDT_SCHEDULED_PAUSE, running, scanning, event_count);
#endif
			event_count = 0;

			int rs = 0;
			do {
				real_pause();
				rs = Sleep(TIME_SDT_SCHEDULED_PAUSE);
#ifdef DEBUG_SDT_THREAD
				xprintf("%s: wakeup, running %d scanning %d reason %d\n\n", name.c_str(), running, scanning, rs);
#endif
			} while(running && !scanning);

			if(!running)
				break;

			if (rs == ETIMEDOUT)
				change(0); // -> restart

			sendToSleepNow = false;
			lastData = time_monotonic();
		}

		int rc = getSection(static_buf, timeoutInMSeconds, timeoutsDMX);
		if(rc > 0) {
			if(addServices())
				lastData = time_monotonic();
		}

		time_t zeit = time_monotonic();

		if(timeoutsDMX < 0 || timeoutsDMX >= CHECK_RESTART_DMX_AFTER_TIMEOUTS) {
#ifdef DEBUG_SDT_THREAD
			xprintf("%s: timeouts %d\n", name.c_str(), timeoutsDMX);
#endif
			timeoutsDMX = 0;
			sendToSleepNow = true;
		}

		if (zeit > (lastData + TIME_SDT_NONEWDATA)) {
#ifdef DEBUG_SDT_THREAD
			xprintf("%s: no new services for %d seconds\n", name.c_str(), (int) (zeit - lastData));
#endif
			sendToSleepNow = true;
		}
	} // while running
	delete[] static_buf;
	printf("[sectionsd] %s ended\n", name.c_str());
	pthread_exit(NULL);
}

bool CSdtThread::addServices()
{
	bool is_new = false;

	LongSection sec(static_buf);
	uint8_t table_id = sec.getTableId();
	if ((table_id == 0x42) || (table_id == 0x46)) {
		SIsectionSDT sdt(static_buf);

		bool is_actual = (sdt.getTableId() == 0x42) ? 1 : 0;

		if (is_actual && !sdt.getLastSectionNumber())
			is_actual = 2;

		is_actual = (is_actual | 8);

		for (SIservices::iterator s = sdt.services().begin(); s != sdt.services().end(); ++s) {
			if (addService(*s, is_actual))
				is_new = true;
			event_count++;
		}
	}
	return is_new;
}
#endif

/* helper function for the housekeeping-thread */
static void print_meminfo(void)
{
	if (!sections_debug)
		return;

	struct mallinfo meminfo = mallinfo();
	dprintf("total size of memory occupied by chunks handed out by malloc: %d\n"
			"total bytes memory allocated with `sbrk' by malloc, in bytes: %d (%dkB)\n",
			meminfo.uordblks, meminfo.arena, meminfo.arena / 1024);
}

//---------------------------------------------------------------------
// housekeeping-thread
// does cleaning on fetched datas
//---------------------------------------------------------------------
static void *houseKeepingThread(void *)
{
	int count = 0;

	dprintf("housekeeping-thread started.\n");
	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, 0);

	while (!sectionsd_stop)
	{
		if (count == META_HOUSEKEEPING) {
			dprintf("meta housekeeping - deleting all transponders, services, bouquets.\n");
			deleteSIexceptEPG();
			count = 0;
		}

		int rc = HOUSEKEEPING_SLEEP;

		while (rc)
			rc = sleep(rc);

		while (!scanning) {
			sleep(1);	// wait for streaming to end...
			if(sectionsd_stop)
				break;
		}

		dprintf("housekeeping.\n");

		// TODO: maybe we need to stop scanning here?...

		readLockEvents();

		unsigned anzEventsAlt = mySIeventsOrderUniqueKey.size();
		dprintf("before removeoldevents\n");
		unlockEvents();

		removeOldEvents(oldEventsAre); // alte Events
		dprintf("after removeoldevents\n");
		readLockEvents();
		printf("[sectionsd] Removed %d old events (%d left).\n", anzEventsAlt - mySIeventsOrderUniqueKey.size(), mySIeventsOrderUniqueKey.size());
		if (mySIeventsOrderUniqueKey.size() != anzEventsAlt)
		{
			print_meminfo();
			dprintf("Removed %d old events.\n", anzEventsAlt - mySIeventsOrderUniqueKey.size());
		}
		anzEventsAlt = mySIeventsOrderUniqueKey.size();
		unlockEvents();

		readLockEvents();
		if (mySIeventsOrderUniqueKey.size() != anzEventsAlt)
		{
			print_meminfo();
			dprintf("Removed %d waste events.\n", anzEventsAlt - mySIeventsOrderUniqueKey.size());
		}

		dprintf("Number of sptr events (event-ID): %u\n", mySIeventsOrderUniqueKey.size());
		dprintf("Number of sptr events (service-id, start time, event-id): %u\n", mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.size());
		dprintf("Number of sptr events (end time, service-id, event-id): %u\n", mySIeventsOrderFirstEndTimeServiceIDEventUniqueKey.size());
		dprintf("Number of sptr nvod events (event-ID): %u\n", mySIeventsNVODorderUniqueKey.size());
		dprintf("Number of cached meta-services: %u\n", mySIeventUniqueKeysMetaOrderServiceUniqueKey.size());

		unlockEvents();

		print_meminfo();

		count++;

	} // for endlos
	dprintf("housekeeping-thread ended.\n");

	pthread_exit(NULL);
}

extern cDemux * dmxUTC;

void sectionsd_main_thread(void * /*data*/)
{
	pthread_t threadTOT, threadHouseKeeping;
	int rc;

	printf("$Id: sectionsd.cpp,v 1.305 2009/07/30 12:41:39 seife Exp $\n");
printf("SIevent size: %d\n", sizeof(SIevent));

	/* "export NO_SLOW_ADDEVENT=true" to disable this */
	slow_addevent = (getenv("NO_SLOW_ADDEVENT") == NULL);
	if (slow_addevent)
		printf("====> USING SLOW ADDEVENT. export 'NO_SLOW_ADDEVENT=1' to avoid <===\n");

	/* for debugging / benchmarking, "export STARTUP_WAIT=true" to wait with startup for
	 * the EPG loading to finish
	 * this wil fail badly if no EPG saving / loading is configured! */
	reader_ready = (getenv("STARTUP_WAIT") == NULL);
	if (!reader_ready)
		printf("====> sectionsd waiting with startup until saved EPG is read <===\n");

	SIlanguage::loadLanguages();

	tzset(); // TZ auswerten

	CBasicServer sectionsd_server;

	//NTP-Config laden
	if (!ntp_config.loadConfig(CONF_FILE))
	{
		/* set defaults if no configuration file exists */
		printf("[sectionsd] %s not found\n", CONF_FILE);
	}

	ntpserver = ntp_config.getString("network_ntpserver", "de.pool.ntp.org");
	ntprefresh = atoi(ntp_config.getString("network_ntprefresh","30").c_str() );
	ntpenable = ntp_config.getBool("network_ntpenable", false);
	ntp_system_cmd = ntp_system_cmd_prefix + ntpserver;

	//EPG Einstellungen laden
	secondsToCache = (atoi(ntp_config.getString("epg_cache_time","14").c_str() ) *24*60L*60L); //Tage
	secondsExtendedTextCache = (atoi(ntp_config.getString("epg_extendedcache_time","360").c_str() ) *60L*60L); //Stunden
	oldEventsAre = (atoi(ntp_config.getString("epg_old_events","1").c_str() ) *60L*60L); //Stunden
	max_events= atoi(ntp_config.getString("epg_max_events","50000").c_str() );

	printf("[sectionsd] Caching max %d events\n", max_events);
	printf("[sectionsd] Caching %ld days\n", secondsToCache / (24*60*60L));
	printf("[sectionsd] Caching %ld hours Extended Text\n", secondsExtendedTextCache / (60*60L));
	printf("[sectionsd] Events are old %ldmin after their end time\n", oldEventsAre / 60);

	readEPGFilter();
	readDVBTimeFilter();
	readEncodingFile();

	if (!sectionsd_server.prepare(SECTIONSD_UDS_NAME)) {
		fprintf(stderr, "[sectionsd] failed to prepare basic server\n");
		return;
	}

	eventServer = new CEventServer;

	// time-Thread starten
	rc = pthread_create(&threadTOT, 0, timeThread, 0);

	if (rc) {
		fprintf(stderr, "[sectionsd] failed to create time-thread (rc=%d)\n", rc);
		return;
	}

	threadEIT.Start();

	threadCN.Start();

#ifdef ENABLE_FREESATEPG
	threadFSEIT.Start();
#endif
#ifdef ENABLE_SDT
	threadSDT.Start();
#endif

	// housekeeping-Thread starten
	rc = pthread_create(&threadHouseKeeping, 0, houseKeepingThread, 0);

	if (rc) {
		fprintf(stderr, "[sectionsd] failed to create housekeeping-thread (rc=%d)\n", rc);
		return;
	}

	if (sections_debug)
		dump_sched_info("main");

	sectionsd_ready = true;

	while (!sectionsd_stop && sectionsd_server.run(sectionsd_parse_command, sectionsd::ACTVERSION, true)) {
		sched_yield();
		if(threadCN.checkUpdate()) {
			sched_yield();
			threadCN.change(0);
			sched_yield();
		}

		sched_yield();
		/* 10 ms is the minimal timeslice anyway (HZ = 100), so let's
		   wait 20 ms at least to lower the CPU load */
		usleep(20000);
	}

	printf("[sectionsd] stopping...\n");
	//scanning = 0;

	timeset = true;
	printf("broadcasting...\n");
	pthread_mutex_lock(&timeIsSetMutex);
	pthread_cond_broadcast(&timeIsSetCond);
	pthread_mutex_unlock(&timeIsSetMutex);
	pthread_mutex_lock(&timeThreadSleepMutex);
	pthread_cond_broadcast(&timeThreadSleepCond);
	pthread_mutex_unlock(&timeThreadSleepMutex);

	printf("pausing...\n");

	pthread_cancel(threadHouseKeeping);

	if(dmxUTC) dmxUTC->Stop();

	//pthread_cancel(threadTOT);
	printf("join TOT\n");
	pthread_join(threadTOT, NULL);
	if(dmxUTC) delete dmxUTC;

	printf("join EIT\n");
	threadEIT.Stop();

	printf("join CN\n");
	threadCN.Stop();

#ifdef ENABLE_SDT
	printf("join SDT\n");
	threadSDT.Stop();
#endif
#ifdef ENABLE_FREESATEPG
	printf("join FSEIT\n");
	threadFSEIT.Stop();
#endif
	printf("[sectionsd] ended\n");
}

/* was: commandAllEventsChannelID sendAllEvents */
void sectionsd_getEventsServiceKey(t_channel_id serviceUniqueKey, CChannelEventList &eList, char search = 0, std::string search_text = "")
{
	dprintf("sendAllEvents for " PRINTF_CHANNEL_ID_TYPE "\n", serviceUniqueKey);

	if ((serviceUniqueKey& 0xFFFFFFFFFFFFULL) != 0) { //0xFFFFFFFFFFFFULL for CREATE_CHANNEL_ID64
		// service Found
		readLockEvents();
		int serviceIDfound = 0;

		if (search_text.length())
			std::transform(search_text.begin(), search_text.end(), search_text.begin(), tolower);
		for (MySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey::iterator e = mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.begin(); e != mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.end(); ++e)
		{
			if ((*e)->get_channel_id() == (serviceUniqueKey& 0xFFFFFFFFFFFFULL)) { //0xFFFFFFFFFFFFULL for CREATE_CHANNEL_ID64
				serviceIDfound = 1;

				bool copy = true;
				if(search == 0); // nothing to do here
				else if(search == 1) {
					std::string eName = (*e)->getName();
					std::transform(eName.begin(), eName.end(), eName.begin(), tolower);
					if(eName.find(search_text) == std::string::npos)
						copy = false;
				}
				else if(search == 2) {
					std::string eText = (*e)->getText();
					std::transform(eText.begin(), eText.end(), eText.begin(), tolower);
					if(eText.find(search_text) == std::string::npos)
						copy = false;
				}
				else if(search == 3) {
					std::string eExtendedText = (*e)->getExtendedText();
					std::transform(eExtendedText.begin(), eExtendedText.end(), eExtendedText.begin(), tolower);
					if(eExtendedText.find(search_text) == std::string::npos)
						copy = false;
				}

				if(copy) {
					for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t)
					{
						CChannelEvent aEvent;
						aEvent.eventID = (*e)->uniqueKey();
						aEvent.startTime = t->startzeit;
						aEvent.duration = t->dauer;
						aEvent.description = (*e)->getName();
						if (((*e)->getText()).empty())
							aEvent.text = (*e)->getExtendedText().substr(0, 120);
						else
							aEvent.text = (*e)->getText();
						aEvent.channelID = serviceUniqueKey;
						eList.push_back(aEvent);
					}
				} // if = serviceID
			}
			else if ( serviceIDfound )
				break; // sind nach serviceID und startzeit sortiert -> nicht weiter suchen
		}

		unlockEvents();
	}
}

/* send back the current and next event for the channel id passed to it
 * Works like that:
 * - if the currently running program is requested, return myCurrentEvent and myNextEvent,
 *   if they are present (filled in by cnThread)
 * - if one or both of those are not present, or if a different program than the currently
 *   running is requested, search the missing events in the list of events gathered by the
 *   EIT and PPT threads, based on the current time.
 *
 * TODO: the handling of "flag" should be vastly simplified.
 */
/* was: commandCurrentNextInfoChannelID */
void sectionsd_getCurrentNextServiceKey(t_channel_id uniqueServiceKey, CSectionsdClient::responseGetCurrentNextInfoChannelID& current_next )
{
	dprintf("[sectionsd] Request of current/next information for " PRINTF_CHANNEL_ID_TYPE "\n", uniqueServiceKey);

	SIevent currentEvt;
	SIevent nextEvt;
	unsigned flag = 0, flag2=0;
	/* ugly hack: retry fetching current/next by restarting dmxCN if this is true */
	bool change = false;//TODO remove ?

	//t_channel_id * uniqueServiceKey = (t_channel_id *)data;

	readLockEvents();
	/* if the currently running program is requested... */
	if (uniqueServiceKey == messaging_current_servicekey) {
		/* ...check for myCurrentEvent and myNextEvent */
		if (!myCurrentEvent) {
			dprintf("!myCurrentEvent ");
			change = true;
			flag |= CSectionsdClient::epgflags::not_broadcast;
		} else {
			currentEvt = *myCurrentEvent;
			flag |= CSectionsdClient::epgflags::has_current; // aktuelles event da...
			flag |= CSectionsdClient::epgflags::has_anything;
		}
		if (!myNextEvent) {
			dprintf("!myNextEvent ");
			change = true;
		} else {
			nextEvt = *myNextEvent;
			if (flag & CSectionsdClient::epgflags::not_broadcast) {
				dprintf("CSectionsdClient::epgflags::has_no_current\n");
				flag = CSectionsdClient::epgflags::has_no_current;
			}
			flag |= CSectionsdClient::epgflags::has_next; // aktuelles event da...
			flag |= CSectionsdClient::epgflags::has_anything;
		}
	}

	//dprintf("flag: 0x%x, has_current: 0x%x has_next: 0x%x\n", flag, CSectionsdClient::epgflags::has_current, CSectionsdClient::epgflags::has_next);
	/* if another than the currently running program is requested, then flag will still be 0
	   if either the current or the next event is not found, this condition will be true, too.
	   */
	if ((flag & (CSectionsdClient::epgflags::has_current|CSectionsdClient::epgflags::has_next)) !=
			(CSectionsdClient::epgflags::has_current|CSectionsdClient::epgflags::has_next)) {
		//dprintf("commandCurrentNextInfoChannelID: current or next missing!\n");
		SItime zeitEvt1(0, 0);
		if (!(flag & CSectionsdClient::epgflags::has_current)) {
			currentEvt = findActualSIeventForServiceUniqueKey(uniqueServiceKey, zeitEvt1, 0, &flag2);
		} else {
			zeitEvt1.startzeit = currentEvt.times.begin()->startzeit;
			zeitEvt1.dauer = currentEvt.times.begin()->dauer;
		}
		SItime zeitEvt2(zeitEvt1);

		if (currentEvt.getName().empty() && flag2 != 0)
		{
			dprintf("commandCurrentNextInfoChannelID change1\n");
			change = true;
		}

		if (currentEvt.service_id != 0)
		{	//Found
			flag &= (CSectionsdClient::epgflags::has_no_current|CSectionsdClient::epgflags::not_broadcast)^(unsigned)-1;
			flag |= CSectionsdClient::epgflags::has_current;
			flag |= CSectionsdClient::epgflags::has_anything;
			dprintf("[sectionsd] current EPG found. service_id: %x, flag: 0x%x\n",currentEvt.service_id, flag);

			if (!(flag & CSectionsdClient::epgflags::has_next)) {
				dprintf("*nextEvt not from cur/next V1!\n");
				nextEvt = findNextSIevent(currentEvt.uniqueKey(), zeitEvt2);
			}
		}
		else
		{	// no current event...
			if ( flag2 & CSectionsdClient::epgflags::has_anything )
			{
				flag |= CSectionsdClient::epgflags::has_anything;
				if (!(flag & CSectionsdClient::epgflags::has_next)) {
					dprintf("*nextEvt not from cur/next V2!\n");
					nextEvt = findNextSIeventForServiceUniqueKey(uniqueServiceKey, zeitEvt2);
				}

				if (nextEvt.service_id != 0)
				{
					MySIeventsOrderUniqueKey::iterator eFirst = mySIeventsOrderUniqueKey.find(uniqueServiceKey);

					if (eFirst != mySIeventsOrderUniqueKey.end())
					{
						// this is a race condition if first entry found is == mySIeventsOrderUniqueKey.begin()
						// so perform a check
						if (eFirst != mySIeventsOrderUniqueKey.begin())
							--eFirst;

						if (eFirst != mySIeventsOrderUniqueKey.begin())
						{
							time_t azeit = time(NULL);

							if (eFirst->second->times.begin()->startzeit < azeit &&
									eFirst->second->uniqueKey() == nextEvt.uniqueKey() - 1)
								flag |= CSectionsdClient::epgflags::has_no_current;
						}
					}
				}
			}
		}
		if (nextEvt.service_id != 0)
		{
			flag &= CSectionsdClient::epgflags::not_broadcast^(unsigned)-1;
			dprintf("[sectionsd] next EPG found. service_id: %x, flag: 0x%x\n",nextEvt.service_id, flag);
			flag |= CSectionsdClient::epgflags::has_next;
		}
		else if (flag != 0)
		{
			dprintf("commandCurrentNextInfoChannelID change2 flag: 0x%02x\n", flag);
			change = true;
		}
	}

	if (currentEvt.service_id != 0)
	{
		/* check for nvod linkage */
		for (unsigned int i = 0; i < currentEvt.linkage_descs.size(); i++)
			if (currentEvt.linkage_descs[i].linkageType == 0xB0)
			{
				fprintf(stderr,"[sectionsd] linkage in current EPG found.\n");
				flag |= CSectionsdClient::epgflags::current_has_linkagedescriptors;
				break;
			}
	} else
		flag |= CSectionsdClient::epgflags::has_no_current;

	time_t now;

	dprintf("currentEvt: '%s' (%04x) nextEvt: '%s' (%04x) flag: 0x%02x\n",
			currentEvt.getName().c_str(), currentEvt.eventID,
			nextEvt.getName().c_str(), nextEvt.eventID, flag);

	CSectionsdClient::sectionsdTime time_cur;
	CSectionsdClient::sectionsdTime time_nxt;
	now = time(NULL);
	time_cur.startzeit = currentEvt.times.begin()->startzeit;
	time_cur.dauer = currentEvt.times.begin()->dauer;
	time_nxt.startzeit = nextEvt.times.begin()->startzeit;
	time_nxt.dauer = nextEvt.times.begin()->dauer;
	/* for nvod events that have multiple times, find the one that matches the current time... */
	if (currentEvt.times.size() > 1) {
		for (SItimes::iterator t = currentEvt.times.begin(); t != currentEvt.times.end(); ++t) {
			if ((long)now < (long)(t->startzeit + t->dauer) && (long)now > (long)t->startzeit) {
				time_cur.startzeit = t->startzeit;
				time_cur.dauer =t->dauer;
				break;
			}
		}
	}
	/* ...and the one after that. */
	if (nextEvt.times.size() > 1) {
		for (SItimes::iterator t = nextEvt.times.begin(); t != nextEvt.times.end(); ++t) {
			if ((long)(time_cur.startzeit + time_cur.dauer) <= (long)(t->startzeit)) { // TODO: it's not "long", it's "time_t"
				time_nxt.startzeit = t->startzeit;
				time_nxt.dauer =t->dauer;
				break;
			}
		}
	}

	current_next.current_uniqueKey = currentEvt.uniqueKey();
	current_next.current_zeit.startzeit = time_cur.startzeit;
	current_next.current_zeit.dauer = time_cur.dauer;
	current_next.current_name = currentEvt.getName();

	current_next.next_uniqueKey = nextEvt.uniqueKey();
	current_next.next_zeit.startzeit = time_nxt.startzeit;
	current_next.next_zeit.dauer = time_nxt.dauer;
	current_next.next_name = nextEvt.getName();

	current_next.flags = flag;
	current_next.current_fsk = currentEvt.getFSK();

	unlockEvents();
}

/* commandEPGepgIDshort */
bool sectionsd_getEPGidShort(event_id_t epgID, CShortEPGData * epgdata)
{
	bool ret = false;
	dprintf("Request of current EPG for 0x%llx\n", epgID);

	readLockEvents();

	const SIevent& e = findSIeventForEventUniqueKey(epgID);

	if (e.service_id != 0)
	{	// Event found
		dputs("EPG found.");
		epgdata->title = e.getName();
		epgdata->info1 = e.getText();
		epgdata->info2 = e.getExtendedText();
		ret = true;
	} else
		dputs("EPG not found!");

	unlockEvents();
	return ret;
}

/*was getEPGid commandEPGepgID(int connfd, char *data, const unsigned dataLength) */
/* TODO item / itemDescription */
bool sectionsd_getEPGid(const event_id_t epgID, const time_t startzeit, CEPGData * epgdata)
{
	bool ret = false;
	dprintf("Request of actual EPG for 0x%llx 0x%lx\n", epgID, startzeit);

	const SIevent& evt = findSIeventForEventUniqueKey(epgID);

	epgdata->itemDescriptions.clear();
	epgdata->items.clear();

	readLockEvents();
	if (evt.service_id != 0) { // Event found
		SItimes::iterator t = evt.times.begin();

		for (; t != evt.times.end(); ++t)
			if (t->startzeit == startzeit)
				break;

		if (t == evt.times.end()) {
			dputs("EPG not found!");
		} else {
			dputs("EPG found.");
			epgdata->eventID = evt.uniqueKey();
			epgdata->title = evt.getName();
			epgdata->info1 = evt.getText();
			epgdata->info2 = evt.getExtendedText();
			/* FIXME printf("itemDescription: %s\n", evt.itemDescription.c_str()); */
			epgdata->contentClassification = std::string(evt.contentClassification.data(), evt.contentClassification.length());
			epgdata->userClassification = std::string(evt.userClassification.data(), evt.userClassification.length());
			epgdata->fsk = evt.getFSK();
			epgdata->table_id = evt.table_id;

			epgdata->epg_times.startzeit = t->startzeit;
			epgdata->epg_times.dauer = t->dauer;

			ret = true;
		}
	} else {
		dputs("EPG not found!");
	}
	unlockEvents();
	return ret;
}
/* was  commandActualEPGchannelID(int connfd, char *data, const unsigned dataLength) */
bool sectionsd_getActualEPGServiceKey(const t_channel_id uniqueServiceKey, CEPGData * epgdata)
{
	bool ret = false;
	SIevent evt;
	SItime zeit(0, 0);

	dprintf("[commandActualEPGchannelID] Request of current EPG for " PRINTF_CHANNEL_ID_TYPE "\n", uniqueServiceKey);

	readLockEvents();
	if (uniqueServiceKey == messaging_current_servicekey) {
		if (myCurrentEvent) {
			evt = *myCurrentEvent;
			zeit.startzeit = evt.times.begin()->startzeit;
			zeit.dauer = evt.times.begin()->dauer;
			if (evt.times.size() > 1) {
				time_t now = time(NULL);
				for (SItimes::iterator t = evt.times.begin(); t != evt.times.end(); ++t) {
					if ((long)now < (long)(t->startzeit + t->dauer) && (long)now > (long)t->startzeit) {
						zeit.startzeit = t->startzeit;
						zeit.dauer = t->dauer;
						break;
					}
				}
			}
		}
	}

	if (evt.service_id == 0)
	{
		dprintf("[commandActualEPGchannelID] evt.service_id == 0 ==> no myCurrentEvent!\n");
		evt = findActualSIeventForServiceUniqueKey(uniqueServiceKey, zeit);
	}

	if (evt.service_id != 0)
	{
		dprintf("EPG found.\n");
		epgdata->eventID = evt.uniqueKey();
		epgdata->title = evt.getName();
		epgdata->info1 = evt.getText();
		epgdata->info2 = evt.getExtendedText();
		/* FIXME printf("itemDescription: %s\n", evt.itemDescription.c_str());*/
		epgdata->contentClassification = std::string(evt.contentClassification.data(), evt.contentClassification.length());
		epgdata->userClassification = std::string(evt.userClassification.data(), evt.userClassification.length());
		epgdata->fsk = evt.getFSK();
		epgdata->table_id = evt.table_id;

		epgdata->epg_times.startzeit = zeit.startzeit;
		epgdata->epg_times.dauer = zeit.dauer;

		ret = true;
	} else
		dprintf("EPG not found!\n");

	unlockEvents();
	return ret;
}

bool channel_in_requested_list(t_channel_id * clist, t_channel_id chid, int len)
{
	if(len == 0) return true;
	for(int i = 0; i < len; i++) {
		if(clist[i] == chid)
			return true;
	}
	return false;
}

/* was static void sendEventList(int connfd, const unsigned char serviceTyp1, const unsigned char serviceTyp2 = 0, int sendServiceName = 1, t_channel_id * chidlist = NULL, int clen = 0) */
void sectionsd_getChannelEvents(CChannelEventList &eList, const bool tv_mode = true, t_channel_id *chidlist = NULL, int clen = 0)
{
	clen = clen / sizeof(t_channel_id);

	t_channel_id uniqueNow = 0;
	t_channel_id uniqueOld = 0;
	bool found_already = true;
	time_t azeit = time(NULL);

	if(tv_mode) {}
showProfiling("sectionsd_getChannelEvents start");
	readLockEvents();

	/* !!! FIX ME: if the box starts on a channel where there is no EPG sent, it hangs!!!	*/
	for (MySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey::iterator e = mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.begin(); e != mySIeventsOrderServiceUniqueKeyFirstStartTimeEventUniqueKey.end(); ++e)
	{
		uniqueNow = (*e)->get_channel_id();

		if (uniqueNow != uniqueOld)
		{
			uniqueOld = uniqueNow;
			if (!channel_in_requested_list(chidlist, uniqueNow, clen))
				continue;

			found_already = false;
		}

		if (!found_already)
		{
			for (SItimes::iterator t = (*e)->times.begin(); t != (*e)->times.end(); ++t)
			{
				if (t->startzeit <= azeit && azeit <= (long)(t->startzeit + t->dauer))
				{
					//TODO CChannelEvent constructor from SIevent ?
					CChannelEvent aEvent;
					aEvent.eventID = (*e)->uniqueKey();
					aEvent.startTime = t->startzeit;
					aEvent.duration = t->dauer;
					aEvent.description = (*e)->getName();
					if (((*e)->getText()).empty())
						aEvent.text = (*e)->getExtendedText().substr(0, 120);
					else
						aEvent.text = (*e)->getText();
					eList.push_back(aEvent);

					found_already = true;
					break;
				}
			}
			if(found_already && clen && (clen == (int) eList.size()))
				break;
		}
	}

showProfiling("sectionsd_getChannelEvents end");
	unlockEvents();
}

/*was static void commandComponentTagsUniqueKey(int connfd, char *data, const unsigned dataLength) */
bool sectionsd_getComponentTagsUniqueKey(const event_id_t uniqueKey, CSectionsdClient::ComponentTagList& tags)
{
	bool ret = false;
	dprintf("Request of ComponentTags for 0x%llx\n", uniqueKey);

	tags.clear();

	readLockEvents();

	MySIeventsOrderUniqueKey::iterator eFirst = mySIeventsOrderUniqueKey.find(uniqueKey);

	if (eFirst != mySIeventsOrderUniqueKey.end()) {
		CSectionsdClient::responseGetComponentTags response;
		ret = true;

		for (SIcomponents::iterator cmp = eFirst->second->components.begin(); cmp != eFirst->second->components.end(); ++cmp) {
			response.component = cmp->component;
			response.componentType = cmp->componentType;
			response.componentTag = cmp->componentTag;
			response.streamContent = cmp->streamContent;

			tags.insert(tags.end(), response);
		}
	}

	unlockEvents();
	return ret;
}

/* was static void commandLinkageDescriptorsUniqueKey(int connfd, char *data, const unsigned dataLength) */
bool sectionsd_getLinkageDescriptorsUniqueKey(const event_id_t uniqueKey, CSectionsdClient::LinkageDescriptorList& descriptors)
{
	bool ret = false;
	dprintf("Request of LinkageDescriptors for 0x%llx\n", uniqueKey);

	descriptors.clear();
	readLockEvents();

	MySIeventsOrderUniqueKey::iterator eFirst = mySIeventsOrderUniqueKey.find(uniqueKey);

	if (eFirst != mySIeventsOrderUniqueKey.end()) {
		for (SIlinkage_descs::iterator linkage_desc = eFirst->second->linkage_descs.begin(); linkage_desc != eFirst->second->linkage_descs.end(); ++linkage_desc)
		{
			if (linkage_desc->linkageType == 0xB0) {

				CSectionsdClient::responseGetLinkageDescriptors response;
				response.name = linkage_desc->name.c_str();
				response.transportStreamId = linkage_desc->transportStreamId;
				response.originalNetworkId = linkage_desc->originalNetworkId;
				response.serviceId = linkage_desc->serviceId;
				descriptors.insert( descriptors.end(), response);
				ret = true;
			}
		}
	}

	unlockEvents();
	return ret;
}

/* was static void commandTimesNVODservice(int connfd, char *data, const unsigned dataLength) */
bool sectionsd_getNVODTimesServiceKey(const t_channel_id uniqueServiceKey, CSectionsdClient::NVODTimesList& nvod_list)
{
	bool ret = false;
	dprintf("Request of NVOD times for " PRINTF_CHANNEL_ID_TYPE "\n", uniqueServiceKey);

	nvod_list.clear();

	readLockServices();
	readLockEvents();

	MySIservicesNVODorderUniqueKey::iterator si = mySIservicesNVODorderUniqueKey.find(uniqueServiceKey);
	if (si != mySIservicesNVODorderUniqueKey.end())
	{
		dprintf("NVODServices: %u\n", si->second->nvods.size());

		if (si->second->nvods.size()) {
			for (SInvodReferences::iterator ni = si->second->nvods.begin(); ni != si->second->nvods.end(); ++ni) {
				SItime zeitEvt1(0, 0);
				findActualSIeventForServiceUniqueKey(ni->uniqueKey(), zeitEvt1, 15*60);

				CSectionsdClient::responseGetNVODTimes response;

				response.service_id =  ni->service_id;
				response.original_network_id = ni->original_network_id;
				response.transport_stream_id = ni->transport_stream_id;
				response.zeit.startzeit = zeitEvt1.startzeit;
				response.zeit.dauer = zeitEvt1.dauer;

				nvod_list.insert( nvod_list.end(), response);
				ret = true;
			}
		}
	}

	unlockEvents();
	unlockServices();
	return ret;
}

void sectionsd_setPrivatePid(unsigned short /*pid*/)
{
}

void sectionsd_set_languages(const std::vector<std::string>& newLanguages)
{
	SIlanguage::setLanguages(newLanguages);
	SIlanguage::saveLanguages();
}

bool sectionsd_isReady(void)
{
	return sectionsd_ready;
}
