// libpng_write_fuzzer.cc
// Copyright 2017-2018 Glenn Randers-Pehrson
// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that may
// be found in the LICENSE file https://cs.chromium.org/chromium/src/LICENSE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>
#include <fstream>
#include <iostream>

#define PNG_INTERNAL
#include "png.h"
  
#define PNG_CLEANUP \
  if(png_handler.png_ptr) \
  { \
    if (png_handler.row_ptr) \
      png_free(png_handler.png_ptr, png_handler.row_ptr); \
    if (png_handler.info_ptr) \
      png_destroy_write_struct(&png_handler.png_ptr, &png_handler.info_ptr); \
    else \
      png_destroy_write_struct(&png_handler.png_ptr, nullptr); \
    png_handler.png_ptr = nullptr; \
    png_handler.row_ptr = nullptr; \
    png_handler.info_ptr = nullptr; \
    png_handler.end_info_ptr = nullptr; \
  }

// if (png_handler.end_info_ptr) \
//   png_destroy_write_struct(&png_handler.png_ptr, &png_handler.info_ptr); \
// else if (png_handler.info_ptr) \
//   png_destroy_write_struct(&png_handler.png_ptr, &png_handler.info_ptr); \
// else \
//   png_destroy_write_struct(&png_handler.png_ptr, nullptr); \

// png_handler.end_info_ptr = nullptr; \


#define TEXT_TITLE    0x01
#define TEXT_AUTHOR   0x02
#define TEXT_DESC     0x04
#define TEXT_COPY     0x08
#define TEXT_EMAIL    0x10
#define TEXT_URL      0x20

#define TEXT_TITLE_OFFSET        0
#define TEXT_AUTHOR_OFFSET      72
#define TEXT_COPY_OFFSET     (2*72)
#define TEXT_EMAIL_OFFSET    (3*72)
#define TEXT_URL_OFFSET      (4*72)
#define TEXT_DESC_OFFSET     (5*72)

typedef unsigned char   uch;
typedef unsigned short  ush;
typedef unsigned long   ulg;

struct WriteBuffer {
  std::vector<uint8_t> data;
};

/* ------------------------------------------------------------------------- */
/*  In-memory PNG round-trip harness – no PNG_STDIO_REQUIRED                 */
/* ------------------------------------------------------------------------- */

// struct BufState {
//   const uint8_t* data;
//   size_t bytes_left;
// };
struct BufferState {
  const uint8_t* data;
  size_t bytes_left;
  size_t size;
  size_t off;
};

struct PngObjectHandler {
  png_infop info_ptr = nullptr;
  png_structp png_ptr = nullptr;
  png_infop end_info_ptr = nullptr;
  png_voidp row_ptr = nullptr;
  WriteBuffer* write_buf = nullptr;

  ~PngObjectHandler() {
    if (row_ptr)
      png_free(png_ptr, row_ptr);
    if (info_ptr) {
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
      png_destroy_write_struct(&png_ptr, &info_ptr);
    }
    else {
      png_destroy_read_struct(&png_ptr, nullptr, nullptr);
      png_destroy_write_struct(&png_ptr, nullptr);
    }
    delete write_buf;
  }
};

void* limited_malloc(png_structp png_ptr, png_alloc_size_t size) {
  // libpng may allocate large amounts of memory that the fuzzer reports as
  // an error. In order to silence these errors, make libpng fail when trying
  // to allocate a large amount.
  // This number is chosen to match the default png_user_chunk_malloc_max.
  if (size > 8000000)
    return nullptr;

  return malloc(size);
}

static void limited_free(png_structp, png_voidp ptr) 
{
  free(ptr); 
}

void default_free(png_structp png_ptr, png_voidp ptr) 
{
  free(ptr);
}

/* Read from the fuzz-input buffer ---------------------------------------- */
static void read_cb(png_structp png_ptr, png_bytep dst, size_t len)
{
  auto* s = static_cast<BufferState*>(png_get_io_ptr(png_ptr));
  if (s->off + len > s->size)                      /* libpng will longjmp()   */
    png_error(png_ptr, "read past end of buffer");
  
  memcpy(dst, s->data + s->off, len);
  s->off += len;
}

/* Discard encoder output (we only care about exercising the code paths) ---- */
static void write_cb(png_structp png_ptr, png_bytep data, png_size_t length) 
{
    WriteBuffer* buf = static_cast<WriteBuffer*>(png_get_io_ptr(png_ptr));
    buf->data.insert(buf->data.end(), data, data + length);
}
static void flush_cb(png_structp) {}


void user_write_data(png_structp png_ptr, png_bytep data, png_size_t length) 
{
  WriteBuffer* buf = static_cast<WriteBuffer*>(png_get_io_ptr(png_ptr));
  buf->data.insert(buf->data.end(), data, data + length);
}
void user_flush_data(png_structp png_ptr) { /* Do nothing. Required stub. */ }


