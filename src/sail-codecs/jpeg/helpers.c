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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sail-common.h"

#include "helpers.h"

void jpeg_private_my_output_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);

    SAIL_LOG_ERROR("JPEG: %s", buffer);
}

void jpeg_private_my_error_exit(j_common_ptr cinfo) {
    struct jpeg_private_my_error_context *myerr = (struct jpeg_private_my_error_context *)cinfo->err;

    (*cinfo->err->output_message)(cinfo);

    longjmp(myerr->setjmp_buffer, 1);
}

enum SailPixelFormat jpeg_private_color_space_to_pixel_format(J_COLOR_SPACE color_space) {
    switch (color_space) {
        case JCS_GRAYSCALE: return SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE;
        case JCS_RGB:       return SAIL_PIXEL_FORMAT_BPP24_RGB;

#ifdef HAVE_JPEG_JCS_EXT
        case JCS_RGB565:    return SAIL_PIXEL_FORMAT_BPP16_RGB565;

        case JCS_EXT_RGB:   return SAIL_PIXEL_FORMAT_BPP24_RGB;
        case JCS_EXT_BGR:   return SAIL_PIXEL_FORMAT_BPP24_BGR;

        case JCS_EXT_RGBA:  return SAIL_PIXEL_FORMAT_BPP32_RGBA;
        case JCS_EXT_BGRA:  return SAIL_PIXEL_FORMAT_BPP32_BGRA;
        case JCS_EXT_ABGR:  return SAIL_PIXEL_FORMAT_BPP32_ABGR;
        case JCS_EXT_ARGB:  return SAIL_PIXEL_FORMAT_BPP32_ARGB;
#endif

        case JCS_YCbCr:     return SAIL_PIXEL_FORMAT_BPP24_YCBCR;
        case JCS_CMYK:      return SAIL_PIXEL_FORMAT_BPP32_CMYK;
        case JCS_YCCK:      return SAIL_PIXEL_FORMAT_BPP32_YCCK;

        default:            return SAIL_PIXEL_FORMAT_UNKNOWN;
    }
}

J_COLOR_SPACE jpeg_private_pixel_format_to_color_space(enum SailPixelFormat pixel_format) {
    switch (pixel_format) {
        case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE:  return JCS_GRAYSCALE;
        case SAIL_PIXEL_FORMAT_BPP24_RGB:       return JCS_RGB;

#ifdef HAVE_JPEG_JCS_EXT
        case SAIL_PIXEL_FORMAT_BPP16_RGB565:    return JCS_RGB565;
        case SAIL_PIXEL_FORMAT_BPP24_BGR:       return JCS_EXT_BGR;

        case SAIL_PIXEL_FORMAT_BPP32_RGBA:      return JCS_EXT_RGBA;
        case SAIL_PIXEL_FORMAT_BPP32_BGRA:      return JCS_EXT_BGRA;
        case SAIL_PIXEL_FORMAT_BPP32_ABGR:      return JCS_EXT_ABGR;
        case SAIL_PIXEL_FORMAT_BPP32_ARGB:      return JCS_EXT_ARGB;
#endif

        case SAIL_PIXEL_FORMAT_BPP24_YCBCR:     return JCS_YCbCr;
        case SAIL_PIXEL_FORMAT_BPP32_CMYK:      return JCS_CMYK;
        case SAIL_PIXEL_FORMAT_BPP32_YCCK:      return JCS_YCCK;

        default:                                return JCS_UNKNOWN;
    }
}

