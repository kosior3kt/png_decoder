#include "png_decoder.h"
#include "zlib.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEMP_SAVE_FILE "generated/compressed.bin"
#define MAGIC_NUM_LEN 8
#define PNG_MAGIC_NUMBER "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a"
#define CRC_LEN		4
#define ADVANCE_BYTE(_x) \
do{	\
	if(current_byte + _x >= ending_byte) {	\
		LOG(LOG_ERROR, "tried to advance byte out of the valid range\n current byte: %ld, ending byte: %ld, and tried to shift by: %d", current_byte, ending_byte, _x);	\
		ASSERT_AND_FLUSH(0); \
	} \
	current_byte += _x;	\
}while(0);

#define GET_TOKEN_NAME(_x)		 \
((_x) == TOK_INIT ? "TOK_INIT" : \
(_x)  == TOK_IHDR ? "TOK_IHDR" : \
(_x)  == TOK_PLTE ? "TOK_PLTE" : \
(_x)  == TOK_IDAT ? "TOK_IDAT" : \
(_x)  == TOK_IEND ? "TOK_IEND" : \
(_x)  == TOK_BKGD ? "TOK_BKGD" : \
(_x)  == TOK_PHYS ? "TOK_PHYS" : \
"TOK_UNKNOWN")

#define AS_HEX_ARR(_arr) (bytes_to_hex(_arr, sizeof(_arr) - 1))
#define AS_HEX_N(_arr, _size) (bytes_to_hex(_arr, _size))
#define AS_HEX(_x) (bytes_to_hex(_x, 1))

////////////////////////// global variables
static uint8_t* ending_byte;
static uint8_t* current_byte;


////////////////////////// declarations
static png_external_context_s* process_next_chunk(const token_e _current_token);
static token_e get_next_token();
static bool look_for_magic_bytes();
static const char* bytes_to_hex(const char*, const size_t);
static const uint32_t get_next_chunk_size();
static const bool check_CRC();
static const uint parse_header(header_chunk_s* _header);
static const bool check_header(const header_chunk_s _header);
static const uint parse_palette(palette_chunk_s* _palette, const uint8_t _size);
static const uint parse_data(data_chunk_s* _data);
static const void save_to_temp_file(const char* _file_path, const data_chunk_s* _data);
static const void preprocess(const data_chunk_s* _data, png_external_context_s* _ret_ctx);

static const int inf(FILE *source, FILE *dest);
static const int def(FILE *source, FILE *dest, int level);
void zerr(int ret);


////////////////////////// definitions
png_external_context_s* decode_from_png(char* _input_png, const uint _size)
{
	ASSERT_AND_FLUSH(_size > 0);
	ASSERT_AND_FLUSH(_input_png != NULL);

	ending_byte  = _input_png + _size;
	current_byte = _input_png;

	return process_next_chunk(TOK_INIT);
}

