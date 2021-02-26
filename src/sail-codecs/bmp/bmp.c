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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sail-common.h"

#include "helpers.h"
#include "io.h"

/*  Compression types.  */
static const uint32_t SAIL_BI_RGB            = 0;
static const uint32_t SAIL_BI_RLE8           = 1;
static const uint32_t SAIL_BI_RLE4           = 2;
static const uint32_t SAIL_BI_BITFIELDS      = 3;
static const uint32_t SAIL_BI_JPEG           = 4;
static const uint32_t SAIL_BI_PNG            = 5;
static const uint32_t SAIL_BI_ALPHABITFIELDS = 6;
static const uint32_t SAIL_BI_CMYK           = 11;
static const uint32_t SAIL_BI_CMYKRLE8       = 12;
static const uint32_t SAIL_BI_CMYKRLE4       = 13;

/* BMP identifiers. */
static const uint16_t SAIL_DDB_IDENTIFIER = 0x02;
static const uint16_t SAIL_DIB_IDENTIFIER = 0x4D42;

/* Sizes of DIB header structs. */
#define SAIL_BITMAP_DIB_HEADER_V2_SIZE 12
#define SAIL_BITMAP_DIB_HEADER_V3_SIZE 40
#define SAIL_BITMAP_DIB_HEADER_V4_SIZE 108
#define SAIL_BITMAP_DIB_HEADER_V5_SIZE 124

/*
 * V1: Device-Dependent Bitmap (DDB).
 */

/* File header. */
struct SailBmpDdbFileHeader
{
    uint16_t type; /* Always 2. Top bit set if discardable. */
};

/* Bitmap16. */
struct SailBmpDdbBitmap
{
    uint16_t type; /* Always 0. */
    uint16_t width;
    uint16_t height;
    uint16_t byte_width;
    uint8_t  planes; /* Always 1. */
    uint8_t  bit_count;
    uint32_t pixels; /* Always 0. */
};

/*
 * V2+: File header + DIB headers.
 */

/* File header. */
struct SailBmpDibFileHeader
{
    uint16_t type; /* "BM" */
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
};

/* DIB headers. */
struct SailBmpDibHeaderV2
{
    uint32_t size;
    int32_t  width;
    int32_t  height;
    uint16_t planes; /* Always 1. */
    uint16_t bit_count;
};

struct SailBmpDibHeaderV3
{
    uint32_t compression;
    uint32_t bitmap_size;
    int32_t  x_pixels_per_meter;
    int32_t  y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};

SAIL_HIDDEN sail_status_t bmp_private_read_dib_file_header(struct sail_io *io, struct SailBmpDibFileHeader *fh) {

    SAIL_TRY(io->strict_read(io->stream, &fh->type,      sizeof(fh->type)));
    SAIL_TRY(io->strict_read(io->stream, &fh->size,      sizeof(fh->size)));
    SAIL_TRY(io->strict_read(io->stream, &fh->reserved1, sizeof(fh->reserved1)));
    SAIL_TRY(io->strict_read(io->stream, &fh->reserved2, sizeof(fh->reserved2)));
    SAIL_TRY(io->strict_read(io->stream, &fh->offset,    sizeof(fh->offset)));

    return SAIL_OK;
}

SAIL_HIDDEN sail_status_t bmp_private_read_v2(struct sail_io *io, struct SailBmpDibHeaderV2 *v2) {

    SAIL_TRY(io->strict_read(io->stream, &v2->size,      sizeof(v2->size)));
    SAIL_TRY(io->strict_read(io->stream, &v2->width,     sizeof(v2->width)));
    SAIL_TRY(io->strict_read(io->stream, &v2->height,    sizeof(v2->height)));
    SAIL_TRY(io->strict_read(io->stream, &v2->planes,    sizeof(v2->planes)));
    SAIL_TRY(io->strict_read(io->stream, &v2->bit_count, sizeof(v2->bit_count)));

    return SAIL_OK;
}

