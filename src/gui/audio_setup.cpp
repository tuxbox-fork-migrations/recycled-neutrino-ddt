/*
	$port: audio_setup.cpp,v 1.6 2010/12/06 21:00:15 tuxbox-cvs Exp $

	audio setup implementation - Neutrino-GUI

	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2009 T. Graf 'dbt'
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


#include "gui/audio_setup.h"

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>

#include <gui/widget/icons.h>
#include <gui/widget/stringinput.h>

#include <driver/screen_max.h>

#include <system/debug.h>

extern CAudioSetupNotifier	* audioSetupNotifier;

CAudioSetup::CAudioSetup()
{
	frameBuffer = CFrameBuffer::getInstance();

	width = w_max (40, 10);
	hheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->getHeight();
	mheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
	height = hheight+13*mheight+ 10;
	selected = -1;
	x = getScreenStartX (width);
	y = getScreenStartY (height);
}

CAudioSetup::~CAudioSetup()
{

}

int CAudioSetup::exec(CMenuTarget* parent, const std::string &/*actionKey*/)
{
	dprintf(DEBUG_DEBUG, "init audio setup\n");
	int   res = menu_return::RETURN_REPAINT;
 
	if (parent)
	{
		parent->hide();
	}

	showAudioSetup();
	
	return res;
}

void CAudioSetup::hide()
{
	frameBuffer->paintBackgroundBoxRel(x,y, width,height);
}


#define AUDIOMENU_ANALOGOUT_OPTION_COUNT 3
const CMenuOptionChooser::keyval AUDIOMENU_ANALOGOUT_OPTIONS[AUDIOMENU_ANALOGOUT_OPTION_COUNT] =
{
	{ 0, LOCALE_AUDIOMENU_STEREO    },
	{ 1, LOCALE_AUDIOMENU_MONOLEFT  },
	{ 2, LOCALE_AUDIOMENU_MONORIGHT }
};

#define AUDIOMENU_SRS_OPTION_COUNT 2
const CMenuOptionChooser::keyval AUDIOMENU_SRS_OPTIONS[AUDIOMENU_SRS_OPTION_COUNT] =
{
	{ 0 , LOCALE_SRS_ALGO_LIGHT },
	{ 1 , LOCALE_SRS_ALGO_NORMAL }
};

#define AUDIOMENU_AVSYNC_OPTION_COUNT 3
const CMenuOptionChooser::keyval AUDIOMENU_AVSYNC_OPTIONS[AUDIOMENU_AVSYNC_OPTION_COUNT] =
{
	{ 0, LOCALE_OPTIONS_OFF },
	{ 1, LOCALE_OPTIONS_ON  },
	{ 2, LOCALE_AUDIOMENU_AVSYNC_AM }
};

// #define AUDIOMENU_CLOCKREC_OPTION_COUNT 2
// const CMenuOptionChooser::keyval AUDIOMENU_CLOCKREC_OPTIONS[AUDIOMENU_CLOCKREC_OPTION_COUNT] =
// {
// 	{ 0, LOCALE_OPTIONS_OFF },
// 	{ 1, LOCALE_OPTIONS_ON  },
// };

