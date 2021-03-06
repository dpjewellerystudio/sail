/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2020 Dmitry Baryshev

    The MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef SAIL_READ_FEATURES_CPP_H
#define SAIL_READ_FEATURES_CPP_H

#include <vector>

#ifdef SAIL_BUILD
    #include "error.h"
    #include "export.h"
#else
    #include <sail-common/error.h>
    #include <sail-common/export.h>
#endif

struct sail_read_features;

namespace sail
{

class read_options;

/*
 * A C++ interface to struct sail_read_features.
 */
class SAIL_EXPORT read_features
{
    friend class codec_info;

public:
    read_features(const read_features &rf);
    read_features& operator=(const read_features &rf);
    read_features(read_features &&rf) noexcept;
    read_features& operator=(read_features &&rf);
    ~read_features();

    const std::vector<SailPixelFormat>& output_pixel_formats() const;
    SailPixelFormat default_output_pixel_format() const;
    int features() const;

    sail_status_t to_read_options(read_options *sread_options) const;

private:
    read_features();
    /*
     * Makes a deep copy of the specified read features and stores the pointer for further use.
     * When the SAIL context gets uninitialized, the pointer becomes dangling.
     */
    read_features(const sail_read_features *rf);

    read_features& with_output_pixel_formats(const std::vector<SailPixelFormat> &output_pixel_formats);
    read_features& with_default_output_pixel_format(SailPixelFormat default_output_pixel_format);
    read_features& with_features(int features);

    const sail_read_features* sail_read_features_c() const;

private:
    class pimpl;
    pimpl *d;
};

}

#endif