SAIL_HIDDEN sail_status_t bmp_private_read_v3(struct sail_io *io, struct SailBmpDibHeaderV3 *v3) {

    SAIL_TRY(io->strict_read(io->stream, &v3->compression,        sizeof(v3->compression)));
    SAIL_TRY(io->strict_read(io->stream, &v3->bitmap_size,        sizeof(v3->bitmap_size)));
    SAIL_TRY(io->strict_read(io->stream, &v3->x_pixels_per_meter, sizeof(v3->x_pixels_per_meter)));
    SAIL_TRY(io->strict_read(io->stream, &v3->y_pixels_per_meter, sizeof(v3->y_pixels_per_meter)));
    SAIL_TRY(io->strict_read(io->stream, &v3->colors_used,        sizeof(v3->colors_used)));
    SAIL_TRY(io->strict_read(io->stream, &v3->colors_important,   sizeof(v3->colors_important)));

    return SAIL_OK;
}

SAIL_HIDDEN sail_status_t bmp_private_bit_count_to_pixel_format(uint16_t bit_count, enum SailPixelFormat *pixel_format) {

    switch (bit_count) {
        case 1: {
            *pixel_format = SAIL_PIXEL_FORMAT_BPP1_INDEXED;
            return SAIL_OK;
        }
        case 4: {
            *pixel_format = SAIL_PIXEL_FORMAT_BPP4_INDEXED;
            return SAIL_OK;
        }
        case 8: {
            *pixel_format = SAIL_PIXEL_FORMAT_BPP8_INDEXED;
            return SAIL_OK;
        }
        case 16: {
            // TODO 555, 565 etc.
            *pixel_format = SAIL_PIXEL_FORMAT_BPP16_GRAYSCALE;
            return SAIL_OK;
        }
        case 24: {
            *pixel_format = SAIL_PIXEL_FORMAT_BPP24_RGB;
            return SAIL_OK;
        }
        case 32: {
            *pixel_format = SAIL_PIXEL_FORMAT_BPP32_RGBA;
            return SAIL_OK;
        }
    }

    return SAIL_ERROR_UNSUPPORTED_PIXEL_FORMAT;
}

struct SailBmpDibHeaderV4
{
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;
    uint32_t color_space_type;
    int32_t  red_x;
    int32_t  red_y;
    int32_t  red_z;
    int32_t  green_x;
    int32_t  green_y;
    int32_t  green_z;
    int32_t  blue_x;
    int32_t  blue_y;
    int32_t  blue_z;
    uint32_t gamma_red;
    uint32_t gamma_green;
    uint32_t gamma_blue;
};

struct SailBmpDibHeaderV5
{
    uint32_t intent;
    uint32_t profile_data;
    uint32_t profile_size;
    uint32_t reserved;
};

enum SailBmpVersion
{
    SAIL_BMP_V1,
    SAIL_BMP_V2,
    SAIL_BMP_V3,
    SAIL_BMP_V4,
    SAIL_BMP_V5,
};

/*
 * Codec-specific state.
 */
struct bmp_state {
    struct sail_read_options *read_options;
    struct sail_write_options *write_options;

    enum SailPixelFormat source_pixel_format;

    enum SailBmpVersion version;

    struct SailBmpDdbFileHeader ddb_file_header;
    struct SailBmpDdbBitmap v1;

    struct SailBmpDibFileHeader dib_file_header;
    struct SailBmpDibHeaderV2 v2;
    struct SailBmpDibHeaderV3 v3;
    struct SailBmpDibHeaderV4 v4;
    struct SailBmpDibHeaderV5 v5;

    sail_rgba_t *palette;
    int palette_count;
    /* Number of bytes to pad scan lines to 4-byte boundary. */
    unsigned pad_bytes;

    bool frame_read;
    bool frame_written;
};

