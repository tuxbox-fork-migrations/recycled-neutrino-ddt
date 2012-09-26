/*
	$port: keybind_setup.cpp,v 1.4 2010/09/20 10:24:12 tuxbox-cvs Exp $

	keybindings setup implementation - Neutrino-GUI

	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2010 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/


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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_COOLSTREAM_CS_IR_GENERIC_H
/* define constants instead of #ifdef'ing the corresponding code.
 * the compiler will optimize it away anyway, but the syntax is
 * still checked */
#define RC_HW_SELECT true
#else
#define RC_HW_SELECT false
#ifdef HAVE_COOL_HARDWARE
#warning header coolstream/cs_ir_generic.h not found
#warning you probably have an old driver installation
#warning you´ll be missing the remotecontrol selection feature!
#endif
#endif

#include "keybind_setup.h"

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>
#include <neutrino_menue.h>

#include <gui/widget/icons.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/stringinput.h>

#include <gui/filebrowser.h>

#include <driver/screen_max.h>
#include <driver/screenshot.h>

#include <system/debug.h>


CKeybindSetup::CKeybindSetup()
{
	changeNotify(LOCALE_KEYBINDINGMENU_REPEATBLOCKGENERIC, NULL);

	width = w_max (40, 10);
}

CKeybindSetup::~CKeybindSetup()
{
}

int CKeybindSetup::exec(CMenuTarget* parent, const std::string &actionKey)
{
	dprintf(DEBUG_DEBUG, "init keybindings setup\n");
	int   res = menu_return::RETURN_REPAINT;

	if (parent)
	{
		parent->hide();
	}

	if(actionKey == "loadkeys") {
		CFileBrowser fileBrowser;
		CFileFilter fileFilter;
		fileFilter.addFilter("conf");
		fileBrowser.Filter = &fileFilter;
		if (fileBrowser.exec("/var/tuxbox/config") == true) {
			CNeutrinoApp::getInstance()->loadKeys(fileBrowser.getSelectedFile()->Name.c_str());
			printf("[neutrino keybind_setup] new keys: %s\n", fileBrowser.getSelectedFile()->Name.c_str());
		}
		return menu_return::RETURN_REPAINT;
	}
	else if(actionKey == "savekeys") {
		CFileBrowser fileBrowser;
		fileBrowser.Dir_Mode = true;
		if (fileBrowser.exec("/var/tuxbox") == true) {
			char  fname[256] = "keys.conf", sname[256];
			CStringInputSMS * sms = new CStringInputSMS(LOCALE_EXTRA_SAVEKEYS, fname, 30, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, "abcdefghijklmnopqrstuvwxyz0123456789. ");
			sms->exec(NULL, "");
			sprintf(sname, "%s/%s", fileBrowser.getSelectedFile()->Name.c_str(), fname);
			printf("[neutrino keybind_setup] save keys: %s\n", sname);
			CNeutrinoApp::getInstance()->saveKeys(sname);
			delete sms;
		}
		return menu_return::RETURN_REPAINT;
	}

	res = showKeySetup();

	return res;
}

#define KEYBINDINGMENU_REMOTECONTROL_HARDWARE_OPTION_COUNT 4
const CMenuOptionChooser::keyval KEYBINDINGMENU_REMOTECONTROL_HARDWARE_OPTIONS[KEYBINDINGMENU_REMOTECONTROL_HARDWARE_OPTION_COUNT] =
{
	{ CRCInput::RC_HW_COOLSTREAM,   LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_COOLSTREAM   },
	{ CRCInput::RC_HW_DBOX,         LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_DBOX         },
	{ CRCInput::RC_HW_PHILIPS,      LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_PHILIPS      },
	{ CRCInput::RC_HW_TRIPLEDRAGON, LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_TRIPLEDRAGON }
};

#define KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_COUNT 4
const CMenuOptionChooser::keyval KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_OPTIONS[KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_COUNT] =
{
	{ SNeutrinoSettings::ZAP,     LOCALE_KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_ZAP     },
	{ SNeutrinoSettings::VZAP,    LOCALE_KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_VZAP    },
	{ SNeutrinoSettings::VOLUME,  LOCALE_KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_VOLUME  },
	{ SNeutrinoSettings::INFOBAR, LOCALE_KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_INFOBAR }
};

