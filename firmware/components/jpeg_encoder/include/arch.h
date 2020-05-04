/******************************************************************************
** This file is part of the jpegant project.
**
** Copyright (C) 2009-2013 Vladimir Antonenko
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 2 of the License,
** or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.
******************************************************************************/

#ifndef __ARCH_H__
#define __ARCH_H__

#ifdef __cplusplus
//extern "C" {
#endif

typedef unsigned char color;

#ifdef _MSC_VER
#	include <intrin.h>
#	include <emmintrin.h>
#	include <io.h>
#	define CACHE_ALIGN __declspec(align(32))
#else
#	include <stdint.h>
#	include <unistd.h>
#	define CACHE_ALIGN __attribute__ ((aligned(32)))
#	define O_BINARY	0
#endif

#ifdef __cplusplus
//}
#endif

#endif//__ARCH_H__