// Entry point for LibFuzzer.
// Roughly follows the libpng book example:
// http://www.libpng.org/pub/png/book/chapter15.html
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // if (size < 8) or 16
  if (size < 32) return 0;

  uint32_t width = (data[0] << 8) | data[1];
  uint32_t height = (data[2] << 8) | data[3];
  // int bit_depth = 8;
  // int color_type = PNG_COLOR_TYPE_RGBA;
  if (width == 0 || height == 0 || width > 1024 || height > 1024) return 0;

  // uint32_t width = ((data[0] << 8) | data[1]) % 1024 + 1;
  // uint32_t height = ((data[2] << 8) | data[3]) % 1024 + 1;

  int bit_depth_options[] = {1, 2, 4, 8};
  int bit_depth = bit_depth_options[data[4] % 4];

  int color_type_options[] = {
    PNG_COLOR_TYPE_GRAY,
    PNG_COLOR_TYPE_GRAY_ALPHA,
    PNG_COLOR_TYPE_RGB,
    PNG_COLOR_TYPE_RGBA
  };
  int color_type = color_type_options[data[5] % 4];

  // png_color_8 shift = {bit_depth / 2, bit_depth / 2, bit_depth / 2, bit_depth / 2, bit_depth / 2};
  uint8_t flags = data[6];
  
  PngObjectHandler png_handler;
  png_handler.write_buf = new WriteBuffer();

  png_handler.png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_handler.png_ptr) {
    return 0;
  }

  png_handler.info_ptr = png_create_info_struct(png_handler.png_ptr);
  if (!png_handler.info_ptr) {
    PNG_CLEANUP
    return 0;
  }
  
  if (setjmp(png_jmpbuf(png_handler.png_ptr))) {
    PNG_CLEANUP
    return 0;
  }

  /* make sure outfile is (re)opened in BINARY mode */
  // png_init_io(png_ptr, mainprog_ptr->outfile);

  png_set_compression_level(png_handler.png_ptr, Z_BEST_COMPRESSION);


  // /* limit allocations & hook up in-memory write */  -- Not sure about these two?
  // png_set_mem_fn(png_handler.png_ptr, nullptr, limited_malloc, limited_free);
  // png_set_write_fn(png_handler.png_ptr, png_handler.write_buf,
  //                  write_cb, flush_cb);
  
  png_set_IHDR(png_handler.png_ptr, png_handler.info_ptr,
               width, height,
               bit_depth, /* bit depth  8, */
               color_type,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  /*---- Set the gamma component of the png ----*/
  png_set_gAMA(png_handler.png_ptr, png_handler.info_ptr, mainprog_ptr->gamma);

  /* we know it's RGBA, not gray+alpha */
  png_color_16  background;
  background.red = 255;
  background.green = 255;
  background.blue = 255;
  png_set_bKGD(png_handler.png_ptr, png_handler.info_ptr, &background);

  
  /*---- Set the time of the png ----*/
  png_time  modtime;
  png_convert_from_time_t(&modtime, time(NULL));
  png_set_tIME(png_handler.png_ptr, png_handler.info_ptr, &modtime);

  
  /*---- Set the text of the png ----*/
  png_text  text[6];
  int  num_text = 6;
  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "Title";
  text[num_text].text = "Test image";

  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "Author";
  text[num_text].text = "Rodrigue2g";
  
  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "Description";
  text[num_text].text = "Png test description for OSS-Fuzz";
  
  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "Copyright";
  text[num_text].text = "2025 - All rights reserved.";
  
  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "E-mail";
  text[num_text].text = "email@example.com";
  
  text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
  text[num_text].key = "URL";
  text[num_text].text = "https://www.example.com";
  png_set_text(png_ptr, info_ptr, text, num_text);


  // png_set_write_fn(png_handler.png_ptr, png_handler.write_buf, user_write_data, user_flush_data);
  // png_set_IHDR(png_handler.png_ptr, png_handler.info_ptr, width, height,
  //              bit_depth, color_type, PNG_INTERLACE_NONE,
  //              PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  // png_write_info(png_handler.png_ptr, png_handler.info_ptr);

  
  /* write all chunks up to (but not including) first IDAT */
  png_write_info(png_handler.png_ptr, png_handler.info_ptr);

  
  /* set up the transformations:  for now, just pack low-bit-depth pixels
   * into bytes (one, two or four pixels per byte) */
  png_set_packing(png_handler.png_ptr);
  // if (flags & 1) png_set_packing(png_handler.png_ptr);
  // if (flags & 2) png_set_shift(png_handler.png_ptr, &shift);
  if (flags & 4) png_set_swap_alpha(png_handler.png_ptr);
  if (flags & 8) png_set_invert_alpha(png_handler.png_ptr);



  /* Write the png image */
  size_t rowbytes = png_get_rowbytes(png_handler.png_ptr, png_handler.info_ptr);
  std::vector<uint8_t> img(rowbytes * height, 0); /* black */
  std::vector<png_bytep> rows(height);
  for (size_t i = 0; i < height; ++i) rows[i] = img.data() + i * rowbytes;

  png_write_image(png_handler.png_ptr, rows.data());
  // png_write_end(png_handler.png_ptr, nullptr);

  
  /* Write the png image in a different fation now (with png_write_row) */
  for (size_t i = 0; i < height; ++i) {
    rows[i] = img.data() + i * rowbytes;
    for (size_t j = 0; j < rowbytes; ++j) {
      size_t idx = i * rowbytes + j;
      rows[i][j] = (idx < size - 32) ? data[32 + idx] : (uint8_t)(idx % 256);
    }
  }

  png_write_rows(png_handler.png_ptr, rows, height);
  // png_write_end(png_handler.png_ptr, nullptr);

  for (size_t i = 0; i < height; ++i)
    png_write_row(png_handler.png_ptr, rows[i]);

  /* close out PNG file; if we had any text or time info to write after
   * the IDATs, second argument would be info_ptr: */
  png_write_end(png_handler.png_ptr, nullptr);

  /* ------------------------------------------------------------------ */
  /* 2.  Immediately **read back** the generated PNG to hit decode paths */
  /* ------------------------------------------------------------------ */
  // BufferState rbuf{ png_handler.write_buf->data.data(),
  //                   png_handler.write_buf->data.size(), 0 };

  // png_structp rd = png_create_read_struct(PNG_LIBPNG_VER_STRING,
  //                                         nullptr, nullptr, nullptr);
  // if (rd) {
  //   png_infop rd_info = png_create_info_struct(rd);
  //   if (rd_info) {
  //     if (setjmp(png_jmpbuf(rd)) == 0) {
  //       png_set_mem_fn(rd, nullptr, limited_malloc, limited_free);
  //       png_set_read_fn(rd, &rbuf, read_cb);

  //       /* choose transform flags from fuzz‑data byte 4 for variety */
  //       uint8_t tf = data[4];
  //       int flags = 0;
  //       if (tf & 1) flags |= PNG_TRANSFORM_EXPAND;
  //       #ifdef PNG_TRANSFORM_PACKING
  //       if (tf & 2) flags |= PNG_TRANSFORM_PACKING;
  //       #endif
  //       if (tf & 4) flags |= PNG_TRANSFORM_STRIP_ALPHA;
  //       if (tf & 8) flags |= PNG_TRANSFORM_INVERT_MONO;

  //       png_read_png(rd, rd_info, flags, nullptr);
  //     }
  //     png_destroy_read_struct(&rd, &rd_info, nullptr);
  //   } else {
  //     png_destroy_read_struct(&rd, nullptr, nullptr);
  //   }
  // }

  PNG_CLEANUP
  return 0;
}



// extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
// {
//   /* libpng needs the 8-byte PNG signature; tiny inputs add no coverage     */
//   if (size < 8)
//     return 0;

//   BufferState buf{data, size, 0};

//   png_structp png_r = png_create_read_struct (PNG_LIBPNG_VER_STRING,
//                                               nullptr, nullptr, nullptr);
//   if (!png_r) return 0;

//   png_structp png_w = png_create_write_struct(PNG_LIBPNG_VER_STRING,
//                                               nullptr, nullptr, nullptr);
//   if (!png_w) { png_destroy_read_struct(&png_r, nullptr, nullptr); return 0; }

//   png_infop info = png_create_info_struct(png_r);
//   if (!info) {
//     png_destroy_read_struct (&png_r, nullptr, nullptr);
//     png_destroy_write_struct(&png_w, nullptr);
//     return 0;
//   }

//   /* libpng long-jmp error exit ------------------------------------------- */
//   if (setjmp(png_jmpbuf(png_r))) {
//     png_destroy_read_struct (&png_r, &info, nullptr);
//     png_destroy_write_struct(&png_w, nullptr);
//     return 0;
//   }

//   /* Memory-allocation guard (optional) ----------------------------------- */
//   png_set_mem_fn(png_r, nullptr, limited_malloc, limited_free);
//   png_set_mem_fn(png_w, nullptr, limited_malloc, limited_free);

//   /* Hook up our in-memory I/O -------------------------------------------- */
//   png_set_read_fn (png_r, &buf, read_cb);
//   png_set_write_fn(png_w, nullptr, write_cb, flush_cb);

//   /* Decode, then immediately re-encode the image ------------------------- */
//   png_read_png (png_r, info, PNG_TRANSFORM_IDENTITY, nullptr);
//   png_write_png(png_w, info, PNG_TRANSFORM_IDENTITY, nullptr);

//   png_destroy_read_struct (&png_r, &info, nullptr);
//   png_destroy_write_struct(&png_w, nullptr);
//   return 0;
// }