png_external_context_s* process_next_chunk(const token_e _current_token)
{
	ASSERT_AND_FLUSH(ending_byte  != NULL);
	ASSERT_AND_FLUSH(current_byte != NULL);
	ASSERT_AND_FLUSH(current_byte != ending_byte);	//TODO: could they every be equal according to spec?
	ASSERT_AND_FLUSH(current_byte <  ending_byte);	//make sure we are not about to loop for ever

	LOG(LOG_INFO, "starting processing chunk: %s", GET_TOKEN_NAME(_current_token));

	token_e next_token;
	png_external_context_s* ret_ctx = malloc(sizeof(png_external_context_s));

	static png_internal_context_s internal_context;
	static arena_s arena;		//TODO: be careful with this arena
	static int32_t next_chunk_size;

	switch(_current_token) {
		//for now we just gonna care about critical chunks
		case TOK_INIT: {
			//this is entry point to the png parser - here we init all the shit
			//later this func will be called recursively until we encounter IEND
			//token or something goes south really bad
			//TODO: add magic number handling here
			if(!look_for_magic_bytes()) {
				//this is not even PNG!!!
				LOG(LOG_ERROR, "provided file is not a png");
				return NULL;
			}
			break;
		}
		case TOK_IHDR: {
			//CRITICAL: it needs to be the first encountered chunk
			uint shifted_bytes = parse_header(&(internal_context.ihdr));
			next_chunk_size -= shifted_bytes;
			if(next_chunk_size < 0) {
				LOG(LOG_ERROR, "read too little bytes from header");
			}
			if(!check_header(internal_context.ihdr)) {
				LOG(LOG_ERROR, "error eouncountered while parsing header");
			};
			break;
		}
		case TOK_PLTE: {
			//CRITICAL: it contains the indexed palette that later is used for colors encoding
			//TODO: make sure it appears for correct color types and bit depths or sth
			if((next_chunk_size % 3) != 0) {
				LOG(LOG_ERROR, "data section of palette chunk is not divisible by 3 - its equal: %d", next_chunk_size);
			} else {
				const uint8_t number_of_entries = next_chunk_size / 3;
				if(number_of_entries == 0 || number_of_entries > 256) {
					LOG(LOG_ERROR, "number of entries is either too big of too small: %d (allowed values are from 1 to 256)", number_of_entries);
				}
				uint shifted_bytes = parse_palette(&(internal_context.plte), number_of_entries);
				next_chunk_size -= shifted_bytes;
				if(next_chunk_size > 0) {
					LOG(LOG_ERROR, "read too little bytes from palette");
				}
			}
			break;
		}
		case TOK_IDAT: {
			//CRITICAL: this is THE data
			LOG(LOG_DEBUG_CRITICAL, "len of IDAT: %ld", next_chunk_size);
			LOG(LOG_DEBUG_CRITICAL, "contents of IDAT: %s", AS_HEX_N(current_byte, next_chunk_size));

			//prepare idat chunk
			internal_context.idat.actual_size = next_chunk_size;
			internal_context.idat.data = malloc(sizeof(uint8_t) * internal_context.idat.actual_size);

			uint shifted_bytes = parse_data(&(internal_context.idat));
			next_chunk_size -= shifted_bytes;
			if(next_chunk_size > 0) {
				LOG(LOG_ERROR, "read too little bytes from idat chunk");
			}

			//TODO: here we should see if all the chunks provided so far match all the bit depth and so on
			break;
		}
		case TOK_PHYS: {
			//ANCILLARY
			//idk what this is for
			break;
		}
		case TOK_BKGD: {
			//ANCILLARY
			//idk what this is for
			break;
		}
		case TOK_IEND: {
			//CRITICAL: this is ending token - if it's not we gotta throw


			ret_ctx.width = internal_context.ihdr.width;
			ret_ctx.height = internal_context.ihdr.height;

			preprocess(&(internal_context.idat), ret_ctx);
			save_to_temp_file(TEMP_SAVE_FILE, &(internal_context.idat));
			LOG(LOG_INFO, "recieved end token");
			return ret_ctx;
		}
		default: {
			//should never be reached
			LOG(LOG_ERROR, "revievied unknown token");
			ASSERT_AND_FLUSH(0);
		}
	}

	{	//TEMP
		LOG(LOG_WARNING, "skiping whole block for now");
		ADVANCE_BYTE(next_chunk_size);
		next_chunk_size = 0;
	}

	{ //TEMP
		if(_current_token != TOK_INIT) {
			check_CRC();
		}
	}

	if(next_chunk_size > 0)		  LOG(LOG_ERROR, "expected to read less data from the current chunk - byte left: %ld", next_chunk_size);
	else if(next_chunk_size != 0) LOG(LOG_ERROR, "expected to read more data from the current chunk - byte left: %ld", next_chunk_size);

	next_chunk_size = get_next_chunk_size();
	next_token = get_next_token();
	return process_next_chunk(next_token);
}

