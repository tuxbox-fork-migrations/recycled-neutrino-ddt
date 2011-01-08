/*
	$similar port: lcd_setup.cpp,v 1.1 2010/07/30 20:52:16 tuxbox-cvs Exp $

	vfd setup implementation, similar to lcd_setup.cpp of tuxbox-cvs - Neutrino-GUI

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


#include "gui/vfd_setup.h"

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>

#include <gui/widget/icons.h>
#include <gui/widget/vfdcontroler.h>
#include <gui/widget/stringinput.h>

#include <driver/screen_max.h>

#include <system/debug.h>
#include <cs_api.h>

CVfdSetup::CVfdSetup()
{
	frameBuffer = CFrameBuffer::getInstance();

	width = w_max (30, 10);
	hheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->getHeight();
	mheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
	height 	= hheight+13*mheight+ 10;
	selected = -1;
	x	= getScreenStartX (width);
	y	= getScreenStartY (height);
}

CVfdSetup::~CVfdSetup()
{

}

void CVfdSetup::hide()
{
	frameBuffer->paintBackgroundBoxRel(x,y, width,height);
}


int CVfdSetup::exec(CMenuTarget* parent, const std::string &)
{
	dprintf(DEBUG_DEBUG, "init lcd setup\n");
	if(parent != NULL)
		parent->hide();

	showSetup();
	
	return menu_return::RETURN_REPAINT;	
}

#define LCDMENU_STATUSLINE_OPTION_COUNT 2
const CMenuOptionChooser::keyval LCDMENU_STATUSLINE_OPTIONS[LCDMENU_STATUSLINE_OPTION_COUNT] =
{
	{ 0, LOCALE_LCDMENU_STATUSLINE_PLAYTIME },
	{ 1, LOCALE_LCDMENU_STATUSLINE_VOLUME   }
	//,{ 2, LOCALE_LCDMENU_STATUSLINE_BOTH     }
};

#define LEDMENU_OPTION_COUNT 4
const CMenuOptionChooser::keyval LEDMENU_OPTIONS[LEDMENU_OPTION_COUNT] =
{
	{ 0, LOCALE_LEDCONTROLER_OFF },
	{ 1, LOCALE_LEDCONTROLER_ON_ALL },
	{ 2, LOCALE_LEDCONTROLER_ON_LED1 },
	{ 3, LOCALE_LEDCONTROLER_ON_LED2   }
};


void CVfdSetup::showSetup()
{
	CMenuWidget *vfds = new CMenuWidget(LOCALE_MAINMENU_SETTINGS, NEUTRINO_ICON_LCD, width);
	vfds->addIntroItems(LOCALE_LCDMENU_HEAD);
	vfds->setSelected(selected);

	CVfdControler* lcdsliders = new CVfdControler(LOCALE_LCDMENU_HEAD, NULL);
	bool vfd_enabled = (cs_get_revision() != 10);

	CStringInput * dim_time = new CStringInput(LOCALE_LCDMENU_DIM_TIME, g_settings.lcd_setting_dim_time, 3, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE,"0123456789 ");
	vfds->addItem(new CMenuForwarder(LOCALE_LCDMENU_DIM_TIME, vfd_enabled, g_settings.lcd_setting_dim_time,dim_time));

	CStringInput * dim_brightness = new CStringInput(LOCALE_LCDMENU_DIM_BRIGHTNESS, g_settings.lcd_setting_dim_brightness, 3, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE,"0123456789 ");
	vfds->addItem(new CMenuForwarder(LOCALE_LCDMENU_DIM_BRIGHTNESS, vfd_enabled, g_settings.lcd_setting_dim_brightness,dim_brightness));

	vfds->addItem(GenericMenuSeparatorLine);
	vfds->addItem(new CMenuForwarder(LOCALE_LCDMENU_LCDCONTROLER, vfd_enabled, NULL, lcdsliders, NULL, CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED));
	
	if(cs_get_revision() > 7) 
	{
		CMenuWidget * ledMenu = new CMenuWidget(LOCALE_LCDMENU_HEAD, NEUTRINO_ICON_LCD);
		ledMenu->addIntroItems(LOCALE_LEDCONTROLER_MENU);
		ledMenu->addItem(new CMenuOptionChooser(LOCALE_LEDCONTROLER_MODE_TV, &g_settings.led_tv_mode, LEDMENU_OPTIONS, LEDMENU_OPTION_COUNT, true, new CLedControlNotifier()));
		ledMenu->addItem(new CMenuOptionChooser(LOCALE_LEDCONTROLER_MODE_STANDBY, &g_settings.led_standby_mode, LEDMENU_OPTIONS, LEDMENU_OPTION_COUNT, true));
		ledMenu->addItem(new CMenuOptionChooser(LOCALE_LEDCONTROLER_MODE_DEEPSTANDBY, &g_settings.led_deep_mode, LEDMENU_OPTIONS, LEDMENU_OPTION_COUNT, true));
		ledMenu->addItem(new CMenuOptionChooser(LOCALE_LEDCONTROLER_BLINK, &g_settings.led_blink, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true));
		vfds->addItem(new CMenuForwarder(LOCALE_LEDCONTROLER_MENU, true, NULL, ledMenu, NULL, CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN));
	}

	vfds->addItem(GenericMenuSeparatorLine);

	CMenuOptionChooser* oj = new CMenuOptionChooser(LOCALE_LCDMENU_STATUSLINE, &g_settings.lcd_setting[SNeutrinoSettings::LCD_SHOW_VOLUME], LCDMENU_STATUSLINE_OPTIONS, LCDMENU_STATUSLINE_OPTION_COUNT, vfd_enabled);
	vfds->addItem(oj);
	
	vfds->exec(NULL, "");
	vfds->hide();
	selected = vfds->getSelected();
	delete vfds;
}

