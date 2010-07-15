/*
 * $Header: /cvs/tuxbox/apps/tuxbox/neutrino/src/gui/widget/buttons.cpp,v 1.2 2004/03/14 22:20:05 thegoodguy Exp $
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gui/widget/buttons.h>

#include <gui/color.h>
#if 1
void paintButtons(CFrameBuffer * const frameBuffer, Font * const font, const CLocaleManager * const localemanager, const int x, const int y, const unsigned int buttonwidth, const unsigned int count, const struct button_label * const content)
{
	for (unsigned int i = 0; i < count; i++)
	{
	  	int iw = 0, ih = 0;
		int fh = font->getHeight();

		frameBuffer->getIconSize(content[i].button, &iw, &ih);
		frameBuffer->paintIcon(content[i].button, x + i * buttonwidth, y);
		font->RenderString(x + i * buttonwidth + iw + 5, y + fh-(fh-ih)/2, buttonwidth - iw - 5, localemanager->getText(content[i].locale), COL_INFOBAR, 0, true); // UTF-8
	}
}
#endif
void paintButtons(CFrameBuffer * const frameBuffer, Font * const font, const CLocaleManager * const localemanager, const int x, const int y, const unsigned int buttonwidth, const unsigned int height, const unsigned int count, const struct button_label * const content)
{
	int iw, ih;
	for (unsigned int i = 0; i < count; i++)
	{
		int fh = font->getHeight();
		frameBuffer->getIconSize(content[i].button, &iw, &ih);
		frameBuffer->paintIcon(content[i].button, x + i * buttonwidth, y, height);
		font->RenderString(x + i * buttonwidth + iw + 5, y + (height-fh)/2 + fh, buttonwidth - iw - 5, localemanager->getText(content[i].locale), COL_INFOBAR, 0, true); // UTF-8
	}
}