/* audio settings menu */
void CAudioSetup::showAudioSetup()
{
	//menue init
	CMenuWidget* audioSettings = new CMenuWidget(LOCALE_MAINSETTINGS_HEAD, NEUTRINO_ICON_SETTINGS, width);
	audioSettings->setSelected(selected);

	//analog modes (stereo, mono l/r...)
	CMenuOptionChooser * as_oj_analogmode 	= new CMenuOptionChooser(LOCALE_AUDIOMENU_ANALOG_MODE, &g_settings.audio_AnalogMode, AUDIOMENU_ANALOGOUT_OPTIONS, AUDIOMENU_ANALOGOUT_OPTION_COUNT, true, audioSetupNotifier);
		
	//dd subchannel auto on/off
	CMenuOptionChooser * as_oj_ddsubchn 	= new CMenuOptionChooser(LOCALE_AUDIOMENU_DOLBYDIGITAL, &g_settings.audio_DolbyDigital, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, audioSetupNotifier);
	
	//dd via hdmi
	CMenuOptionChooser * as_oj_dd_hdmi 	= new CMenuOptionChooser(LOCALE_AUDIOMENU_HDMI_DD, &g_settings.hdmi_dd, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, audioSetupNotifier);
	
	//dd via spdif
	CMenuOptionChooser * as_oj_dd_spdif 	= new CMenuOptionChooser(LOCALE_AUDIOMENU_SPDIF_DD, &g_settings.spdif_dd, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, audioSetupNotifier);
	
	//av synch
	CMenuOptionChooser * as_oj_avsync	= new CMenuOptionChooser(LOCALE_AUDIOMENU_AVSYNC, &g_settings.avsync, AUDIOMENU_AVSYNC_OPTIONS, AUDIOMENU_AVSYNC_OPTION_COUNT, true, audioSetupNotifier);
	
	//volume steps
	CMenuOptionNumberChooser * as_oj_vsteps = new CMenuOptionNumberChooser(LOCALE_AUDIOMENU_VOLUME_STEP, (int *)&g_settings.current_volume_step, true, 1, 100, NULL);

	//clock rec
//	CMenuOptionChooser * as_oj_clockrec new CMenuOptionChooser(LOCALE_AUDIOMENU_CLOCKREC, &g_settings.clockrec, AUDIOMENU_CLOCKREC_OPTIONS, AUDIOMENU_CLOCKREC_OPTION_COUNT, true, audioSetupNotifier);

	//SRS volumetech separator
	CMenuSeparator * as_sep_srs 		= new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_SRS_IQ);
	
	//SRS on/off
	CMenuOptionChooser * as_oj_srsonoff 	= new CMenuOptionChooser(LOCALE_SRS_IQ, &g_settings.srs_enable, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, audioSetupNotifier);
	
	//SRS algo
	CMenuOptionChooser * as_oj_algo 	= new CMenuOptionChooser(LOCALE_SRS_ALGO, &g_settings.srs_algo, AUDIOMENU_SRS_OPTIONS, AUDIOMENU_SRS_OPTION_COUNT, true, audioSetupNotifier);
	
	//SRS noise manage
	CMenuOptionChooser * as_oj_noise 	= new CMenuOptionChooser(LOCALE_SRS_NMGR, &g_settings.srs_nmgr_enable, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, audioSetupNotifier);
	
	//SRS reverence volume
	CMenuOptionNumberChooser * as_oj_volrev = new CMenuOptionNumberChooser(LOCALE_SRS_VOLUME, &g_settings.srs_ref_volume, true, 1, 100, audioSetupNotifier);

#if 0
	CStringInput * audio_PCMOffset = new CStringInput(LOCALE_AUDIOMENU_PCMOFFSET, g_settings.audio_PCMOffset, 2, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, "0123456789 ", audioSetupNotifier);
	CMenuForwarder *mf = new CMenuForwarder(LOCALE_AUDIOMENU_PCMOFFSET, true, g_settings.audio_PCMOffset, audio_PCMOffset );
#endif

	//paint items
	audioSettings->addIntroItems(LOCALE_MAINSETTINGS_AUDIO);
	//---------------------------------------------------------
	audioSettings->addItem(as_oj_analogmode);
	audioSettings->addItem(GenericMenuSeparatorLine);
	//---------------------------------------------------------
	audioSettings->addItem(as_oj_ddsubchn);
	audioSettings->addItem(as_oj_dd_hdmi);
	audioSettings->addItem(as_oj_dd_spdif);
	audioSettings->addItem(GenericMenuSeparatorLine);
	//---------------------------------------------------------
	audioSettings->addItem(as_oj_avsync);
	audioSettings->addItem(as_oj_vsteps);
//	audioSettings->addItem(as_clockrec);
	//---------------------------------------------------------
	audioSettings->addItem(as_sep_srs);
	audioSettings->addItem(as_oj_srsonoff);
	audioSettings->addItem(as_oj_algo);
	audioSettings->addItem(as_oj_noise);
	audioSettings->addItem(as_oj_volrev);
#if 0
	audioSettings->addItem(mf);
#endif
	
	audioSettings->exec(NULL, "");
	audioSettings->hide();
	selected = audioSettings->getSelected();
	delete audioSettings;
}

