/*
  Neutrino-GUI  -   DBoxII-Project

  Part of Movieplayer (c) 2003, 2004 by gagga
  Based on code by Zwen. Thanks.

  $Id: bookmarkmanager.cpp,v 1.12 2004/05/20 07:38:34 thegoodguy Exp $

  Homepage: http://www.giggo.de/dbox2/movieplayer.html

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

#include <gui/bookmarkmanager.h>

#include <global.h>
#include <neutrino.h>

#include <system/settings.h>
#include <driver/display.h>
#include <driver/screen_max.h>
#include <driver/display.h>
#include <driver/fontrenderer.h>
#include <gui/components/cc.h>
#include <gui/widget/msgbox.h>
#include <gui/widget/hintbox.h>
#include <gui/widget/stringinput.h>
#include <gui/widget/icons.h>
#include <gui/widget/buttons.h>
#include <system/helpers.h>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define info_height 60


CBookmark::CBookmark(const std::string & inName, const std::string & inUrl, const std::string & inTime)
{
	name = inName;
	url = inUrl;
	time = inTime;
}

//------------------------------

int CBookmarkManager::addBookmark (CBookmark inBookmark) {
	if (bookmarks.size() < MAXBOOKMARKS)
	{
		bookmarks.push_back(inBookmark);
		printf("CBookmarkManager: addBookmark: %s %s\n", inBookmark.getName(), inBookmark.getTime());
		bookmarksmodified = true;
		return 0;
	}
	// TODO:show dialog to delete old bookmark
	return -1;
}

//------------------------------------------------------------------------
inline int CBookmarkManager::createBookmark (const std::string & name, const std::string & url, const std::string & time) {
	return addBookmark(CBookmark(name, url, time));
}

int CBookmarkManager::createBookmark (const std::string & url, const std::string & time) {
	std::string bookmarkname;
	CStringInputSMS bookmarkname_input(LOCALE_MOVIEPLAYER_BOOKMARKNAME, &bookmarkname, 25, LOCALE_MOVIEPLAYER_BOOKMARKNAME_HINT1, LOCALE_MOVIEPLAYER_BOOKMARKNAME_HINT2, "abcdefghijklmnopqrstuvwxyz0123456789-_", this);
	bookmarkname_input.exec(NULL, "");
	if (bookmarkname_entered)
	{
		bookmarkname_entered = false;
		return createBookmark(bookmarkname, url, time);
	}
	return -1;
}

//------------------------------------------------------------------------
void CBookmarkManager::readBookmarkFile() {
	if (bookmarkfile.loadConfig(BOOKMARKFILE))
	{
		bookmarksmodified = false;
		bookmarks.clear();

		unsigned int bookmarkcount = bookmarkfile.getInt32("bookmarkcount", 0);

		if (bookmarkcount > MAXBOOKMARKS)
			bookmarkcount = MAXBOOKMARKS;

		while (bookmarkcount-- > 0)
		{
			std::string bookmarkstring = "bookmark" + to_string(bookmarkcount) + ".";
			std::string bookmarkname = bookmarkfile.getString(bookmarkstring + "name", "");
			std::string bookmarkurl = bookmarkfile.getString(bookmarkstring + "url", "");
			std::string bookmarktime = bookmarkfile.getString(bookmarkstring + "time", "");
			bookmarks.push_back(CBookmark(bookmarkname, bookmarkurl, bookmarktime));
		}
	}
}

//------------------------------------------------------------------------
void CBookmarkManager::writeBookmarkFile() {

	printf("CBookmarkManager: Writing bookmark file\n");

	bookmarkfile.clear();
	unsigned int bookmarkcount = 0;
	for (std::vector<CBookmark>::const_iterator it = bookmarks.begin(); it != bookmarks.end(); ++it, bookmarkcount++)
	{
		std::string bookmarkstring = "bookmark" + to_string(bookmarkcount) + ".";
		bookmarkfile.setString(bookmarkstring + "name", it->getName());
		bookmarkfile.setString(bookmarkstring + "url", it->getUrl());
		bookmarkfile.setString(bookmarkstring + "time", it->getTime());
	}
	bookmarkfile.setInt32("bookmarkcount", bookmarks.size());
	bookmarkfile.saveConfig(BOOKMARKFILE);

	bookmarksmodified = false;
}

//------------------------------------------------------------------------

CBookmarkManager::CBookmarkManager() : bookmarkfile ('\t')
{
	bookmarkname_entered = false;
	bookmarksmodified = false;
	readBookmarkFile();
}

//------------------------------------------------------------------------

CBookmarkManager::~CBookmarkManager () {
	flush();
}

//------------------------------------------------------------------------

bool CBookmarkManager::changeNotify(const neutrino_locale_t, void *)
{
	bookmarkname_entered = true;
	return false;
}

//------------------------------------------------------------------------

void CBookmarkManager::flush() {
	if (bookmarksmodified) {
		writeBookmarkFile();
	}
}

//------------------------------------------------------------------------
void CBookmarkManager::paintItem(int pos)
{
	int ypos = y+ theight+0 + pos*fheight*2;

	unsigned int currpos = liststart + pos;

	bool i_selected	= currpos == selected;
	bool i_marked	= false;
	bool i_switch	= false; //(currpos < bookmarks.size()) && (pos & 1);

	fb_pixel_t color;
	fb_pixel_t bgcolor;

	getItemColors(color, bgcolor, i_selected, i_marked, i_switch);

	int real_width=width;
	if (bookmarks.size()>listmaxshow)
	{
		real_width-=15; //scrollbar
	}

	frameBuffer->paintBoxRel(x,ypos, real_width, 2*fheight, bgcolor);
	if (currpos < bookmarks.size())
	{
		CBookmark theBookmark = bookmarks[currpos];
		g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x+10,ypos+fheight, real_width-10, theBookmark.getName(), color, fheight);
		g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x+10,ypos+2*fheight, real_width-10, theBookmark.getUrl(), color, fheight);

		// LCD Display
		if (i_selected)
		{
#if BOXMODEL_DM820 || BOXMODEL_DM7080 || BOXMODEL_DM8000 || BOXMODEL_DM7020HD || BOXMODEL_DM800SE || BOXMODEL_DM800SEV2
			CVFD::getInstance()->setMode(CVFD::MODE_MENU_UTF8, theBookmark.getName());
#else
			CVFD::getInstance()->showMenuText(0, theBookmark.getName(), -1, true); // UTF-8
			CVFD::getInstance()->showMenuText(1, theBookmark.getUrl(), -1, true); // UTF-8
#endif
		}
	}
	frameBuffer->blit();
}

//------------------------------------------------------------------------

void CBookmarkManager::hide()
{
	if (visible)
	{
		frameBuffer->paintBackgroundBoxRel(x, y, width, height+ info_height+ 5);
		frameBuffer->blit();
		visible = false;
	}
}

//------------------------------------------------------------------------
void CBookmarkManager::paintHead()
{
	CComponentsHeader header(x, y, width, theight, LOCALE_BOOKMARKMANAGER_NAME, NEUTRINO_ICON_BOOKMARK_MANAGER, CComponentsHeader::CC_BTN_HELP);
	header.paint(CC_SAVE_SCREEN_NO);
}

const struct button_label BookmarkmanagerButtons[2] =
{
	{ NEUTRINO_ICON_BUTTON_RED   , LOCALE_BOOKMARKMANAGER_DELETE },
	{ NEUTRINO_ICON_BUTTON_YELLOW, LOCALE_BOOKMARKMANAGER_RENAME }
};

//------------------------------------------------------------------------
void CBookmarkManager::paintFoot()
{
	int ButtonWidth = (width - 20) / 4;
	frameBuffer->paintBoxRel(x,y+height, width, footerHeight, COL_MENUFOOT_PLUS_0);
	frameBuffer->paintHLine(x, x+width,  y, COL_MENUFOOT_PLUS_0);

	if (bookmarks.empty()) {
		frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_OKAY, x+width- 1* ButtonWidth + 10, y+height);
		g_Font[SNeutrinoSettings::FONT_TYPE_MENU_FOOT]->RenderString(x+width-1 * ButtonWidth + 38, y+height+footerHeight - 2, ButtonWidth- 28, g_Locale->getText(LOCALE_BOOKMARKMANAGER_SELECT), COL_INFOBAR_TEXT);
	}
	else
	{
		::paintButtons(x + 10, y + height + 4, width, 2, BookmarkmanagerButtons, footerHeight, ButtonWidth);

		frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_OKAY, x+width- 1* ButtonWidth + 10, y+height);
		g_Font[SNeutrinoSettings::FONT_TYPE_MENU_FOOT]->RenderString(x+width-1 * ButtonWidth + 38, y+height+footerHeight - 2, ButtonWidth- 28, g_Locale->getText(LOCALE_BOOKMARKMANAGER_SELECT), COL_INFOBAR_TEXT);
	}
}

//------------------------------------------------------------------------
void CBookmarkManager::paint()
{
	unsigned int page_nr = (listmaxshow == 0) ? 0 : (selected / listmaxshow);
	liststart = page_nr * listmaxshow;

	CVFD::getInstance()->setMode(CVFD::MODE_MENU_UTF8, g_Locale->getText(LOCALE_BOOKMARKMANAGER_NAME));

	paintHead();

	for (unsigned int count=0; count<listmaxshow; count++)
	{
		paintItem(count);
	}
	if (bookmarks.size()>listmaxshow)
	{
		int ypos = y+ theight;
		int sb = 2*fheight* listmaxshow;
		frameBuffer->paintBoxRel(x+ width- 15,ypos, 15, sb,  COL_SCROLLBAR_PLUS_0);
		unsigned  int  tmp_max  =  listmaxshow;
		if(!tmp_max)
			tmp_max  =  1;
		int sbc= ((bookmarks.size()- 1)/ tmp_max)+ 1;
		if (sbc < 1)
			sbc = 1;

		frameBuffer->paintBoxRel(x+ width- 13, ypos+ 2+ page_nr * (sb-4)/sbc, 11, (sb-4)/sbc,  COL_SCROLLBAR_ACTIVE_PLUS_0);
	}

	paintFoot();
	visible = true;
	frameBuffer->blit();
}