#define KEYBINDINGMENU_BOUQUETHANDLING_OPTION_COUNT 3
const CMenuOptionChooser::keyval KEYBINDINGMENU_BOUQUETHANDLING_OPTIONS[KEYBINDINGMENU_BOUQUETHANDLING_OPTION_COUNT] =
{
	{ 0, LOCALE_KEYBINDINGMENU_BOUQUETCHANNELS_ON_OK },
	{ 1, LOCALE_KEYBINDINGMENU_BOUQUETLIST_ON_OK     },
	{ 2, LOCALE_KEYBINDINGMENU_ALLCHANNELS_ON_OK     }
};

typedef struct key_settings_t
{
	const neutrino_locale_t keydescription;
	int * keyvalue_p;
	const neutrino_locale_t hint;

} key_settings_struct_t;

const key_settings_struct_t key_settings[CKeybindSetup::KEYBINDS_COUNT] =
{
	{LOCALE_KEYBINDINGMENU_TVRADIOMODE,   	&g_settings.key_tvradio_mode,		LOCALE_MENU_HINT_KEY_TVRADIOMODE },
	{LOCALE_KEYBINDINGMENU_POWEROFF,      	&g_settings.key_power_off,		LOCALE_MENU_HINT_KEY_POWEROFF },
	{LOCALE_KEYBINDINGMENU_PAGEUP, 		&g_settings.key_channelList_pageup,	LOCALE_MENU_HINT_KEY_PAGEUP },
	{LOCALE_KEYBINDINGMENU_PAGEDOWN, 	&g_settings.key_channelList_pagedown, 	LOCALE_MENU_HINT_KEY_PAGEDOWN },
	{LOCALE_EXTRA_KEY_LIST_START, 		&g_settings.key_list_start, 		LOCALE_MENU_HINT_KEY_LIST_START },
	{LOCALE_EXTRA_KEY_LIST_END,	 	&g_settings.key_list_end,		LOCALE_MENU_HINT_KEY_LIST_END },
	{LOCALE_KEYBINDINGMENU_CANCEL,		&g_settings.key_channelList_cancel,	LOCALE_MENU_HINT_KEY_CANCEL },
	{LOCALE_KEYBINDINGMENU_SORT,		&g_settings.key_channelList_sort,	LOCALE_MENU_HINT_KEY_SORT },
	{LOCALE_KEYBINDINGMENU_ADDRECORD,	&g_settings.key_channelList_addrecord,	LOCALE_MENU_HINT_KEY_ADDRECORD },
	{LOCALE_KEYBINDINGMENU_ADDREMIND,	&g_settings.key_channelList_addremind,	LOCALE_MENU_HINT_KEY_ADDREMIND },
	{LOCALE_KEYBINDINGMENU_BOUQUETUP,	&g_settings.key_bouquet_up, 		LOCALE_MENU_HINT_KEY_BOUQUETUP },
	{LOCALE_KEYBINDINGMENU_BOUQUETDOWN,	&g_settings.key_bouquet_down, 		LOCALE_MENU_HINT_KEY_BOUQUETDOWN },
	{LOCALE_EXTRA_KEY_CURRENT_TRANSPONDER,	&g_settings.key_current_transponder,	LOCALE_MENU_HINT_KEY_TRANSPONDER },
	{LOCALE_KEYBINDINGMENU_CHANNELUP,	&g_settings.key_quickzap_up,		LOCALE_MENU_HINT_KEY_CHANNELUP },
	{LOCALE_KEYBINDINGMENU_CHANNELDOWN,	&g_settings.key_quickzap_down,  	LOCALE_MENU_HINT_KEY_CHANNELDOWN },
	{LOCALE_KEYBINDINGMENU_SUBCHANNELUP,	&g_settings.key_subchannel_up,  	LOCALE_MENU_HINT_KEY_SUBCHANNELUP },
	{LOCALE_KEYBINDINGMENU_SUBCHANNELDOWN,	&g_settings.key_subchannel_down,	LOCALE_MENU_HINT_KEY_SUBCHANNELDOWN },
	{LOCALE_KEYBINDINGMENU_ZAPHISTORY,	&g_settings.key_zaphistory, 		LOCALE_MENU_HINT_KEY_HISTORY },
	{LOCALE_KEYBINDINGMENU_LASTCHANNEL,	&g_settings.key_lastchannel,		LOCALE_MENU_HINT_KEY_LASTCHANNEL },
	{LOCALE_MPKEY_REWIND,			&g_settings.mpkey_rewind,		LOCALE_MENU_HINT_KEY_MPREWIND },
	{LOCALE_MPKEY_FORWARD,			&g_settings.mpkey_forward,  		LOCALE_MENU_HINT_KEY_MPFORWARD },
	{LOCALE_MPKEY_PAUSE,			&g_settings.mpkey_pause, 		LOCALE_MENU_HINT_KEY_MPPAUSE },
	{LOCALE_MPKEY_STOP,			&g_settings.mpkey_stop,			LOCALE_MENU_HINT_KEY_MPSTOP },
	{LOCALE_MPKEY_PLAY,			&g_settings.mpkey_play,			LOCALE_MENU_HINT_KEY_MPPLAY },
	{LOCALE_MPKEY_AUDIO,			&g_settings.mpkey_audio, 		LOCALE_MENU_HINT_KEY_MPAUDIO },
	{LOCALE_MPKEY_TIME,			&g_settings.mpkey_time,			LOCALE_MENU_HINT_KEY_MPTIME },
	{LOCALE_MPKEY_BOOKMARK,			&g_settings.mpkey_bookmark, 		LOCALE_MENU_HINT_KEY_MPBOOKMARK },
	{LOCALE_EXTRA_KEY_TIMESHIFT,		&g_settings.key_timeshift,  		LOCALE_MENU_HINT_KEY_MPTIMESHIFT },
	{LOCALE_MPKEY_PLUGIN,			&g_settings.mpkey_plugin,		LOCALE_MENU_HINT_KEY_MPPLUGIN },
	/*{LOCALE_EXTRA_KEY_PLUGIN,		&g_settings.key_plugin,			},*/
	{LOCALE_EXTRA_KEY_UNLOCK,		&g_settings.key_unlock,			LOCALE_MENU_HINT_KEY_UNLOCK},
	{LOCALE_EXTRA_KEY_SCREENSHOT,		&g_settings.key_screenshot,		LOCALE_MENU_HINT_KEY_SCREENSHOT }
};


