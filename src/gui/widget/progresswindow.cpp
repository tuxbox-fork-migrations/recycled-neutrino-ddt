/*
	Based up Neutrino-GUI - Tuxbox-Project
	Copyright (C) 2001 by Steffen Hehn 'McClean'

	Implementation of CComponent Window class.
	Copyright (C) 2014 Thilo Graf 'dbt'

	License: GPL

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "progresswindow.h"

#include <global.h>
#include <neutrino.h>

#include <driver/fontrenderer.h>
#include <driver/neutrinofonts.h>
#include <driver/rcinput.h>


CProgressWindow::CProgressWindow(CComponentsForm *parent) 
: CComponentsWindow(0, 0, 700, 200, string(), NEUTRINO_ICON_INFO, NULL, parent)
{
	Init();
}

void CProgressWindow::Init()
{
	global_progress = local_progress = 100;

	showFooter(false);
	shadow = true;

	int x_item = 10;
	int y_item = 10;
	int w_item = width-2*x_item;
	int h_item = 20;

	//create status text object
	status_txt = new CComponentsLabel();
	status_txt->setDimensionsAll(x_item, y_item, w_item, h_item);
	status_txt->setColorBody(col_body);
	addWindowItem(status_txt);
	y_item += 2*h_item;

	//create local_bar object
	local_bar = new CProgressBar();
	local_bar->setDimensionsAll(x_item, y_item, w_item, h_item);
	local_bar->setColorBody(col_body);
	local_bar->setActiveColor(COL_MENUCONTENT_PLUS_7);
	local_bar->setFrameThickness(2);
	local_bar->setColorFrame(COL_MENUCONTENT_PLUS_7);
	addWindowItem(local_bar);
	y_item += 2*h_item;

	//create global_bar object
	global_bar = new CProgressBar();
	global_bar->setDimensionsAll(x_item, y_item, w_item, h_item);
	global_bar->setColorBody(col_body);
	global_bar->setActiveColor(COL_MENUCONTENT_PLUS_7);
	global_bar->setFrameThickness(2);
	global_bar->setColorFrame(COL_MENUCONTENT_PLUS_7);
	addWindowItem(global_bar);
	y_item += 2*h_item;

	height = y_item + ccw_head->getHeight() + 10;

	setCenterPos();
}

void CProgressWindow::setTitle(const neutrino_locale_t title)
{
	setWindowCaption(title);

#ifdef VFD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1,NULL,-1,g_Locale->getText(ccw_caption)); // set global text in VFD
#endif // VFD_UPDATE
}


void CProgressWindow::showGlobalStatus(const unsigned int prog)
{
	if (global_progress == prog)
		return;

	global_progress = prog;
	global_bar->setValues(prog, 100);
	global_bar->paint(false);

#ifdef VFD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1,NULL,global_progress);
#endif // VFD_UPDATE
}

void CProgressWindow::showLocalStatus(const unsigned int prog)
{
	if (local_progress == prog)
		return;

	local_progress = prog;
	local_bar->setValues(prog, 100);
	local_bar->paint(false);

#ifdef VFD_UPDATE
	CVFD::getInstance()->showProgressBar2(local_progress);
#else
	CVFD::getInstance()->showPercentOver((uint8_t)local_progress);
#endif // VFD_UPDATE
}

void CProgressWindow::showStatusMessageUTF(const std::string & text)
{
	string txt = text;
	int w_txt = status_txt->getWidth();
	int h_txt = status_txt->getHeight();
	status_txt->setText(txt, CTextBox::CENTER, *CNeutrinoFonts::getInstance()->getDynFont(w_txt, h_txt, txt), COL_MENUCONTENT_TEXT);

	status_txt->paint(false);

#ifdef VFD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1,text.c_str()); // set local text in VFD
#endif // VFD_UPDATE
}


unsigned int CProgressWindow::getGlobalStatus(void)
{
	return global_progress;
}

void CProgressWindow::hide(bool no_restore)
{
	CComponentsWindow::hide(no_restore);
}

int CProgressWindow::exec(CMenuTarget* parent, const std::string & /*actionKey*/)
{
	if(parent)
	{
		parent->hide();
	}
	paint();

	return menu_return::RETURN_REPAINT;
}