token_e get_next_token()
{
	const uint chunk_str_len = 4;

	ASSERT_AND_FLUSH(ending_byte  != NULL);
	ASSERT_AND_FLUSH(current_byte != NULL);
	ASSERT_AND_FLUSH(current_byte != ending_byte);
	//png chunks are guaranteed to be 4 letters long
	ASSERT_AND_FLUSH(current_byte + chunk_str_len < ending_byte);	//TODO: check specs if png can end with token and no data? prolly not

	char chunk_name[chunk_str_len + 1];
	memset(chunk_name, 0, sizeof(char) * (chunk_str_len + 1));

	LOG(LOG_DEBUG_2, "10 next bytes before shifting: %s", AS_HEX_N(current_byte, 10));

	for(int i = 0; i < chunk_str_len; ++i) {
		//chunks in png can be lower or upper case - this means a lot of things
		//that I dont care about, so just make them lowercase for now
		chunk_name[i] = *current_byte | (1 << 5);
		ADVANCE_BYTE(1);
	}
	LOG(LOG_DEBUG_3, "read chunk as: %s (%s)", chunk_name, AS_HEX_N(chunk_name, 4));
	LOG(LOG_DEBUG_2, "10 next bytes after shifting: %s", AS_HEX_N(current_byte, 10));

#define TOKEN_MATCHES(_str) (strncmp(chunk_name, _str, chunk_str_len) == 0)
	token_e token;
	if(TOKEN_MATCHES("ihdr")) {
		token = TOK_IHDR;
	} else if(TOKEN_MATCHES("plte")) {
		token = TOK_PLTE;
	} else if(TOKEN_MATCHES("idat")) {
		token = TOK_IDAT;
	} else if(TOKEN_MATCHES("iend")) {
		token = TOK_IEND;
	} else if(TOKEN_MATCHES("phys")) {
		token = TOK_PHYS;
	} else if(TOKEN_MATCHES("bkgd")) {
		token = TOK_BKGD;
	} else {
		//something went wrong
		//TODO: add rest of tokens later
		char temp_log_arr[chunk_str_len + 1];
		memset(temp_log_arr, 0, sizeof(char) * chunk_str_len + 1);
		memcpy(temp_log_arr, chunk_name, chunk_str_len);
		temp_log_arr[chunk_str_len] = '\0';
		LOG(LOG_ERROR, "there is a unrecognized chunk: %s (%s)\n", chunk_name, AS_HEX_ARR(temp_log_arr));
		ASSERT_AND_FLUSH(0);
	}
	return token;
#undef TOKEN_MATCHES
}

static bool look_for_magic_bytes()
{
	ASSERT_AND_FLUSH(current_byte != NULL);
	if(memcmp(current_byte, PNG_MAGIC_NUMBER, MAGIC_NUM_LEN) != 0) {
		char temp_log_arr[MAGIC_NUM_LEN + 1];
		memcpy(temp_log_arr, current_byte, MAGIC_NUM_LEN);
		temp_log_arr[MAGIC_NUM_LEN] = '\0';
		LOG(LOG_ERROR, "incorrect magic numbers - expected: %s got: %s", PNG_MAGIC_NUMBER, temp_log_arr);
		return false;
	}
	ADVANCE_BYTE(MAGIC_NUM_LEN);
	return true;
}

static const char* bytes_to_hex(const char* _data, const size_t _len)
{
    static char hex_buffer[1028];
    memset(hex_buffer, 0, sizeof(hex_buffer));

    for (size_t i = 0; i < _len; i++) {
        snprintf(hex_buffer + strlen(hex_buffer),
                 sizeof(hex_buffer) - strlen(hex_buffer),
                 "%02X ", (unsigned char)_data[i]);
    }
	return hex_buffer;
}