int CKeybindSetup::showKeySetup()
{
	//save original rc hardware selection and initialize text strings
	int org_remote_control_hardware = g_settings.remote_control_hardware;
	char RC_HW_str[4][32];
	snprintf(RC_HW_str[CRCInput::RC_HW_COOLSTREAM],   sizeof(RC_HW_str[CRCInput::RC_HW_COOLSTREAM])-1,   "%s", g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_COOLSTREAM));
	snprintf(RC_HW_str[CRCInput::RC_HW_DBOX],         sizeof(RC_HW_str[CRCInput::RC_HW_DBOX])-1,         "%s", g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_DBOX));
	snprintf(RC_HW_str[CRCInput::RC_HW_PHILIPS],      sizeof(RC_HW_str[CRCInput::RC_HW_PHILIPS])-1,      "%s", g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_PHILIPS));
	snprintf(RC_HW_str[CRCInput::RC_HW_TRIPLEDRAGON], sizeof(RC_HW_str[CRCInput::RC_HW_TRIPLEDRAGON])-1, "%s", g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_TRIPLEDRAGON));
	char RC_HW_msg[256];
	snprintf(RC_HW_msg, sizeof(RC_HW_msg)-1, "%s", g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_MSG_PART1));

	//keysetup menu
	CMenuWidget* keySettings = new CMenuWidget(LOCALE_MAINSETTINGS_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP);
	keySettings->addIntroItems(LOCALE_MAINSETTINGS_KEYBINDING);

	//keybindings menu
	CMenuWidget bindSettings(LOCALE_MAINSETTINGS_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP_KEYBINDING);

	//keybindings
	int shortcut = 1;
	showKeyBindSetup(&bindSettings);
	CMenuForwarder * mf;

	mf = new CMenuForwarder(LOCALE_KEYBINDINGMENU_HEAD, true, NULL, &bindSettings, NULL, CRCInput::convertDigitToKey(shortcut++));
	mf->setHint("", LOCALE_MENU_HINT_KEY_BINDING);
	keySettings->addItem(mf);
	keySettings->addItem(GenericMenuSeparator);

	mf = new CMenuForwarder(LOCALE_EXTRA_LOADKEYS, true, NULL, this, "loadkeys", CRCInput::convertDigitToKey(shortcut++));
	mf->setHint("", LOCALE_MENU_HINT_KEY_LOAD);
	keySettings->addItem(mf);

	mf = new CMenuForwarder(LOCALE_EXTRA_SAVEKEYS, true, NULL, this, "savekeys", CRCInput::convertDigitToKey(shortcut++));
	mf->setHint("", LOCALE_MENU_HINT_KEY_SAVE);
	keySettings->addItem(mf);

	//rc tuning
	CStringInput keySettings_repeat_genericblocker(LOCALE_KEYBINDINGMENU_REPEATBLOCKGENERIC, g_settings.repeat_genericblocker, 3, LOCALE_REPEATBLOCKER_HINT_1, LOCALE_REPEATBLOCKER_HINT_2, "0123456789 ", this);
	CStringInput keySettings_repeatBlocker(LOCALE_KEYBINDINGMENU_REPEATBLOCK, g_settings.repeat_blocker, 3, LOCALE_REPEATBLOCKER_HINT_1, LOCALE_REPEATBLOCKER_HINT_2, "0123456789 ", this);

	keySettings->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_KEYBINDINGMENU_RC));
	if (RC_HW_SELECT) {
		CMenuOptionChooser * mc = new CMenuOptionChooser(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE, &g_settings.remote_control_hardware, KEYBINDINGMENU_REMOTECONTROL_HARDWARE_OPTIONS, KEYBINDINGMENU_REMOTECONTROL_HARDWARE_OPTION_COUNT, true);
		mc->setHint("", LOCALE_MENU_HINT_KEY_HARDWARE);
		keySettings->addItem(mc);
	}
	mf = new CMenuForwarder(LOCALE_KEYBINDINGMENU_REPEATBLOCK, true, g_settings.repeat_blocker, &keySettings_repeatBlocker);
	mf->setHint("", LOCALE_MENU_HINT_KEY_REPEATBLOCK);
	keySettings->addItem(mf);

	mf = new CMenuForwarder(LOCALE_KEYBINDINGMENU_REPEATBLOCKGENERIC, true, g_settings.repeat_genericblocker, &keySettings_repeat_genericblocker);
	mf->setHint("", LOCALE_MENU_HINT_KEY_REPEATBLOCKGENERIC);
	keySettings->addItem(mf);

	int res = keySettings->exec(NULL, "");

	//check if rc hardware selection has changed before leaving the menu
	if (org_remote_control_hardware != g_settings.remote_control_hardware) {
		g_RCInput->CRCInput::set_rc_hw();
		strcat(RC_HW_msg, RC_HW_str[org_remote_control_hardware]);
		strcat(RC_HW_msg, g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_MSG_PART2));
		strcat(RC_HW_msg, RC_HW_str[g_settings.remote_control_hardware]);
		strcat(RC_HW_msg, g_Locale->getText(LOCALE_KEYBINDINGMENU_REMOTECONTROL_HARDWARE_MSG_PART3));
		if(ShowMsgUTF(LOCALE_MESSAGEBOX_INFO, RC_HW_msg, CMessageBox::mbrNo, CMessageBox::mbYes | CMessageBox::mbNo, NEUTRINO_ICON_INFO, 450, 15, true) == CMessageBox::mbrNo) {
			g_settings.remote_control_hardware = org_remote_control_hardware;
			g_RCInput->CRCInput::set_rc_hw();
		}
	}

	delete keySettings;
	return res;
}