sail_status_t jpeg_private_auto_output_color_space(enum SailPixelFormat input_pixel_format, J_COLOR_SPACE *output_color_space) {

    SAIL_CHECK_PTR(output_color_space);

    switch (input_pixel_format) {

        case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE: *output_color_space = JCS_GRAYSCALE; return SAIL_OK;
        case SAIL_PIXEL_FORMAT_BPP24_RGB:
        case SAIL_PIXEL_FORMAT_BPP24_BGR:
        case SAIL_PIXEL_FORMAT_BPP32_RGBA:
        case SAIL_PIXEL_FORMAT_BPP32_BGRA:
        case SAIL_PIXEL_FORMAT_BPP32_ABGR:
        case SAIL_PIXEL_FORMAT_BPP32_ARGB:
        case SAIL_PIXEL_FORMAT_BPP24_YCBCR:    *output_color_space = JCS_YCbCr;     return SAIL_OK;
        case SAIL_PIXEL_FORMAT_BPP32_CMYK:     *output_color_space = JCS_CMYK;      return SAIL_OK;
        case SAIL_PIXEL_FORMAT_BPP32_YCCK:     *output_color_space = JCS_YCCK;      return SAIL_OK;

        default: SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_PIXEL_FORMAT);
    }
}

static void get_cmyk(unsigned char **pixels, unsigned char *C, unsigned char *M, unsigned char *Y, unsigned char *K) {

    *C = (unsigned char)(*(*pixels)++ / 100.0);
    *M = (unsigned char)(*(*pixels)++ / 100.0);
    *Y = (unsigned char)(*(*pixels)++ / 100.0);
    *K = (unsigned char)(*(*pixels)++ / 100.0);
}

sail_status_t jpeg_private_convert_cmyk(unsigned char *pixels_source, unsigned char *pixels_target, unsigned width, enum SailPixelFormat target_pixel_format) {
    unsigned char C, M, Y, K;

    switch (target_pixel_format) {
        case SAIL_PIXEL_FORMAT_BPP32_RGBA: {
            for (unsigned i = 0; i < width; i++) {
                get_cmyk(&pixels_source, &C, &M, &Y, &K);

                *pixels_target++ = 255 * (1-C) * (1-K);
                *pixels_target++ = 255 * (1-M) * (1-K);
                *pixels_target++ = 255 * (1-Y) * (1-K);
                *pixels_target++ = 255;
            }
            return SAIL_OK;
        }
        case SAIL_PIXEL_FORMAT_BPP32_BGRA: {
            for (unsigned i = 0; i < width; i++) {
                get_cmyk(&pixels_source, &C, &M, &Y, &K);

                *pixels_target++ = 255 * (1-Y) * (1-K);
                *pixels_target++ = 255 * (1-M) * (1-K);
                *pixels_target++ = 255 * (1-C) * (1-K);
                *pixels_target++ = 255;
            }
            return SAIL_OK;
        }
        case SAIL_PIXEL_FORMAT_BPP24_RGB: {
            for (unsigned i = 0; i < width; i++) {
                get_cmyk(&pixels_source, &C, &M, &Y, &K);

                *pixels_target++ = 255 * (1-C) * (1-K);
                *pixels_target++ = 255 * (1-M) * (1-K);
                *pixels_target++ = 255 * (1-Y) * (1-K);
            }
            return SAIL_OK;
        }
        case SAIL_PIXEL_FORMAT_BPP24_BGR: {
            for (unsigned i = 0; i < width; i++) {
                get_cmyk(&pixels_source, &C, &M, &Y, &K);

                *pixels_target++ = 255 * (1-Y) * (1-K);
                *pixels_target++ = 255 * (1-M) * (1-K);
                *pixels_target++ = 255 * (1-C) * (1-K);
            }
            return SAIL_OK;
        }

        default: {
            SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_PIXEL_FORMAT);
        }
    }
}