static const uint32_t get_next_chunk_size()
{
	const uint shift_size = sizeof(uint32_t)/sizeof(uint8_t);
	ASSERT_AND_FLUSH(ending_byte  != NULL);
	ASSERT_AND_FLUSH(current_byte != NULL);
	ASSERT_AND_FLUSH(current_byte != ending_byte);
	ASSERT_AND_FLUSH(current_byte + shift_size < ending_byte);

	//4 bytes at the beggining of the chunk define its len
	LOG(LOG_DEBUG_2, "10 next bytes before shifting: %s", AS_HEX_N(current_byte, 10));

	int32_t len = 0;
	union byte_u {
		uint32_t len;
		uint8_t  padding[4];
	} temp_union;

	for(uint i = 0; i < shift_size; ++i) {
		temp_union.padding[shift_size - i - 1] = (uint8_t)*current_byte;
		LOG(LOG_DEBUG_2, "adding: %ld (%s)", (uint8_t)*current_byte, AS_HEX_N((char*)(current_byte), sizeof(int32_t)));
		ADVANCE_BYTE(1);
	}

	LOG(LOG_DEBUG_1, "union: %ld", temp_union.len);
	LOG(LOG_INFO, "len for next block: %ld (%s)", temp_union.len, AS_HEX_N((char*)(&temp_union.len), sizeof(int32_t)));

	LOG(LOG_DEBUG_2, "10 next bytes after shifting: %s", AS_HEX_N(current_byte, 10));
	LOG(LOG_DEBUG_3, "next block size is: %ld", temp_union.len);
	return temp_union.len;
}

const bool check_CRC()
{
	LOG(LOG_WARNING, "this is not implemented yet - add it later");
	LOG(LOG_DEBUG_3, "CRC bytes: %s", AS_HEX_N(current_byte, CRC_LEN));
	ADVANCE_BYTE(CRC_LEN);
	return true;
}

static const uint parse_header(header_chunk_s* _header)
{
#define DIM_LEN		4
#define HEADER_LEN 13
	//according to specs the header consists of 13 bytes and they are as follows:
	//width:              4 bytes
    //height:             4 bytes
    //bit depth:          1 byte
    //color type:         1 byte
    //compression method: 1 byte
    //filter method:      1 byte
    //interlace method:   1 byte

	union {
		uint32_t value;
		uint8_t  padding[4];
	} casting_union;

	for(int i = 0; i < DIM_LEN; ++i) {
		casting_union.padding[DIM_LEN - i - 1] = (uint8_t)*current_byte;
		LOG(LOG_DEBUG_1, "width - adding: %ld (%s)", (uint8_t)*current_byte, AS_HEX_N((char*)(current_byte), sizeof(int32_t)));
		ADVANCE_BYTE(1);
	}
	_header->width = casting_union.value;

	casting_union.value = 0;

	for(int i = 0; i < DIM_LEN; ++i) {
		casting_union.padding[DIM_LEN - i - 1] = (uint8_t)*current_byte;
		LOG(LOG_DEBUG_1, "height - adding: %ld (%s)", (uint8_t)*current_byte, AS_HEX_N((char*)(current_byte), sizeof(int32_t)));
		ADVANCE_BYTE(1);
	}
	_header->height = casting_union.value;

	_header->bit_depth = (uint8_t)*current_byte;
	ADVANCE_BYTE(1);
	LOG(LOG_DEBUG_1, "bit_depth: %d", _header->bit_depth);

	_header->color_type = (uint8_t)*current_byte;
	ADVANCE_BYTE(1);
	LOG(LOG_DEBUG_1, "color_type: %d", _header->color_type);

	_header->compression_method = (uint8_t)*current_byte;
	ADVANCE_BYTE(1);
	LOG(LOG_DEBUG_1, "compression_method: %d", _header->compression_method);

	_header->filter_method = (uint8_t)*current_byte;
	ADVANCE_BYTE(1);
	LOG(LOG_DEBUG_1, "filter_method: %d", _header->filter_method);

	_header->interlace_method = (uint8_t)*current_byte;
	ADVANCE_BYTE(1);
	LOG(LOG_DEBUG_1, "interlace_method: %d", _header->interlace_method);

	return HEADER_LEN;

#undef DIM_LEN
#undef HEADER_LEN
}