void CKeybindSetup::showKeyBindSetup(CMenuWidget *bindSettings)
{
	CMenuForwarder * mf;

	bindSettings->addIntroItems(LOCALE_KEYBINDINGMENU_HEAD);

	for (int i = 0; i < KEYBINDS_COUNT; i++)
		keychooser[i] = new CKeyChooser(key_settings[i].keyvalue_p, key_settings[i].keydescription/*as head caption*/, NEUTRINO_ICON_SETTINGS);

	//modes
	CMenuWidget* bindSettings_modes = new CMenuWidget(LOCALE_KEYBINDINGMENU_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP_KEYBINDING_MODES);
	showKeyBindModeSetup(bindSettings_modes);
	mf = new CMenuDForwarder(LOCALE_KEYBINDINGMENU_MODECHANGE, true, NULL, bindSettings_modes, NULL, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);
	mf->setHint("", LOCALE_MENU_HINT_KEY_MODECHANGE);
	bindSettings->addItem(mf);

	// channellist keybindings
	CMenuWidget* bindSettings_chlist = new CMenuWidget(LOCALE_KEYBINDINGMENU_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP_KEYBINDING_CHANNELLIST);
	showKeyBindChannellistSetup(bindSettings_chlist);
	mf = new CMenuDForwarder(LOCALE_KEYBINDINGMENU_CHANNELLIST, true, NULL, bindSettings_chlist, NULL, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);
	mf->setHint("", LOCALE_MENU_HINT_KEY_CHANNELLIST);
	bindSettings->addItem(mf);

	// Zapping keys quickzap
	CMenuWidget* bindSettings_qzap = new CMenuWidget(LOCALE_KEYBINDINGMENU_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP_KEYBINDING_QUICKZAP);
	showKeyBindQuickzapSetup(bindSettings_qzap);
	mf = new CMenuDForwarder(LOCALE_KEYBINDINGMENU_QUICKZAP, true, NULL, bindSettings_qzap, NULL, CRCInput::RC_yellow, NEUTRINO_ICON_BUTTON_YELLOW);
	mf->setHint("", LOCALE_MENU_HINT_KEY_QUICKZAP);
 	bindSettings->addItem(mf);

	//movieplayer
	CMenuWidget* bindSettings_mplayer = new CMenuWidget(LOCALE_KEYBINDINGMENU_HEAD, NEUTRINO_ICON_KEYBINDING, width, MN_WIDGET_ID_KEYSETUP_KEYBINDING_MOVIEPLAYER);
	showKeyBindMovieplayerSetup(bindSettings_mplayer);
	mf = new CMenuDForwarder(LOCALE_MAINMENU_MOVIEPLAYER, true, NULL, bindSettings_mplayer, NULL, CRCInput::RC_blue, NEUTRINO_ICON_BUTTON_BLUE);
	mf->setHint("", LOCALE_MENU_HINT_KEY_MOVIEPLAYER);
	bindSettings->addItem(mf);

	//misc
	bindSettings->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_KEYBINDINGMENU_MISC));
	//bindSettings->addItem(new CMenuDForwarder(keydescription[KEY_PLUGIN], true, NULL, keychooser[KEY_PLUGIN]));
	// unlock
	mf = new CMenuDForwarder(key_settings[KEY_UNLOCK].keydescription, true, keychooser[KEY_UNLOCK]->getKeyName(), keychooser[KEY_UNLOCK]);
	mf->setHint("", key_settings[KEY_UNLOCK].hint);
	bindSettings->addItem(mf);
	// screenshot
	mf = new CMenuDForwarder(key_settings[KEY_SCREENSHOT].keydescription, true, keychooser[KEY_SCREENSHOT]->getKeyName(), keychooser[KEY_SCREENSHOT]);
	mf->setHint("", key_settings[KEY_SCREENSHOT].hint);
	bindSettings->addItem(mf);

	//bindSettings->addItem(new CMenuOptionChooser(LOCALE_EXTRA_ZAP_CYCLE, &g_settings.zap_cycle, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true));
	// left-exit, FIXME is this option really change anything ??
	CMenuOptionChooser * mc = new CMenuOptionChooser(LOCALE_EXTRA_MENU_LEFT_EXIT, &g_settings.menu_left_exit, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true);
	mc->setHint("", LOCALE_MENU_HINT_KEY_LEFT_EXIT);
	bindSettings->addItem(mc);

	// audio for audio player
	mc = new CMenuOptionChooser(LOCALE_EXTRA_AUDIO_RUN_PLAYER, &g_settings.audio_run_player, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true);
	mc->setHint("", LOCALE_MENU_HINT_KEY_AUDIO);
	bindSettings->addItem(mc);

	// right key
	mc = new CMenuOptionChooser(LOCALE_KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV, &g_settings.mode_left_right_key_tv, KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_OPTIONS, KEYBINDINGMENU_MODE_LEFT_RIGHT_KEY_TV_COUNT, true);
	mc->setHint("", LOCALE_MENU_HINT_KEY_RIGHT);
	bindSettings->addItem(mc);
}

