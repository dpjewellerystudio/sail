/*  This file is part of SAIL (https://github.com/sailor-keg/sail)

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

#include "config.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

#include "common.h"
#include "error.h"
#include "export.h"
#include "log.h"
#include "meta_entry_node.h"
#include "utils.h"

/*
 * Plugin-specific data types.
 */
static const int COMPRESSION_MIN     = 0;
static const int COMPRESSION_MAX     = 100;
static const int COMPRESSION_DEFAULT = 15;

struct my_error_context {
    struct jpeg_error_mgr jpeg_error_mgr;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_context * my_error_context_ptr;

static void my_output_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);

    SAIL_LOG_ERROR("%s", buffer);
}

static void my_error_exit(j_common_ptr cinfo) {
    my_error_context_ptr myerr = (my_error_context_ptr)cinfo->err;

    (*cinfo->err->output_message)(cinfo);

    longjmp(myerr->setjmp_buffer, 1);
}

static int color_space_to_pixel_format(J_COLOR_SPACE color_space) {
    switch (color_space) {
        case JCS_GRAYSCALE: return SAIL_PIXEL_FORMAT_GRAYSCALE;

        case JCS_EXT_RGB:
        case JCS_RGB:       return SAIL_PIXEL_FORMAT_RGB;

        case JCS_YCbCr:     return SAIL_PIXEL_FORMAT_YCBCR;
        case JCS_CMYK:      return SAIL_PIXEL_FORMAT_CMYK;
        case JCS_YCCK:      return SAIL_PIXEL_FORMAT_YCCK;
        case JCS_EXT_RGBX:  return SAIL_PIXEL_FORMAT_RGBX;
        case JCS_EXT_BGR:   return SAIL_PIXEL_FORMAT_BGR;
        case JCS_EXT_BGRX:  return SAIL_PIXEL_FORMAT_BGRX;
        case JCS_EXT_XBGR:  return SAIL_PIXEL_FORMAT_XBGR;
        case JCS_EXT_XRGB:  return SAIL_PIXEL_FORMAT_XRGB;
        case JCS_EXT_RGBA:  return SAIL_PIXEL_FORMAT_RGBA;
        case JCS_EXT_BGRA:  return SAIL_PIXEL_FORMAT_BGRA;
        case JCS_EXT_ABGR:  return SAIL_PIXEL_FORMAT_ABGR;
        case JCS_EXT_ARGB:  return SAIL_PIXEL_FORMAT_ARGB;
        case JCS_RGB565:    return SAIL_PIXEL_FORMAT_RGB565;

        default:            return SAIL_PIXEL_FORMAT_UNKNOWN;
    }
}

static J_COLOR_SPACE pixel_format_to_color_space(int pixel_format) {
    switch (pixel_format) {
        case SAIL_PIXEL_FORMAT_GRAYSCALE: return JCS_GRAYSCALE;
        case SAIL_PIXEL_FORMAT_RGB:       return JCS_RGB;
        case SAIL_PIXEL_FORMAT_YCBCR:     return JCS_YCbCr;
        case SAIL_PIXEL_FORMAT_CMYK:      return JCS_CMYK;
        case SAIL_PIXEL_FORMAT_YCCK:      return JCS_YCCK;
        case SAIL_PIXEL_FORMAT_RGBX:      return JCS_EXT_RGBX;
        case SAIL_PIXEL_FORMAT_BGR:       return JCS_EXT_BGR;
        case SAIL_PIXEL_FORMAT_BGRX:      return JCS_EXT_BGRX;
        case SAIL_PIXEL_FORMAT_XBGR:      return JCS_EXT_XBGR;
        case SAIL_PIXEL_FORMAT_XRGB:      return JCS_EXT_XRGB;
        case SAIL_PIXEL_FORMAT_RGBA:      return JCS_EXT_RGBA;
        case SAIL_PIXEL_FORMAT_BGRA:      return JCS_EXT_BGRA;
        case SAIL_PIXEL_FORMAT_ABGR:      return JCS_EXT_ABGR;
        case SAIL_PIXEL_FORMAT_ARGB:      return JCS_EXT_ARGB;
        case SAIL_PIXEL_FORMAT_RGB565:    return JCS_RGB565;

        default:                          return JCS_UNKNOWN;
    }
}

/*
 * Plugin-specific PIMPL.
 */
struct pimpl {
    struct jpeg_decompress_struct decompress_context;
    struct jpeg_compress_struct compress_context;
    struct my_error_context error_context;
    JSAMPARRAY buffer;
    bool libjpeg_error;
    struct sail_read_options *read_options;
    struct sail_write_options *write_options;
    bool frame_read;
    bool frame_written;
};

