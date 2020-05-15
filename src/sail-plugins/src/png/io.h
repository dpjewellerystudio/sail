/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2020 Dmitry Baryshev <dmitrymq@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef SAIL_PNG_IO_H
#define SAIL_PNG_IO_H

#include <stdio.h>

#include <png.h>

#include "export.h"

SAIL_HIDDEN void my_read_fn(png_structp png_ptr, png_bytep bytes, png_size_t bytes_size);

SAIL_HIDDEN void my_write_fn(png_structp png_ptr, png_bytep bytes, png_size_t bytes_size);

SAIL_HIDDEN void my_flush_fn(png_structp png_ptr);

#endif