sail_status_t jpeg_private_fetch_meta_data(struct jpeg_decompress_struct *decompress_context, struct sail_meta_data_node **last_meta_data_node) {

    SAIL_CHECK_META_DATA_NODE_PTR(last_meta_data_node);

    jpeg_saved_marker_ptr it = decompress_context->marker_list;

    while(it != NULL) {
        if(it->marker == JPEG_COM) {
            struct sail_meta_data_node *meta_data_node;

            SAIL_TRY(sail_alloc_meta_data_node(&meta_data_node));
            meta_data_node->key = SAIL_META_DATA_COMMENT;
            SAIL_TRY_OR_CLEANUP(sail_strdup_length((const char *)it->data, it->data_length, &meta_data_node->value_string),
                                /* cleanup */ sail_destroy_meta_data_node(meta_data_node));

            *last_meta_data_node = meta_data_node;
            last_meta_data_node = &meta_data_node->next;
        }

        it = it->next;
    }

    return SAIL_OK;
}

sail_status_t jpeg_private_write_meta_data(struct jpeg_compress_struct *compress_context, const struct sail_meta_data_node *meta_data_node) {

    while (meta_data_node != NULL) {
        if (meta_data_node->value_type == SAIL_META_DATA_TYPE_STRING) {
            jpeg_write_marker(compress_context,
                                JPEG_COM,
                                (JOCTET *)meta_data_node->value_string,
                                (unsigned int)strlen(meta_data_node->value_string));
        } else {
            const char *meta_data_str = NULL;
            SAIL_TRY_OR_SUPPRESS(sail_meta_data_to_string(meta_data_node->key, &meta_data_str));

            SAIL_LOG_WARNING("JPEG: Ignoring unsupported binary key '%s'", meta_data_str);
        }

        meta_data_node = meta_data_node->next;
    }

    return SAIL_OK;
}

#ifdef HAVE_JPEG_ICCP
sail_status_t jpeg_private_fetch_iccp(struct jpeg_decompress_struct *decompress_context, struct sail_iccp **iccp) {

    SAIL_CHECK_ICCP_PTR(iccp);

    void *data = NULL;
    unsigned data_length = 0;

    SAIL_LOG_DEBUG("JPEG: ICC profile is %sfound",
                   jpeg_read_icc_profile(decompress_context,
                                         (JOCTET **)&data,
                                         &data_length)
                   ? "" : "not ");

    if (data != NULL && data_length > 0) {
        SAIL_TRY_OR_CLEANUP(sail_alloc_iccp_from_shallow_data(iccp, data, data_length),
                            /* cleanup */ sail_free(data));
    }

    return SAIL_OK;
}
#endif

sail_status_t jpeg_private_fetch_resolution(struct jpeg_decompress_struct *decompress_context, struct sail_resolution **resolution) {

    SAIL_CHECK_RESOLUTION_PTR(resolution);

    /* Resolution information is not valid. */
    if (decompress_context->X_density == 0 && decompress_context->Y_density == 0) {
        return SAIL_OK;
    }

    SAIL_TRY(sail_alloc_resolution(resolution));

    switch (decompress_context->density_unit) {
        case 1: {
            (*resolution)->unit = SAIL_RESOLUTION_UNIT_INCH;
            break;
        }
        case 2: {
            (*resolution)->unit = SAIL_RESOLUTION_UNIT_CENTIMETER;
            break;
        }
    }

    (*resolution)->x = (float)decompress_context->X_density;
    (*resolution)->y = (float)decompress_context->Y_density;

    return SAIL_OK;
}

sail_status_t jpeg_private_write_resolution(struct jpeg_compress_struct *compress_context, const struct sail_resolution *resolution) {

    /* Not an error. */
    if (resolution == NULL) {
        return SAIL_OK;
    }

    switch (resolution->unit) {
        case SAIL_RESOLUTION_UNIT_INCH: {
            compress_context->density_unit = 1;
            break;
        }
        case SAIL_RESOLUTION_UNIT_CENTIMETER: {
            compress_context->density_unit = 2;
            break;
        }
        default: {
            compress_context->density_unit = 0;
            break;
        }
    }

    compress_context->X_density = (UINT16)resolution->x;
    compress_context->Y_density = (UINT16)resolution->y;

    return SAIL_OK;
}