void CKeybindSetup::showKeyBindModeSetup(CMenuWidget *bindSettings_modes)
{
	CMenuForwarder * mf;
	bindSettings_modes->addIntroItems(LOCALE_KEYBINDINGMENU_MODECHANGE);

	// tv/radio
	mf = new CMenuDForwarder(key_settings[KEY_TV_RADIO_MODE].keydescription, true, keychooser[KEY_TV_RADIO_MODE]->getKeyName(), keychooser[KEY_TV_RADIO_MODE], NULL, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED);
	mf->setHint("", key_settings[KEY_TV_RADIO_MODE].hint);
	bindSettings_modes->addItem(mf);

	mf = new CMenuDForwarder(key_settings[KEY_POWER_OFF].keydescription, true, keychooser[KEY_POWER_OFF]->getKeyName(), keychooser[KEY_POWER_OFF], NULL, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN);
	mf->setHint("", key_settings[KEY_POWER_OFF].hint);
	bindSettings_modes->addItem(mf);
}

void CKeybindSetup::showKeyBindChannellistSetup(CMenuWidget *bindSettings_chlist)
{
	bindSettings_chlist->addIntroItems(LOCALE_KEYBINDINGMENU_CHANNELLIST);
#if 0
	CMenuOptionChooser *oj = new CMenuOptionChooser(LOCALE_KEYBINDINGMENU_BOUQUETHANDLING, &g_settings.bouquetlist_mode, KEYBINDINGMENU_BOUQUETHANDLING_OPTIONS, KEYBINDINGMENU_BOUQUETHANDLING_OPTION_COUNT, true );
	bindSettings_chlist->addItem(oj);
#endif
	for (int i = KEY_PAGE_UP; i <= KEY_CURRENT_TRANSPONDER; i++) {
		CMenuForwarder * mf = new CMenuDForwarder(key_settings[i].keydescription, true, keychooser[i]->getKeyName(), keychooser[i]);
		mf->setHint("", key_settings[i].hint);
		bindSettings_chlist->addItem(mf);
	}

	CMenuOptionChooser * mc = new CMenuOptionChooser(LOCALE_EXTRA_SMS_CHANNEL, &g_settings.sms_channel, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true);
	mc->setHint("", LOCALE_MENU_HINT_KEY_CHANNEL_SMS);
	bindSettings_chlist->addItem(mc);
}

