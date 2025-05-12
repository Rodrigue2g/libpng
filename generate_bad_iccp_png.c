#include <stdio.h>
#include <stdlib.h>
#include "png.h"

int main() {
    FILE *fp = fopen("malformed_iccp.png", "wb");
    if (!fp) { perror("fopen"); return 1; }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) return 1;

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "libpng write error\n");
        fclose(fp);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return 1;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr,
        1, 1, 8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    const char *profile_name = "sRGB";
    int compression_type = 0;

    // ❗ Invalid profile data (less than 4 bytes)
    png_bytep bad_profile = (png_bytep)"ABC"; // only 3 bytes
    png_uint_32 bad_profile_len = 3;

    png_set_iCCP(png_ptr, info_ptr, profile_name, compression_type, bad_profile, bad_profile_len);

    png_write_info(png_ptr, info_ptr);

    png_bytep row = malloc(3);
    row[0] = 255; row[1] = 0; row[2] = 0;
    png_write_row(png_ptr, row);

    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    free(row);
    fclose(fp);

    return 0;
}
