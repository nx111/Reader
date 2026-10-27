#ifndef __CACHE_H__
#define __CACHE_H__

#include "types.h"
#include "BookmarkData.h"
#include <map>
#include <string>

class Cache
{
public:
    Cache(const TCHAR* file);
    ~Cache(void);

    BOOL init();
    BOOL exit();
    BOOL save();
    header_t* get_header();
    item_t* get_item(int row); // enumeration only
    item_t* get_by_id(int id);
    item_t* open_item(int item_id);
    item_t* new_item(const TCHAR* file_name, const TCHAR* key = NULL);
    item_t* find_item(const TCHAR* file_name, const TCHAR* key = NULL);
    BOOL relink_item(int id, const TCHAR* file_name, const TCHAR* key = NULL);
    static BOOL canonical_path(const TCHAR* path, TCHAR* full);
    static void file_key(const TCHAR* path, TCHAR* key);
    BOOL delete_item(int item_id);
    int delete_items(const std::vector<int>& ids);
    BOOL delete_all_item(void);
    header_t* default_header();
    BOOL add_mark(item_t *item, int value, const bookmark_detail_t* detail = NULL);
    bookmark_detail_t mark_detail(const item_t* item, int index) const;
    BOOL set_mark_note(item_t* item, int index, const std::wstring& note);
    BOOL del_mark(item_t *item, int index);
    void rebase_marks(item_t* item, int start, int delta);

private:
    void default_header(header_t* header);
    BOOL move_item(int from, int to);
    BOOL read(void **data, int *size);
    BOOL write(void *data, int size);
    void update_addr(void);
    void rebuild_indexes();
    void encode(void *data, int size);
    void decode(void* data, int size);

private:
    TCHAR m_file_name[MAX_PATH];
    void* m_buffer;
    int   m_size;
    int   m_capacity;
    void* m_jsonbak;
    int   m_jsonlen;
    BOOL  m_needsMigration;
    int   m_loadedSchema;
    std::map<std::wstring, int> m_paths, m_keys;
    std::map<int, int> m_rows;
    bookmark_store_t m_bookmarks;
};

#endif
