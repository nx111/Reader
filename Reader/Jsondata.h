#ifndef __JSON_DATA_H__
#define __JSON_DATA_H__

#include "types.h"
#include "BookmarkData.h"

char* create_json(header_t* data, const bookmark_store_t* bookmarks = NULL);
void create_json_free(char* json);
BOOL parser_json(const char* json, header_t* defhdr, void **data, int *size, int *source_schema = NULL, bookmark_store_t* bookmarks = NULL);

BOOL import_book_source(const char *json, book_source_t *bs, int *count);
BOOL export_book_source(const book_source_t *bs, int count, char **json);
void export_book_source_free(char* json);

#endif