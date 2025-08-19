#ifndef __PNG_DECODER__
#define __PNG_DECODER__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "logger.h"


//////////////////////////custom includes
#include "../../std_lib/allocators/arena.h"

////////////////////////// typedefs
typedef unsigned uint;

typedef enum {
	//state initialization
	TOK_INIT = 0,
	//critical headers
	TOK_IHDR,
	TOK_PLTE,
	TOK_IDAT,
	TOK_IEND,
	//ancillary headers
	//we dont need them for now, but can still parse them
	TOK_BKGD,
	TOK_PHYS,
} token_e;

typedef struct {
	uint8_t r, g, b;
} colour_s;

typedef struct {
	uint32_t width, height;
	uint8_t  bit_depth, color_type, compression_method, filter_method, interlace_method;
} header_chunk_s;

typedef struct {
	uint	  actual_size;
	colour_s* colour_array;
} palette_chunk_s;

typedef struct {
	uint32_t actual_size;
	uint8_t* data;
} data_chunk_s;

typedef struct {
	//this contains internal info about png, which is needed to decode it correctly
	header_chunk_s	ihdr;
	palette_chunk_s plte;
	data_chunk_s	idat;
} png_internal_context_s;

typedef struct {
	//this contains info about which user may care
	uint width, height;
	uint8_t** pixels;
} png_external_context_s;

////////////////////////// declarations
png_external_context_s* decode_from_png(char* _input_png, const uint _size);

#endif //__PNG_DECODER__
