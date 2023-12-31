/*
	Based up Neutrino-GUI - Tuxbox-Project
	Copyright (C) 2001 by Steffen Hehn 'McClean'

	Classes for generic GUI-related components.
	Copyright (C) 2012, 2013, Thilo Graf 'dbt'

	License: GPL

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; if not, write to the
	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
	Boston, MA  02110-1301, USA.
*/

#ifndef __CC_DETAIL_LINE_H__
#define __CC_DETAIL_LINE_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cc_base.h"

//! Sub class of CComponents. Shows a connectline with given dimensions and color on screen.
/*!
Not usable as CCItem!
*/

class CComponentsDetailsLine : public CComponents
{
	private:
		///property: line width
		int dl_w;
		///property: lowest y position
		int y_down;
		///property: height of top marker
		int h_mark_top;
		///property: height of bottom marker
		int h_mark_down;

		///initialize all internal attributes
		void initVarDline(const int &x_pos, const int &y_pos_top, const int &y_pos_down,
			const int &h_mark_top_, const int &h_mark_down_,
			fb_pixel_t color_line, fb_pixel_t color_shadow);

	public:
		CComponentsDetailsLine(const int &x_pos = 1, const int &y_pos_top = 1, const int &y_pos_down = 1,
			const int &h_mark_top_ = CC_HEIGHT_MIN, const int &h_mark_down_ = CC_HEIGHT_MIN,
			fb_pixel_t color_line = COL_FRAME_PLUS_0, fb_pixel_t color_shadow = COL_SHADOW_PLUS_0);
		~CComponentsDetailsLine();

		///set colors
		void setColors(fb_pixel_t color_line, fb_pixel_t color_shadow)
		{
			col_body = color_line;
			col_shadow = color_shadow;
		};
		///set colors with system settings
		void syncSysColors();
		///set property: lowest y position
		void setYPosDown(const int &y_pos_down)
		{
			y_down = y_pos_down;
		};
		///set property: height of top marker
		void setHMarkTop(const int &h_mark_top_)
		{
			h_mark_top = h_mark_top_ - 2 * shadow_w;
		};
		///property: height of bottom marker
		void setHMarkDown(const int &h_mark_down_)
		{
			h_mark_down = h_mark_down_ - 2 * shadow_w;
		};
		///set all positions and dimensions of details line at once
		void setDimensionsAll(const int &x_pos, const int &y_pos, const int &y_pos_down, const int &h_mark_top_, const int &h_mark_down_)
		{
			setXPos(x_pos);
			setYPos(y_pos);
			setYPosDown(y_pos_down);
			setHMarkTop(h_mark_top_);
			setHMarkDown(h_mark_down_);
		}
		///property: set line thickness
		void setLineWidth(const int &w)
		{
			dl_w = w;
		}

		///paint all to screen
		void paint(bool do_save_bg = CC_SAVE_SCREEN_YES);
};

#endif