static const bool check_header(const header_chunk_s _header)
{
	bool ret_status = true;
	//header fields can only take specyfic values - we check that here:
	//width and height is fine as it is

	//bit depth can be: 1, 2, 4, 8, 16
	if(_header.bit_depth != 1 && _header.bit_depth != 2 &&
	   _header.bit_depth != 4 && _header.bit_depth != 8 &&
	   _header.bit_depth != 16) {
		LOG(LOG_ERROR, "encountered disallowed bit depth");
		ret_status = false;
	}

	//color type: 0, 2, 3, 4, 6
	if(_header.color_type != 0 && _header.color_type != 2 &&
	   _header.color_type != 3 && _header.color_type != 4 &&
	   _header.color_type != 6) {
		LOG(LOG_ERROR, "encountered disallowed color type: %d", _header.color_type);
		ret_status = false;
	}

	//compression method: 0
	if(_header.compression_method != 0) {
		LOG(LOG_ERROR, "encountered disallowed compression method");
		ret_status = false;
	}

	//filter method: 0
	if(_header.filter_method != 0) {
		LOG(LOG_ERROR, "encountered disallowed filter method");
		ret_status = false;
	}

	//interlace method: 0, 1
	if(_header.interlace_method != 0 &&
	   _header.interlace_method != 1) {
		LOG(LOG_ERROR, "encountered disallowed interlace method");
		ret_status = false;
	}

	//color type and bit depth can also appear only in certain configurations:
	//  Color   Allowed     Interpretation
    //  Type    Bit Depths
	//
    //  0       1,2,4,8,16  Each pixel is a grayscale sample.
	//
    //  2       8,16        Each pixel is an R,G,B triple.
	//
    //  3       1,2,4,8     Each pixel is a palette index;
    //                      a PLTE chunk must appear.
	//
    //  4       8,16        Each pixel is a grayscale sample,
    //                      followed by an alpha sample.
	//
    //  6       8,16        Each pixel is an R,G,B triple,
    //                      followed by an alpha sample.


	switch(_header.color_type) {
		case 0: {
			//all bit depth are allowed - intentionally empty
			break;
		}
		case 3: {
			if(!(_header.bit_depth == 1 || _header.bit_depth == 2 ||
			   _header.bit_depth == 4 || _header.bit_depth == 8)) {
				LOG(LOG_ERROR, "encountered disallowed bit depth and color type configuration - bit depth: %d, color type: %d",
						_header.bit_depth, _header.color_type);
				ret_status = false;
			}
			break;
		}
		case 2:	//fallthrough
		case 4:	//fallthrough
		case 6:{
			if(_header.bit_depth != 8 || _header.bit_depth != 16) {
				LOG(LOG_ERROR, "encountered disallowed bit depth and color type configuration - bit depth: %d, color type: %d",
						_header.bit_depth, _header.color_type);
				ret_status = false;
			}
			break;
		}
		default: {
			LOG(LOG_ERROR, "encountered unrecognized color type: %d", _header.color_type);
			ASSERT_AND_FLUSH(0);
		}
	}

	return ret_status;
}

static const uint parse_palette(palette_chunk_s* _palette, const uint8_t _size)
{
	ASSERT_AND_FLUSH(_palette != NULL);
	ASSERT_AND_FLUSH(_size != 0);
	ASSERT_AND_FLUSH(_size <= 256);

	_palette->actual_size = _size;
	_palette->colour_array = malloc(sizeof(colour_s) * _size);

	for(int i = 0; i < _size; ++i) {
		_palette->colour_array[i].r = (uint8_t)*current_byte;
		ADVANCE_BYTE(1);
		_palette->colour_array[i].g = (uint8_t)*current_byte;
		ADVANCE_BYTE(1);
		_palette->colour_array[i].b = (uint8_t)*current_byte;
		ADVANCE_BYTE(1);
	}
	return _size * 3;
}

static const uint parse_data(data_chunk_s* _data)
{
	ASSERT_AND_FLUSH(_data->data != NULL);
	ASSERT_AND_FLUSH(_data->actual_size != 0);
	ASSERT_AND_FLUSH(current_byte + _data->actual_size < ending_byte);

	memcpy(_data->data, current_byte, _data->actual_size);
	ADVANCE_BYTE(_data->actual_size);
	return _data->actual_size;
}

