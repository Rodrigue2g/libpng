#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"

int main() {
    FILE *fp = fopen("malformed_iccp.png", "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL, NULL, NULL);
    if (!png_ptr) return 1;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return 1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("libpng triggered an error (likely due to bad profile)\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 1;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr,
                 1, 1, 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    // Malformed iCCP chunk: less than 4 bytes for ICC profile
    const char *profile_name = "FakeProfile";
    int compression_type = 0;
    png_bytep bad_profile = (png_bytep)"ABC";  // 3 bytes: too short!
    png_uint_32 bad_profile_len = 3;

    png_set_iCCP(png_ptr, info_ptr,
                 profile_name, compression_type,
                 bad_profile, bad_profile_len);

    png_write_info(png_ptr, info_ptr);

    // Write dummy image data
    png_bytep row = (png_bytep)malloc(3);
    memset(row, 255, 3);
    png_write_row(png_ptr, row);
    png_write_end(png_ptr, NULL);
    free(row);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    printf("Wrote malformed_iccp.png with invalid iCCP chunk.\n");
    return 0;
}
