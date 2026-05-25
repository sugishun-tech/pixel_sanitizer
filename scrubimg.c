#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <png.h>
#include <jpeglib.h>

#define MAGIC_LEN 8

static void die(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static void die_errno(const char *msg) {
    fprintf(stderr, "error: %s: %s\n", msg, strerror(errno));
    exit(1);
}

static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), m = strlen(ext);
    if (n < m) return 0;
    const char *p = path + n - m;
    for (size_t i = 0; i < m; i++) {
        char a = p[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int is_png_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) die_errno("cannot open input");

    unsigned char sig[MAGIC_LEN];
    size_t n = fread(sig, 1, MAGIC_LEN, fp);
    fclose(fp);

    return n == MAGIC_LEN && png_sig_cmp(sig, 0, MAGIC_LEN) == 0;
}

static int is_jpeg_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) die_errno("cannot open input");

    unsigned char sig[3];
    size_t n = fread(sig, 1, 3, fp);
    fclose(fp);

    return n == 3 && sig[0] == 0xFF && sig[1] == 0xD8 && sig[2] == 0xFF;
}

static void scrub_png(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "rb");
    if (!in) die_errno("cannot open input PNG");

    png_structp rpng = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!rpng) die("png_create_read_struct failed");

    png_infop rinfo = png_create_info_struct(rpng);
    if (!rinfo) die("png_create_info_struct failed");

    if (setjmp(png_jmpbuf(rpng))) {
        png_destroy_read_struct(&rpng, &rinfo, NULL);
        fclose(in);
        die("failed while reading PNG");
    }

    png_init_io(rpng, in);
    png_read_info(rpng, rinfo);

    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type, compression_type, filter_method;
    png_get_IHDR(rpng, rinfo, &width, &height, &bit_depth, &color_type,
                 &interlace_type, &compression_type, &filter_method);

    if (bit_depth == 16) png_set_strip_16(rpng);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(rpng);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(rpng);
    if (png_get_valid(rpng, rinfo, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(rpng);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(rpng);

    /* Do not apply gAMA/sRGB/iCCP/cHRM/etc.  Pixels are copied after structural normalization only. */
    png_read_update_info(rpng, rinfo);

    int channels = png_get_channels(rpng, rinfo);
    bit_depth = png_get_bit_depth(rpng, rinfo);
    color_type = png_get_color_type(rpng, rinfo);

    if (bit_depth != 8) die("unsupported PNG bit depth after conversion");
    if (!(channels == 3 || channels == 4)) die("unsupported PNG channel count after conversion");

    png_size_t rowbytes = png_get_rowbytes(rpng, rinfo);
    png_bytep *rows = calloc(height, sizeof(*rows));
    if (!rows) die_errno("calloc rows failed");

    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = malloc(rowbytes);
        if (!rows[y]) die_errno("malloc row failed");
    }

    png_read_image(rpng, rows);
    png_read_end(rpng, NULL);

    png_destroy_read_struct(&rpng, &rinfo, NULL);
    fclose(in);

    FILE *out = fopen(out_path, "wb");
    if (!out) die_errno("cannot open output PNG");

    png_structp wpng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!wpng) die("png_create_write_struct failed");

    png_infop winfo = png_create_info_struct(wpng);
    if (!winfo) die("png_create_info_struct failed");

    if (setjmp(png_jmpbuf(wpng))) {
        png_destroy_write_struct(&wpng, &winfo);
        fclose(out);
        die("failed while writing PNG");
    }

    png_init_io(wpng, out);

    int out_color = channels == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    png_set_IHDR(wpng, winfo, width, height, 8, out_color,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    /* Intentionally no text, time, EXIF, ICC, gamma, sRGB, cHRM, pHYs, bKGD, or private chunks. */
    png_write_info(wpng, winfo);
    png_write_image(wpng, rows);
    png_write_end(wpng, NULL);

    png_destroy_write_struct(&wpng, &winfo);
    fclose(out);

    for (png_uint_32 y = 0; y < height; y++) free(rows[y]);
    free(rows);
}

static void scrub_jpeg(const char *in_path, const char *out_path, int quality) {
    FILE *in = fopen(in_path, "rb");
    if (!in) die_errno("cannot open input JPEG");

    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    jpeg_stdio_src(&dinfo, in);

    /* No jpeg_save_markers call: EXIF/ICC/XMP/comment markers are discarded. */
    jpeg_read_header(&dinfo, TRUE);
    dinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dinfo);

    unsigned int width = dinfo.output_width;
    unsigned int height = dinfo.output_height;
    int channels = dinfo.output_components;
    if (channels != 3) die("unexpected JPEG output channels");

    size_t row_stride = (size_t)width * 3;
    if (height != 0 && row_stride > SIZE_MAX / height) die("image too large");

    unsigned char *pixels = malloc(row_stride * height);
    if (!pixels) die_errno("malloc pixels failed");

    while (dinfo.output_scanline < dinfo.output_height) {
        JSAMPROW row = pixels + (size_t)dinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&dinfo, &row, 1);
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    fclose(in);

    FILE *out = fopen(out_path, "wb");
    if (!out) die_errno("cannot open output JPEG");

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr cerr;
    cinfo.err = jpeg_std_error(&cerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, out);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    cinfo.optimize_coding = TRUE;

    /* Intentionally no APPn markers, COM markers, EXIF, ICC profile, XMP, or thumbnails. */
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = pixels + (size_t)cinfo.next_scanline * row_stride;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(out);
    free(pixels);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [-q quality] input.{png,jpg,jpeg} output.{png,jpg,jpeg}\n"
        "\n"
        "Removes metadata by decoding pixels and writing a fresh image.\n"
        "PNG output keeps only IHDR/IDAT/IEND plus required structural chunks.\n"
        "JPEG output writes no EXIF/ICC/XMP/comments/thumbnails.\n"
        "\n"
        "options:\n"
        "  -q quality   JPEG quality 1..100, default 95\n",
        argv0);
}

int main(int argc, char **argv) {
    int quality = 95;
    int argi = 1;

    if (argc >= 3 && strcmp(argv[argi], "-q") == 0) {
        if (argc < 5) {
            usage(argv[0]);
            return 2;
        }
        quality = atoi(argv[argi + 1]);
        if (quality < 1 || quality > 100) die("quality must be 1..100");
        argi += 2;
    }

    if (argc - argi != 2) {
        usage(argv[0]);
        return 2;
    }

    const char *in_path = argv[argi];
    const char *out_path = argv[argi + 1];

    int in_png = is_png_file(in_path);
    int in_jpeg = is_jpeg_file(in_path);

    int out_png = has_ext(out_path, ".png");
    int out_jpg = has_ext(out_path, ".jpg") || has_ext(out_path, ".jpeg");

    if (in_png && out_png) {
        scrub_png(in_path, out_path);
    } else if (in_jpeg && out_jpg) {
        scrub_jpeg(in_path, out_path, quality);
    } else if (in_png && out_jpg) {
        die("PNG to JPEG conversion is not implemented in this minimal tool");
    } else if (in_jpeg && out_png) {
        die("JPEG to PNG conversion is not implemented in this minimal tool");
    } else {
        die("input/output type mismatch or unsupported format");
    }

    return 0;
}