static const void save_to_temp_file(const char* _file_path, const data_chunk_s* _data)
{
	assert(_file_path != NULL);
	LOG(LOG_DEBUG_CRITICAL, "got end token - saving to temp file: %s", _file_path);

	//this is fucking ugly but let's roll with it
	//will this even compile?
	*(_data->data + _data->actual_size) = '\0';

	uint8_t* buff = malloc(sizeof(uint8_t) * (_data->actual_size + 1));
	FILE* temp_input_stream = fmemopen(buff, _data->actual_size + 1, "w+");
	if(temp_input_stream == NULL) {
		LOG_ERRNO(LOG_DEBUG_CRITICAL, "error - can not create a stream in memory!");
		return;
	}


	if(fwrite(_data->data, sizeof(uint8_t), _data->actual_size, temp_input_stream) == 0) {
		LOG_ERRNO(LOG_DEBUG_CRITICAL, "something went wrong");
	}
	rewind(temp_input_stream);

	FILE* fp = fopen(_file_path, "w+");
	if(fp == NULL) {
		LOG_ERRNO(LOG_DEBUG_CRITICAL, "something went wrong with opening other file");
	}

	int ret = inf(temp_input_stream, fp);


	if(ret != Z_OK) {
		LOG_ERRNO(LOG_DEBUG_CRITICAL, "idk, seems like it failed");
		zerr(ret);
	}

	fclose(fp);
	fclose(temp_input_stream);

	LOG(LOG_DEBUG_CRITICAL, "this thing might have succeeded");
}


static const void preprocess(const data_chunk_s* _data, png_external_context_s* _ret_ctx)
{
	//according to specs each line of data is prepended with a byte
	//and then padded to line it up to a byte boundary
	//this means we need to first divide it into lines, then subtract 1 byte
	//from the beggining of each line and later subtract from the end whatever
	//was added to line it up

	assert(_data	!= NULL);
	assert(_ret_ctx != NULL);
	assert(_ret_ctx->height > 0);
	assert(_ret_ctx->width	> 0);

	_ret_ctx->pixels = malloc(sizeof(uint8_t) * _ret_ctx->height);
	for(int i = 0; i < _ret_ctx->height; ++i) {
		*(_ret_ctx->pixels + i) = malloc(sizeof(uint8_t) * _ret_ctx->width);
	}

	for(int i = 0; i < _ret_ctx->height; ++i) {
		asm("nop": : :);
	}
	LOG(LOG_ERROR, "unfinished code part");

}


#undef GET_TOKEN_NAME
#undef MAGIC_NUM_LEN
#undef PNG_MAGIC_NUMBER
#undef CRC_LEN
#undef AS_HEX
#undef AS_HEX_ARR
#undef AS_HEX_N








////////////////////////// START

#if defined(_WIN32) && !defined(_CRT_NONSTDC_NO_DEPRECATE)
#  define _CRT_NONSTDC_NO_DEPRECATE
#endif


#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define CHUNK 16384


static const int def(FILE *source, FILE *dest, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return ret;

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)deflateEnd(&strm);
            return Z_ERRNO;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    return Z_OK;
}

static const int inf(FILE *source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;     /* and fall through */
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					(void)inflateEnd(&strm);
					return ret;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

/* report a zlib or i/o error */
void zerr(int ret)
{
    fputs("zpipe: ", stderr);
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin))
            fputs("error reading stdin\n", stderr);
        if (ferror(stdout))
            fputs("error writing stdout\n", stderr);
        break;
    case Z_STREAM_ERROR:
        fputs("invalid compression level\n", stderr);
        break;
    case Z_DATA_ERROR:
        fputs("invalid or incomplete deflate data\n", stderr);
        break;
    case Z_MEM_ERROR:
        fputs("out of memory\n", stderr);
        break;
    case Z_VERSION_ERROR:
        fputs("zlib version mismatch!\n", stderr);
    }
}

/* compress or decompress from stdin to stdout */
int dupa(int argc, char **argv)
{
    int ret;


    /* do compression if no arguments */
    if (argc == 1) {
        ret = def(stdin, stdout, Z_DEFAULT_COMPRESSION);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }

    /* do decompression if -d specified */
    else if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        ret = inf(stdin, stdout);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }

    /* otherwise, report usage */
    else {
        fputs("zpipe usage: zpipe [-d] < source > dest\n", stderr);
        return 1;
    }
}


////////////////////////// START