static sail_status_t alloc_bmp_state(struct bmp_state **bmp_state) {

    void *ptr;
    SAIL_TRY(sail_malloc(sizeof(struct bmp_state), &ptr));
    *bmp_state = ptr;

    if (*bmp_state == NULL) {
        SAIL_LOG_AND_RETURN(SAIL_ERROR_MEMORY_ALLOCATION);
    }

    (*bmp_state)->read_options  = NULL;
    (*bmp_state)->write_options = NULL;

#if 0
    (*bmp_state)->ddb_file_header = NULL;
    (*bmp_state)->v1              = NULL;
    (*bmp_state)->dib_file_header = NULL;
    (*bmp_state)->v2              = NULL;
    (*bmp_state)->v3              = NULL;
    (*bmp_state)->v4              = NULL;
    (*bmp_state)->v5              = NULL;
#endif

    (*bmp_state)->palette         = NULL;
    (*bmp_state)->palette_count   = 0;
    (*bmp_state)->pad_bytes       = 0;

    (*bmp_state)->frame_read    = false;
    (*bmp_state)->frame_written = false;

    return SAIL_OK;
}

static void destroy_bmp_state(struct bmp_state *bmp_state) {

    if (bmp_state == NULL) {
        return;
    }

    sail_destroy_read_options(bmp_state->read_options);
    sail_destroy_write_options(bmp_state->write_options);

#if 0
    sail_free(bmp_state->ddb_file_header);
    sail_free(bmp_state->v1);
    sail_free(bmp_state->dib_file_header);
    sail_free(bmp_state->v2);
    sail_free(bmp_state->v3);
    sail_free(bmp_state->v4);
    sail_free(bmp_state->v5);
#endif

    sail_free(bmp_state->palette);

    sail_free(bmp_state);
}

