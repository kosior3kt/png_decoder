#include <stdio.h>

//////////////////////////custom includes
#include "png_decoder.h"
#include "logger.h"

#define FILE_PATH_PNG "assets/read_png.png"
#define WRITE_FILE_PATH_PNG "assets/write.png"


int main()
{
	//logger_init(LOG_ERROR, "logs/log");
	//logger_init(LOG_DEBUG_3, "logs/log");
	logger_init(LOG_DEBUG_CRITICAL, "logs/log");
	//////////////////////////FUTURE WORKFLOW
	//read base64 garbage
	//transform it into binary

	//////////////////////////CURRENT WORKFLOW
	//read this png garbage
	FILE* fp;
	fp = fopen(FILE_PATH_PNG, "r");
	if(fp == NULL) {
		LOG_ERRNO(LOG_ERROR, "cannot open a file");
		return 1;
	}

	//read whole file
	fseek(fp, 0, SEEK_END);
	long num_of_bytes = ftell(fp);
	(void)fseek(fp, 0, SEEK_SET);
	char* file_contents = (char*)malloc(sizeof(char) * (num_of_bytes + 1));
	fread(file_contents, num_of_bytes, sizeof(char), fp);
	file_contents[num_of_bytes] = '\0';
	fclose(fp);

	LOG(LOG_INFO, "read %d, bytes from og file", num_of_bytes);

	fopen(WRITE_FILE_PATH_PNG, "w+");
	if(fp == NULL) {
		LOG_ERRNO(LOG_ERROR, "cannot open a file");
		return 1;
	}
	fseek(fp, 0, SEEK_SET);
	fwrite(file_contents , num_of_bytes, sizeof(char), fp);
	fclose(fp);

	//now that we have a full file we can start doing shit with it
	png_external_context_s* decoded_png = decode_from_png(file_contents, num_of_bytes);
	if(decoded_png == NULL) {
		LOG(LOG_ERROR, "failure decoding png");
	}

	/*arena_s arena;*/
	/*arena_init(&arena);*/

	logger_close();
	return 0;
}

#undef FILE_PATH_PNG