void CKeybindSetup::showKeyBindQuickzapSetup(CMenuWidget *bindSettings_qzap)
{
	bindSettings_qzap->addIntroItems(LOCALE_KEYBINDINGMENU_QUICKZAP);

	for (int i = KEY_CHANNEL_UP; i <= KEY_LASTCHANNEL; i++) {
		CMenuForwarder * mf = new CMenuDForwarder(key_settings[i].keydescription, true, keychooser[i]->getKeyName(), keychooser[i]);
		mf->setHint("", key_settings[i].hint);
		bindSettings_qzap->addItem(mf);
	}
}

void CKeybindSetup::showKeyBindMovieplayerSetup(CMenuWidget *bindSettings_mplayer)
{
	bindSettings_mplayer->addIntroItems(LOCALE_MAINMENU_MOVIEPLAYER);

	for (int i = MPKEY_REWIND; i < MPKEY_PLUGIN; i++) {
		CMenuForwarder * mf = new CMenuDForwarder(key_settings[i].keydescription, true, keychooser[i]->getKeyName(), keychooser[i]);
		mf->setHint("", key_settings[i].hint);
		bindSettings_mplayer->addItem(mf);
	}
}

bool CKeybindSetup::changeNotify(const neutrino_locale_t OptionName, void * /* data */)
{
	if (ARE_LOCALES_EQUAL(OptionName, LOCALE_KEYBINDINGMENU_REPEATBLOCKGENERIC) ||
			ARE_LOCALES_EQUAL(OptionName, LOCALE_KEYBINDINGMENU_REPEATBLOCK)) {
		unsigned int fdelay = atoi(g_settings.repeat_blocker);
		unsigned int xdelay = atoi(g_settings.repeat_genericblocker);

		g_RCInput->repeat_block = fdelay * 1000;
		g_RCInput->repeat_block_generic = xdelay * 1000;

		int fd = g_RCInput->getFileHandle();
		ioctl(fd, IOC_IR_SET_F_DELAY, fdelay);
		ioctl(fd, IOC_IR_SET_X_DELAY, xdelay);
	}
	return false;
}
