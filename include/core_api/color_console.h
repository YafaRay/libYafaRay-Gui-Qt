#pragma once
/****************************************************************************
 *      color_console.h: A console coloring utility
 *      This is part of the yafray package
 *      Copyright (C) 2010 Rodrigo Placencia Vazquez
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef YAFARAY_COLOR_CONSOLE_H
#define YAFARAY_COLOR_CONSOLE_H

#include <yafray_constants.h>
#include <iostream>

BEGIN_YAFRAY

enum ConsoleColor : unsigned int
{
#if !defined(_WIN32)
	Black,
	Red,
	Green,
	Yellow,
	Blue,
	Magenta,
	Cyan,
	White,
	Default
#else
	Black 		= 0x0000,
	Red			= 0x0004,
	Green		= 0x0002,
	Yellow		= Red | Green,
	Blue		= 0x0001,
	Magenta		= Blue | Red,
	Cyan		= Blue | Green,
	White		= Red | Green | Blue,
	Default		= 0xFFFF
#endif
};

struct YAFRAYCORE_EXPORT SetColor final
{
	SetColor() : fg_col_(Default), bg_col_(Default), intense_(false) {}
	SetColor(ConsoleColor fg_color, ConsoleColor bg_color, bool intensecolor = false)
	{
#ifdef _WIN32
		fg_col_ = fg_color;
		bg_col_ = (bg_color != Default) ? (ConsoleColor)((unsigned int)bg_color << 4) : Default;
#else
		fg_col_ = (fg_color != Default) ? (ConsoleColor) ((unsigned int) fg_color + 30) : Default;
		bg_col_ = (bg_color != Default) ? (ConsoleColor) ((unsigned int) bg_color + 40) : Default;
#endif
		intense_ = intensecolor;
	}
	SetColor(unsigned int fg_color, bool intensecolor = false)
	{
#ifdef _WIN32
		fg_col_ = (ConsoleColor)((unsigned int)fg_color);
#else
		fg_col_ = (fg_color != Default) ? (ConsoleColor) ((unsigned int) fg_color + 30) : Default;
#endif
		bg_col_ = Default;
		intense_ = intensecolor;
	}

	ConsoleColor fg_col_;
	ConsoleColor bg_col_;
	bool intense_;
};


YAFRAYCORE_EXPORT std::ostream &operator<<(std::ostream &o, const SetColor &c);

END_YAFRAY

#endif