/*
 * Decoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_read_init_v4_bmp(struct sail_io *io, const struct sail_read_options *read_options, void **state) {

    SAIL_CHECK_STATE_PTR(state);
    *state = NULL;

    SAIL_CHECK_IO(io);
    SAIL_CHECK_READ_OPTIONS_PTR(read_options);

    SAIL_TRY(bmp_private_supported_read_output_pixel_format(read_options->output_pixel_format));

    /* Allocate a new state. */
    struct bmp_state *bmp_state;
    SAIL_TRY(alloc_bmp_state(&bmp_state));
    *state = bmp_state;

    /* Deep copy read options. */
    SAIL_TRY(sail_copy_read_options(read_options, &bmp_state->read_options));

    /* "BM" or 0x02. */
    uint16_t magic;
    SAIL_TRY(io->strict_read(io->stream, &magic, sizeof(magic)));
    SAIL_TRY(io->seek(io->stream, 0, SEEK_SET));

    if (magic == SAIL_DDB_IDENTIFIER) {
        bmp_state->version = SAIL_BMP_V1;

        SAIL_TRY(io->strict_read(io->stream, &bmp_state->ddb_file_header, sizeof(bmp_state->ddb_file_header)));
        SAIL_TRY(io->strict_read(io->stream, &bmp_state->v1, sizeof(bmp_state->v1)));

        // TODO Support DDB
        SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_FORMAT);
    } else if (magic == SAIL_DIB_IDENTIFIER) {
        SAIL_TRY(bmp_private_read_dib_file_header(io, &bmp_state->dib_file_header));
        SAIL_TRY(bmp_private_read_v2(io, &bmp_state->v2));

        switch (bmp_state->v2.size) {
            case SAIL_BITMAP_DIB_HEADER_V2_SIZE: {
                bmp_state->version = SAIL_BMP_V2;
                break;
            }
            case SAIL_BITMAP_DIB_HEADER_V3_SIZE: {
                SAIL_TRY(bmp_private_read_v3(io, &bmp_state->v3));
                bmp_state->version = SAIL_BMP_V3;
                break;
            }
            case SAIL_BITMAP_DIB_HEADER_V4_SIZE: {
                SAIL_TRY(bmp_private_read_v3(io, &bmp_state->v3));
                // TODO
                //SAIL_TRY(bmp_private_read_v4(io, &bmp_state->v4));
                bmp_state->version = SAIL_BMP_V4;
                break;
            }
            case SAIL_BITMAP_DIB_HEADER_V5_SIZE: {
                SAIL_TRY(bmp_private_read_v3(io, &bmp_state->v3));
                // TODO
                //SAIL_TRY(bmp_private_read_v4(io, &bmp_state->v4));
                //SAIL_TRY(bmp_private_read_v5(io, &bmp_state->v5));
                bmp_state->version = SAIL_BMP_V5;
                break;
            }
            default: {
                SAIL_LOG_ERROR("BMP: Unsupported file header size %u", bmp_state->dib_file_header.size);
                SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_FORMAT);
            }
        }
    } else {
        SAIL_LOG_ERROR("BMP: 0x%x is not a valid magic number", magic);
        SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_FORMAT);
    }

    /* Check BMP restrictions. */
    if (bmp_state->version >= SAIL_BMP_V3) {
        if ((bmp_state->v2.bit_count == 16 || bmp_state->v2.bit_count == 32) && bmp_state->v3.compression != SAIL_BI_BITFIELDS) {
            return SAIL_ERROR_UNSUPPORTED_COMPRESSION;
        }
    }

    if (bmp_state->v2.bit_count != 16 && bmp_state->v3.compression != SAIL_BI_RGB) {
        return SAIL_ERROR_UNSUPPORTED_PIXEL_FORMAT;
    }

    SAIL_TRY(bmp_private_bit_count_to_pixel_format(bmp_state->v2.bit_count, &bmp_state->source_pixel_format));

    /*  Read palette.  */
    if (bmp_state->v2.bit_count < 16) {
        bmp_state->palette_count = 1 << bmp_state->v2.bit_count;

        void *ptr;
        SAIL_TRY(sail_malloc(sizeof(sail_rgba_t) * bmp_state->palette_count, &ptr));
        bmp_state->palette = ptr;

        sail_rgba_t rgba;

        for (int i = 0; i < bmp_state->palette_count; i++) {
            SAIL_TRY(sail_read_sail_pixel4_uint8(io, &rgba));

            bmp_state->palette[i].component1 = rgba.component3;
            bmp_state->palette[i].component2 = rgba.component2;
            bmp_state->palette[i].component3 = rgba.component1;
            bmp_state->palette[i].component4 = 255;
        }
    }

    /* Calculate the number of pad bytes to align scan lines to 4-byte boundary. */
    unsigned bytes_in_row = 0;

    switch (bmp_state->v2.bit_count) {
        case 1: {
            bytes_in_row = (bmp_state->v2.width + 7) / 8;
            break;
        }
        case 4: {
            bytes_in_row = (bmp_state->v2.width + 3) / 4;
            break;
        }
        case 8: {
            bytes_in_row = bmp_state->v2.width;
            break;
        }
        case 16: {
            bytes_in_row = bmp_state->v2.width * 2;
            break;
        }
        case 24: {
            bytes_in_row = bmp_state->v2.width * 3;
            break;
        }
        case 32: {
            bytes_in_row = bmp_state->v2.width * 4;
            break;
        }
        default: {
            SAIL_LOG_AND_RETURN(SAIL_ERROR_UNSUPPORTED_FORMAT);
        }
    }

    unsigned remainder = bytes_in_row % 4;
    bmp_state->pad_bytes = (remainder == 0) ? 0 : (4 - remainder);

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_seek_next_frame_v4_bmp(void *state, struct sail_io *io, struct sail_image **image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE_PTR(image);

    struct bmp_state *bmp_state = (struct bmp_state *)state;

    if (bmp_state->frame_read) {
        SAIL_LOG_AND_RETURN(SAIL_ERROR_NO_MORE_FRAMES);
    }

    bmp_state->frame_read = true;

    SAIL_TRY(sail_alloc_image(image));
    SAIL_TRY_OR_CLEANUP(sail_alloc_source_image(&(*image)->source_image),
                        /* cleanup */ sail_destroy_image(*image));

    (*image)->source_image->compression = SAIL_COMPRESSION_NONE;
    (*image)->source_image->pixel_format = bmp_state->source_pixel_format;
    (*image)->width = bmp_state->v2.width;
    (*image)->height = bmp_state->v2.height;
    (*image)->properties = SAIL_IMAGE_PROPERTY_FLIPPED_VERTICALLY;

    if (bmp_state->read_options->output_pixel_format == SAIL_PIXEL_FORMAT_BPP32_RGBA) {
        (*image)->pixel_format = SAIL_PIXEL_FORMAT_BPP32_RGBA;
    } else if (bmp_state->read_options->output_pixel_format == SAIL_PIXEL_FORMAT_BPP32_BGRA) {
        (*image)->pixel_format = SAIL_PIXEL_FORMAT_BPP32_BGRA;
    }
    SAIL_TRY_OR_CLEANUP(sail_bytes_per_line((*image)->width, (*image)->pixel_format, &(*image)->bytes_per_line),
                        /* cleanup */ sail_destroy_image(*image));

    SAIL_TRY_OR_CLEANUP(io->seek(io->stream, bmp_state->dib_file_header.offset, SEEK_SET),
                        /* cleanup */ sail_destroy_image(*image));

    const char *pixel_format_str = NULL;
    SAIL_TRY_OR_SUPPRESS(sail_pixel_format_to_string((*image)->source_image->pixel_format, &pixel_format_str));
    SAIL_LOG_DEBUG("BMP: Input pixel format is %s", pixel_format_str);
    SAIL_TRY_OR_SUPPRESS(sail_pixel_format_to_string(bmp_state->read_options->output_pixel_format, &pixel_format_str));
    SAIL_LOG_DEBUG("BMP: Output pixel format is %s", pixel_format_str);

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_seek_next_pass_v4_bmp(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE(image);

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_frame_v4_bmp(void *state, struct sail_io *io, struct sail_image *image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE(image);

    struct bmp_state *bmp_state = (struct bmp_state *)state;

    switch (bmp_state->v2.bit_count) {
        case 24: {
            uint8_t rgb[3];

            for (unsigned i = 0; i < image->height; i++) {
                unsigned char *scan = (unsigned char *)image->pixels + image->width * i * 4;

                for (unsigned j = 0; j < image->width; j++) {
                    SAIL_TRY(io->strict_read(io->stream, rgb, sizeof(rgb)));

                    *scan++ = rgb[2];
                    *scan++ = rgb[1];
                    *scan++ = rgb[0];
                    *scan++ = 255;
                }

                for (unsigned j = 0; j < bmp_state->pad_bytes; j++) {
                    char byte;
                    SAIL_TRY(io->strict_read(io->stream, &byte, sizeof(byte)));
                }
            }
            break;
        }
        case 32:
        {
        }
    }

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_finish_v4_bmp(void **state, struct sail_io *io) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);

    struct bmp_state *bmp_state = (struct bmp_state *)(*state);

    /* Subsequent calls to finish() will expectedly fail in the above line. */
    *state = NULL;

    destroy_bmp_state(bmp_state);

    return SAIL_OK;
}

/*
 * Encoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_write_init_v4_bmp(struct sail_io *io, const struct sail_write_options *write_options, void **state) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_WRITE_OPTIONS_PTR(write_options);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_seek_next_frame_v4_bmp(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE(image);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_seek_next_pass_v4_bmp(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE(image);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_frame_v4_bmp(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);
    SAIL_CHECK_IMAGE(image);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_finish_v4_bmp(void **state, struct sail_io *io) {

    SAIL_CHECK_STATE_PTR(state);
    SAIL_CHECK_IO(io);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}
