/*
	Based up Neutrino-GUI - Tuxbox-Project
	Copyright (C) 2001 by Steffen Hehn 'McClean'

	progressbar_setup menu
	Suggested by tomworld

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

#include "progressbar_setup.h"

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>
#include <neutrino_menue.h>

#include <driver/screen_max.h>

#include <system/debug.h>

#define LOCALE_MISCSETTINGS_INFOBAR_POSITION_COUNT 4
const CMenuOptionChooser::keyval  LOCALE_MISCSETTINGS_INFOBAR_POSITION_OPTIONS[LOCALE_MISCSETTINGS_INFOBAR_POSITION_COUNT]=
{
   { 0 , LOCALE_MISCSETTINGS_INFOBAR_POSITION_0 },
   { 1 , LOCALE_MISCSETTINGS_INFOBAR_POSITION_1 },
   { 2 , LOCALE_MISCSETTINGS_INFOBAR_POSITION_2 },
   { 3 , LOCALE_MISCSETTINGS_INFOBAR_POSITION_3 }
};

#define LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_COUNT 4
const CMenuOptionChooser::keyval  LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_OPTIONS[LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_COUNT]=
{
   { 0 , LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_0 },
   { 1 , LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_1 },
   { 2 , LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_2 },
   { 3 , LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_3 }
};

CProgressbarSetup::CProgressbarSetup()
{
	width = w_max (40, 10); //%
}

CProgressbarSetup::~CProgressbarSetup()
{

}

int CProgressbarSetup::exec(CMenuTarget* parent, const std::string &)
{
	printf("[neutrino] init progressbar menu setup...\n");

	if (parent)
		parent->hide();

	return showMenu();
}

int CProgressbarSetup::showMenu()
{
	//menue init
	CMenuWidget *progress = new CMenuWidget(LOCALE_MAINMENU_SETTINGS, NEUTRINO_ICON_SETTINGS, width, MN_WIDGET_ID_PROGRESSBAR);

	//intros: back ande save
	progress->addIntroItems(LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR);

	//infobar progresscolor on/off
	COnOffNotifier* miscProgressNotifier = new COnOffNotifier(0);

	CMenuOptionChooser *progresscolor;
	progresscolor = new CMenuOptionChooser(LOCALE_PROGRESSBAR_COLOR, &g_settings.progressbar_color, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, miscProgressNotifier);
	progresscolor->setHint("", LOCALE_MENU_HINT_PROGRESSBAR_COLOR);

	//infobar design
	CMenuOptionChooser *progressdesign = new CMenuOptionChooser(LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN, &g_settings.progressbar_design, LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_OPTIONS, LOCALE_MISCSETTINGS_INFOBAR_PROGRESSBAR_DESIGN_COUNT, g_settings.progressbar_color);
	progressdesign->setHint("", LOCALE_MENU_HINT_INFOBAR_PROGRESSBAR_DESIGN);

	//infobar progressbarposition
	CMenuOptionChooser *progressbarposition;
	progressbarposition = new CMenuOptionChooser(LOCALE_MISCSETTINGS_INFOBAR_POSITION, &g_settings.infobar_progressbar, LOCALE_MISCSETTINGS_INFOBAR_POSITION_OPTIONS, LOCALE_MISCSETTINGS_INFOBAR_POSITION_COUNT, true);
	progressbarposition->setHint("", LOCALE_MENU_HINT_INFOBAR_POSITION);

	miscProgressNotifier->addItem(progressdesign);

	//paint items
	progress->addItem(progresscolor);
	progress->addItem(progressdesign);
	progress->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_MISCSETTINGS_INFOBAR));
	progress->addItem(progressbarposition);

	int res = progress->exec (NULL, "");
	delete miscProgressNotifier;
	delete progress;

	return res;
}