static int alloc_pimpl(struct pimpl **pimpl) {

    *pimpl = (struct pimpl *)malloc(sizeof(struct pimpl));

    if (*pimpl == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*pimpl)->buffer        = NULL;
    (*pimpl)->libjpeg_error = false;
    (*pimpl)->read_options  = NULL;
    (*pimpl)->write_options = NULL;
    (*pimpl)->frame_read    = false;
    (*pimpl)->frame_written = false;

    return 0;
}

/*
 * Decoding functions.
 */
SAIL_EXPORT sail_error_t sail_plugin_read_features_v1(struct sail_read_features **read_features) {

    SAIL_TRY(sail_alloc_read_features(read_features));

    (*read_features)->input_pixel_formats_length = 5;
    (*read_features)->input_pixel_formats = (int *)malloc((*read_features)->input_pixel_formats_length * sizeof(int));

    if ((*read_features)->input_pixel_formats == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*read_features)->input_pixel_formats[0] = SAIL_PIXEL_FORMAT_GRAYSCALE;
    (*read_features)->input_pixel_formats[1] = SAIL_PIXEL_FORMAT_RGB;
    (*read_features)->input_pixel_formats[2] = SAIL_PIXEL_FORMAT_YCBCR;
    (*read_features)->input_pixel_formats[3] = SAIL_PIXEL_FORMAT_CMYK;
    (*read_features)->input_pixel_formats[4] = SAIL_PIXEL_FORMAT_YCCK;

    (*read_features)->output_pixel_formats_length = 15;
    (*read_features)->output_pixel_formats = (int *)malloc((*read_features)->output_pixel_formats_length * sizeof(int));

    if ((*read_features)->output_pixel_formats == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*read_features)->output_pixel_formats[0]  = SAIL_PIXEL_FORMAT_GRAYSCALE;
    (*read_features)->output_pixel_formats[1]  = SAIL_PIXEL_FORMAT_RGB;
    (*read_features)->output_pixel_formats[2]  = SAIL_PIXEL_FORMAT_YCBCR;
    (*read_features)->output_pixel_formats[3]  = SAIL_PIXEL_FORMAT_CMYK;
    (*read_features)->output_pixel_formats[4]  = SAIL_PIXEL_FORMAT_YCCK;
    (*read_features)->output_pixel_formats[5]  = SAIL_PIXEL_FORMAT_RGBX;
    (*read_features)->output_pixel_formats[6]  = SAIL_PIXEL_FORMAT_BGR;
    (*read_features)->output_pixel_formats[7]  = SAIL_PIXEL_FORMAT_BGRX;
    (*read_features)->output_pixel_formats[8]  = SAIL_PIXEL_FORMAT_XBGR;
    (*read_features)->output_pixel_formats[9]  = SAIL_PIXEL_FORMAT_XRGB;
    (*read_features)->output_pixel_formats[10] = SAIL_PIXEL_FORMAT_RGBA;
    (*read_features)->output_pixel_formats[11] = SAIL_PIXEL_FORMAT_BGRA;
    (*read_features)->output_pixel_formats[12] = SAIL_PIXEL_FORMAT_ABGR;
    (*read_features)->output_pixel_formats[13] = SAIL_PIXEL_FORMAT_ARGB;
    (*read_features)->output_pixel_formats[14] = SAIL_PIXEL_FORMAT_RGB565;

    (*read_features)->preferred_output_pixel_format = SAIL_PIXEL_FORMAT_RGB;

    (*read_features)->features = SAIL_PLUGIN_FEATURE_STATIC | SAIL_PLUGIN_FEATURE_META_INFO;

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_read_init_v1(struct sail_file *file, struct sail_read_options *read_options) {

    SAIL_CHECK_FILE(file);

    struct pimpl *pimpl;

    SAIL_TRY(alloc_pimpl(&pimpl));

    file->pimpl = pimpl;

    /* Construct default read options. */
    if (read_options == NULL) {
        /* Copy options from read features. */
        struct sail_read_features *read_features;

        SAIL_TRY(sail_plugin_read_features_v1(&read_features));
        SAIL_TRY_OR_CLEANUP(sail_alloc_read_options_from_features(read_features, &pimpl->read_options),
                            sail_destroy_read_features(read_features));

        sail_destroy_read_features(read_features);
    } else {
        pimpl->read_options = (struct sail_read_options *)malloc(sizeof(struct sail_read_options));

        if (pimpl->read_options == NULL) {
            return SAIL_MEMORY_ALLOCATION_FAILED;
        }

        memcpy(pimpl->read_options, read_options, sizeof(struct sail_read_options));
    }

    /* Error handling setup. */
    pimpl->decompress_context.err = jpeg_std_error(&pimpl->error_context.jpeg_error_mgr);
    pimpl->error_context.jpeg_error_mgr.error_exit = my_error_exit;
    pimpl->error_context.jpeg_error_mgr.output_message = my_output_message;

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    /* JPEG setup. */
    jpeg_create_decompress(&pimpl->decompress_context);
    jpeg_stdio_src(&pimpl->decompress_context, file->fptr);

    if (pimpl->read_options->io_options & SAIL_IO_OPTION_META_INFO) {
        jpeg_save_markers(&pimpl->decompress_context, JPEG_COM, 0xffff);
    }

    jpeg_read_header(&pimpl->decompress_context, true);

    if (pixel_format_to_color_space(pimpl->read_options->pixel_format) == JCS_UNKNOWN) {
        return SAIL_UNSUPPORTED_PIXEL_FORMAT;
    }

    /* Handle the requested color space. */
    J_COLOR_SPACE requested_color_space = pixel_format_to_color_space(pimpl->read_options->pixel_format);

    if (pimpl->read_options->pixel_format == SAIL_PIXEL_FORMAT_SOURCE) {
        pimpl->decompress_context.out_color_space = pimpl->decompress_context.jpeg_color_space;
    } else {
        pimpl->decompress_context.out_color_space = requested_color_space;
    }

    /* We don't want colormapped output. */
    pimpl->decompress_context.quantize_colors = false;

    /* Launch decompression! */
    jpeg_start_decompress(&pimpl->decompress_context);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_read_seek_next_frame_v1(struct sail_file *file, struct sail_image **image) {

    SAIL_CHECK_FILE(file);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    if (pimpl->frame_read) {
        return SAIL_NO_MORE_FRAMES;
    }

    pimpl->frame_read = true;
    SAIL_TRY(sail_alloc_image(image));

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    const int bytes_per_line = pimpl->decompress_context.output_width * pimpl->decompress_context.output_components;

    /* Buffer to put scan lines into. libjpeg will automatically free it. */
    pimpl->buffer = (*pimpl->decompress_context.mem->alloc_sarray)((j_common_ptr)&pimpl->decompress_context,
                                                                    JPOOL_IMAGE,
                                                                    bytes_per_line,
                                                                    1);

    if (pimpl->buffer == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    /* Image properties. */
    (*image)->width               = pimpl->decompress_context.output_width;
    (*image)->height              = pimpl->decompress_context.output_height;
    (*image)->bytes_per_line      = bytes_per_line;
    (*image)->pixel_format        = color_space_to_pixel_format(pimpl->decompress_context.out_color_space);
    (*image)->passes              = 1;
    (*image)->source_pixel_format = color_space_to_pixel_format(pimpl->decompress_context.jpeg_color_space);

    /* Read meta info. */
    if (pimpl->read_options->io_options & SAIL_IO_OPTION_META_INFO) {
        jpeg_saved_marker_ptr it = pimpl->decompress_context.marker_list;
        struct sail_meta_entry_node *last_meta_entry_node;

        while(it != NULL) {
            if(it->marker == JPEG_COM) {
                struct sail_meta_entry_node *meta_entry_node;

                SAIL_TRY(sail_alloc_meta_entry_node(&meta_entry_node));
                SAIL_TRY_OR_CLEANUP(sail_strdup("Comment", &meta_entry_node->key),
                                    /* cleanup */ sail_destroy_meta_entry_node(meta_entry_node));
                SAIL_TRY_OR_CLEANUP(sail_strdup_length((const char *)it->data, it->data_length, &meta_entry_node->value),
                                    /* cleanup */ sail_destroy_meta_entry_node(meta_entry_node));

                if ((*image)->meta_entry_node == NULL) {
                    (*image)->meta_entry_node = last_meta_entry_node = meta_entry_node;
                } else {
                    last_meta_entry_node->next = meta_entry_node;
                    last_meta_entry_node = meta_entry_node;
                }
            }

            it = it->next;
        }
    }

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_read_seek_next_pass_v1(struct sail_file *file, struct sail_image *image) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_read_scan_line_v1(struct sail_file *file, struct sail_image *image, void *scanline) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);
    SAIL_CHECK_SCAN_LINE_PTR(scanline);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    if (pimpl->libjpeg_error) {
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    const int color_components = pimpl->decompress_context.output_components;

    (void)jpeg_read_scanlines(&pimpl->decompress_context, pimpl->buffer, 1);

    memcpy(scanline, pimpl->buffer[0], image->width * color_components);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_read_scan_line_v2(struct sail_file *file, struct sail_image *image, void **scanline) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    const int color_components = pimpl->decompress_context.output_components;

    *scanline = malloc(image->width * color_components);

    if (*scanline == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    return sail_plugin_read_scan_line_v1(file, image, *scanline);
}

SAIL_EXPORT sail_error_t sail_plugin_read_finish_v1(struct sail_file *file) {

    SAIL_CHECK_FILE(file);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    if (pimpl == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    sail_destroy_read_options(pimpl->read_options);

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    jpeg_abort_decompress(&pimpl->decompress_context);
    jpeg_destroy_decompress(&pimpl->decompress_context);

    return 0;
}

/*
 * Encoding functions.
 */

SAIL_EXPORT sail_error_t sail_plugin_write_features_v1(struct sail_write_features **write_features) {

    SAIL_TRY(sail_alloc_write_features(write_features));

    (*write_features)->input_pixel_formats_length = 15;
    (*write_features)->input_pixel_formats = (int *)malloc((*write_features)->input_pixel_formats_length * sizeof(int));

    if ((*write_features)->input_pixel_formats == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*write_features)->input_pixel_formats[0]  = SAIL_PIXEL_FORMAT_GRAYSCALE;
    (*write_features)->input_pixel_formats[1]  = SAIL_PIXEL_FORMAT_RGB;
    (*write_features)->input_pixel_formats[2]  = SAIL_PIXEL_FORMAT_YCBCR;
    (*write_features)->input_pixel_formats[3]  = SAIL_PIXEL_FORMAT_CMYK;
    (*write_features)->input_pixel_formats[4]  = SAIL_PIXEL_FORMAT_YCCK;
    (*write_features)->input_pixel_formats[5]  = SAIL_PIXEL_FORMAT_RGBX;
    (*write_features)->input_pixel_formats[6]  = SAIL_PIXEL_FORMAT_BGR;
    (*write_features)->input_pixel_formats[7]  = SAIL_PIXEL_FORMAT_BGRX;
    (*write_features)->input_pixel_formats[8]  = SAIL_PIXEL_FORMAT_XBGR;
    (*write_features)->input_pixel_formats[9]  = SAIL_PIXEL_FORMAT_XRGB;
    (*write_features)->input_pixel_formats[10] = SAIL_PIXEL_FORMAT_RGBA;
    (*write_features)->input_pixel_formats[11] = SAIL_PIXEL_FORMAT_BGRA;
    (*write_features)->input_pixel_formats[12] = SAIL_PIXEL_FORMAT_ABGR;
    (*write_features)->input_pixel_formats[13] = SAIL_PIXEL_FORMAT_ARGB;
    (*write_features)->input_pixel_formats[14] = SAIL_PIXEL_FORMAT_RGB565;

    (*write_features)->output_pixel_formats_length = 5;
    (*write_features)->output_pixel_formats = (int *)malloc((*write_features)->output_pixel_formats_length * sizeof(int));

    if ((*write_features)->output_pixel_formats == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*write_features)->output_pixel_formats[0] = SAIL_PIXEL_FORMAT_GRAYSCALE;
    (*write_features)->output_pixel_formats[1] = SAIL_PIXEL_FORMAT_RGB;
    (*write_features)->output_pixel_formats[2] = SAIL_PIXEL_FORMAT_YCBCR;
    (*write_features)->output_pixel_formats[3] = SAIL_PIXEL_FORMAT_CMYK;
    (*write_features)->output_pixel_formats[4] = SAIL_PIXEL_FORMAT_YCCK;

    (*write_features)->preferred_output_pixel_format = SAIL_PIXEL_FORMAT_YCBCR;

    (*write_features)->features                   = SAIL_PLUGIN_FEATURE_STATIC | SAIL_PLUGIN_FEATURE_META_INFO;
    (*write_features)->properties                 = 0;
    (*write_features)->passes                     = 0;
    (*write_features)->compression_types          = NULL;
    (*write_features)->compression_types_length   = 0;
    (*write_features)->preferred_compression_type = 0;
    (*write_features)->compression_min            = COMPRESSION_MIN;
    (*write_features)->compression_max            = COMPRESSION_MAX;
    (*write_features)->compression_default        = COMPRESSION_DEFAULT;

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_write_init_v1(struct sail_file *file, struct sail_write_options *write_options) {

    SAIL_CHECK_FILE(file);

    struct pimpl *pimpl;

    SAIL_TRY(alloc_pimpl(&pimpl));

    file->pimpl = pimpl;

    /* Construct default write options. */
    if (write_options == NULL) {
        /* Copy options from write features. */
        struct sail_write_features *write_features;

        SAIL_TRY(sail_plugin_write_features_v1(&write_features));
        SAIL_TRY_OR_CLEANUP(sail_alloc_write_options_from_features(write_features, &pimpl->write_options),
                            sail_destroy_write_features(write_features));

        sail_destroy_write_features(write_features);
    } else {
        pimpl->write_options = (struct sail_write_options *)malloc(sizeof(struct sail_write_options));

        if (pimpl->write_options == NULL) {
            return SAIL_MEMORY_ALLOCATION_FAILED;
        }

        memcpy(pimpl->write_options, write_options, sizeof(struct sail_write_options));
    }

    /* Sanity check. */
    if (pixel_format_to_color_space(pimpl->write_options->pixel_format) == JCS_UNKNOWN) {
        return SAIL_UNSUPPORTED_PIXEL_FORMAT;
    }

    if (pimpl->write_options->compression_type != 0) {
        return SAIL_UNSUPPORTED_COMPRESSION_TYPE;
    }

    /* Error handling setup. */
    pimpl->compress_context.err = jpeg_std_error(&pimpl->error_context.jpeg_error_mgr);
    pimpl->error_context.jpeg_error_mgr.error_exit = my_error_exit;
    pimpl->error_context.jpeg_error_mgr.output_message = my_output_message;

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    /* JPEG setup. */
    jpeg_create_compress(&pimpl->compress_context);
    jpeg_stdio_dest(&pimpl->compress_context, file->fptr);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_write_seek_next_frame_v1(struct sail_file *file, struct sail_image *image) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);

    /* Sanity check. */
    if (pixel_format_to_color_space(image->pixel_format) == JCS_UNKNOWN) {
        return SAIL_UNSUPPORTED_PIXEL_FORMAT;
    }

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    if (pimpl->frame_written) {
        return SAIL_NO_MORE_FRAMES;
    }

    pimpl->frame_written = true;

    /* Error handling setup. */
    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    pimpl->compress_context.image_width = image->width;
    pimpl->compress_context.image_height = image->height;
    pimpl->compress_context.input_components = sail_bits_per_pixel(image->pixel_format) / 8;
    pimpl->compress_context.in_color_space = pixel_format_to_color_space(image->pixel_format);

    jpeg_set_defaults(&pimpl->compress_context);
    jpeg_set_colorspace(&pimpl->compress_context, pixel_format_to_color_space(pimpl->write_options->pixel_format));

    const int compression = (pimpl->write_options->compression < COMPRESSION_MIN ||
                                pimpl->write_options->compression > COMPRESSION_MAX)
                            ? COMPRESSION_DEFAULT
                            : pimpl->write_options->compression;
    jpeg_set_quality(&pimpl->compress_context, /* to quality */COMPRESSION_MAX-compression, true);

    jpeg_start_compress(&pimpl->compress_context, true);

    /* Write meta info. */
    if (pimpl->write_options->io_options & SAIL_IO_OPTION_META_INFO && image->meta_entry_node != NULL) {

        struct sail_meta_entry_node *meta_entry_node = image->meta_entry_node;

        while (meta_entry_node != NULL) {
            jpeg_write_marker(&pimpl->compress_context,
                                JPEG_COM,
                                (JOCTET *)meta_entry_node->value,
                                (unsigned int)strlen(meta_entry_node->value));

            meta_entry_node = meta_entry_node->next;
        }
    }

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_write_seek_next_pass_v1(struct sail_file *file, struct sail_image *image) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_write_scan_line_v1(struct sail_file *file, struct sail_image *image, void *scanline) {

    SAIL_CHECK_FILE(file);
    SAIL_CHECK_IMAGE(image);
    SAIL_CHECK_SCAN_LINE_PTR(scanline);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    if (pimpl->libjpeg_error) {
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    JSAMPROW row = (JSAMPROW)scanline;

    jpeg_write_scanlines(&pimpl->compress_context, &row, 1);

    return 0;
}

SAIL_EXPORT sail_error_t sail_plugin_write_finish_v1(struct sail_file *file) {

    SAIL_CHECK_FILE(file);

    struct pimpl *pimpl = (struct pimpl *)file->pimpl;

    SAIL_CHECK_PIMPL_PTR(pimpl);

    sail_destroy_write_options(pimpl->write_options);

    if (setjmp(pimpl->error_context.setjmp_buffer) != 0) {
        pimpl->libjpeg_error = true;
        return SAIL_UNDERLYING_CODEC_ERROR;
    }

    jpeg_finish_compress(&pimpl->compress_context);
    jpeg_destroy_compress(&pimpl->compress_context);

    return 0;
}
