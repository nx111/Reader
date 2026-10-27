#include "framework.h"
#include "Cache.h"
#include "Keyset.h"
#include "Upgrade.h"
#include "Jsondata.h"
#include "DPIAwareness.h"
#include "LibraryModel.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <shlwapi.h>

extern VOID GetCacheVersion(TCHAR*);

Cache::Cache(const TCHAR* file)
    : m_buffer(NULL), m_size(0), m_capacity(0), m_jsonbak(NULL), m_jsonlen(0), m_needsMigration(FALSE)
{
    m_file_name[0] = 0;
    GetModuleFileName(NULL, m_file_name, MAX_PATH);
    TCHAR* base = _tcsrchr(m_file_name, _T('\\'));
    if (base && (base - m_file_name) + 1 + _tcslen(file) < MAX_PATH) _tcscpy(base + 1, file);
}

Cache::~Cache() { free(m_buffer); free(m_jsonbak); }

BOOL Cache::init()
{
    if (!PathFileExists(m_file_name)) return default_header() != NULL;
    void* encoded = NULL;
    int length = 0;
    if (!read(&encoded, &length)) return FALSE;
    char* decoded = (char*)malloc(length + 1);
    header_t* defaults = (header_t*)calloc(1, sizeof(header_t));
    if (!decoded || !defaults) { free(encoded); free(decoded); free(defaults); return FALSE; }
    memcpy(decoded, encoded, length);
    decode(decoded, length); decoded[length] = 0;
    defaults->item_id = -1;
    default_header(defaults);
    int schema = 0;
    const BOOL ok = !memchr(decoded, 0, length)
        && parser_json(decoded, defaults, &m_buffer, &m_size, &schema);
    free(decoded); free(defaults);
    if (!ok) { free(encoded); return FALSE; } // Never replace a corrupt/future cache with defaults.
    m_capacity = m_size;
    m_jsonbak = encoded; m_jsonlen = length;
    m_needsMigration = schema < CACHE_SCHEMA_VERSION;
    rebuild_indexes();
    return TRUE;
}

BOOL Cache::exit()
{
    const BOOL ok = !m_buffer || save();
    free(m_buffer); m_buffer = NULL; m_size = m_capacity = 0;
    free(m_jsonbak); m_jsonbak = NULL; m_jsonlen = 0;
    return ok;
}

BOOL Cache::save()
{
    char* json = create_json(get_header());
    if (!json) return FALSE;
    const int size = (int)strlen(json);
    if (size > 128 * 1024 * 1024) { create_json_free(json); return FALSE; }
    encode(json, size);
    BOOL ok = TRUE;
    if (size != m_jsonlen || !m_jsonbak || memcmp(m_jsonbak, json, size) != 0)
    {
        ok = write(json, size);
        if (ok)
        {
            // Update the snapshot only after durable replacement. A failed write must be retried.
            free(m_jsonbak); m_jsonbak = json; m_jsonlen = size;
            json = NULL; // Transfer the encoded buffer; no fallible allocation after commit.
            m_needsMigration = FALSE;
        }
    }
    create_json_free(json);
    return ok;
}

header_t* Cache::get_header() { return (header_t*)m_buffer; }

item_t* Cache::get_item(int row)
{
    header_t* header = get_header();
    if (!header || row < 0 || row >= header->item_count) return NULL;
    return (item_t*)((char*)m_buffer + sizeof(header_t) + row * sizeof(item_t));
}

item_t* Cache::get_by_id(int id)
{
    std::map<int, int>::const_iterator it = m_rows.find(id);
    return it == m_rows.end() ? NULL : get_item(it->second);
}

void Cache::rebuild_indexes()
{
    m_paths.clear(); m_keys.clear(); m_rows.clear();
    for (int i = 0; get_header() && i < get_header()->item_count; ++i)
    {
        item_t* item = get_item(i);
        TCHAR full[MAX_PATH];
        if (canonical_path(item->file_name, full)) m_paths[LibraryFold(full)] = item->id;
        if (item->file_key[0]) m_keys[item->file_key] = item->id;
        m_rows[item->id] = i;
    }
}

BOOL Cache::canonical_path(const TCHAR* path, TCHAR* full)
{
    if (!path || !*path || _tcslen(path) >= MAX_PATH) return FALSE;
    const DWORD length = GetFullPathName(path, MAX_PATH, full, NULL);
    if (!length || length >= MAX_PATH) return FALSE;
    for (TCHAR* p = full; *p; ++p) if (*p == _T('/')) *p = _T('\\');
    return TRUE;
}

void Cache::file_key(const TCHAR* path, TCHAR* key)
{
    key[0] = 0;
    HANDLE file = CreateFile(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(file, &info) && (info.nFileIndexHigh || info.nFileIndexLow))
        _stprintf(key, _T("%08lx:%08lx%08lx"), info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow);
    CloseHandle(file);
}

item_t* Cache::find_item(const TCHAR* file_name, const TCHAR* key)
{
    TCHAR full[MAX_PATH];
    if (!canonical_path(file_name, full)) return NULL;
    std::map<std::wstring, int>::const_iterator found = m_paths.find(LibraryFold(full));
    if (found != m_paths.end()) return get_by_id(found->second);
    if (key && *key)
    {
        found = m_keys.find(key);
        if (found != m_keys.end()) return get_by_id(found->second);
    }
    return NULL;
}

item_t* Cache::new_item(const TCHAR* file_name, const TCHAR* key)
{
    TCHAR full[MAX_PATH];
    if (!canonical_path(file_name, full)) return NULL;
    item_t* existing = find_item(full, key);
    if (existing)
    {
        if (key && *key && _tcscmp(existing->file_key, key) != 0)
        {
            if (existing->file_key[0]) m_keys.erase(existing->file_key);
            _tcsncpy(existing->file_key, key, 63); existing->file_key[63] = 0;
            m_keys[existing->file_key] = existing->id;
        }
        return existing;
    }
    header_t* header = get_header();
    if (!header || header->item_count >= MAX_LIBRARY_ITEMS || header->next_item_id == INT_MAX) return NULL;
    if (m_size + (int)sizeof(item_t) > m_capacity)
    {
        int capacity = (std::max)(m_size + (int)sizeof(item_t), m_capacity * 2);
        void* buffer = realloc(m_buffer, capacity);
        if (!buffer) return NULL;
        m_buffer = buffer; m_capacity = capacity;
        update_addr();
    }
    m_size += sizeof(item_t);
    header = get_header();
    const int row = header->item_count++;
    item_t* item = get_item(row);
    memset(item, 0, sizeof(item_t));
    item->id = header->next_item_id++;
    _tcscpy(item->file_name, full);
    _tcsncpy(item->title, PathFindFileName(full), 255);
    PathRemoveExtension(item->title);
    if (key) _tcsncpy(item->file_key, key, 63);
    m_rows[item->id] = row;
    m_paths[LibraryFold(full)] = item->id;
    if (item->file_key[0]) m_keys[item->file_key] = item->id;
    return item;
}

item_t* Cache::open_item(int id)
{
    item_t* item = get_by_id(id);
    if (!item) return NULL;
    const int row = m_rows[id];
    move_item(row, 0);
    get_header()->item_id = id;
    return get_by_id(id);
}

BOOL Cache::delete_item(int id)
{
    item_t* item = get_by_id(id);
    if (!item) return FALSE;
    const int row = m_rows[id];
    const int remaining = get_header()->item_count - row - 1;
    if (remaining) memmove(item, item + 1, remaining * sizeof(item_t));
    --get_header()->item_count; m_size -= sizeof(item_t);
    if (get_header()->item_id == id) get_header()->item_id = -1;
    rebuild_indexes();
    return TRUE;
}

BOOL Cache::delete_all_item()
{
    if (!get_header()) return FALSE;
    get_header()->item_count = 0; get_header()->item_id = -1;
    m_size = sizeof(header_t);
    rebuild_indexes(); // next_item_id deliberately survives clearing.
    return TRUE;
}

BOOL Cache::relink_item(int id, const TCHAR* file_name, const TCHAR* key)
{
    TCHAR full[MAX_PATH];
    item_t* source = get_by_id(id);
    if (!source || !canonical_path(file_name, full)) return FALSE;
    item_t merged = *source;
    item_t* other = find_item(full, key);
    int duplicate = other && other->id != id ? other->id : -1;
    if (duplicate >= 0)
    {
        // Keep the missing book's identity/anchor. Preserve the other anchor as a mark.
        for (int i = 0; i <= other->mark_size; ++i)
        {
            const int mark = i == other->mark_size ? other->index : other->mark[i];
            if (mark == 0 || mark == merged.index) continue;
            bool found = false;
            for (int j = 0; j < merged.mark_size; ++j) if (merged.mark[j] == mark) found = true;
            if (!found)
            {
                if (merged.mark_size == MAX_MARK_COUNT) return FALSE; // Do not drop any marks.
                merged.mark[merged.mark_size++] = mark;
            }
        }
        merged.favorite |= other->favorite;
        if (!merged.author[0]) _tcscpy(merged.author, other->author);
        if (!merged.text_length) merged.text_length = other->text_length;
        if (other->last_read > merged.last_read) merged.last_read = other->last_read;
        if (get_header()->item_id == duplicate) get_header()->item_id = id;
        delete_item(duplicate);
    }
    _tcscpy(merged.file_name, full);
    merged.file_key[0] = 0;
    if (key) _tcsncpy(merged.file_key, key, 63);
    *get_by_id(id) = merged;
    rebuild_indexes();
    return TRUE;
}

header_t* Cache::default_header()
{
    header_t* header = NULL;

    if (!m_buffer)
    {
        m_size = sizeof(header_t);
        m_buffer = malloc(m_size);
        m_capacity = m_size;
        if (!m_buffer)
            return NULL;
        memset(m_buffer, 0, m_size);
        header = (header_t*)m_buffer;
        header->item_id = -1;
    }
    else
    {
        header = (header_t*)m_buffer;
    }

    default_header(header);	
    return header;
}

void Cache::default_header(header_t* header)
{
    header->cache_schema = CACHE_SCHEMA_VERSION;
    if (header->next_item_id < 1) header->next_item_id = 1;
    // default font
    static HFONT hFont = NULL;
    static LOGFONT lf;
    int i;
    const int PointSize = 14;
    int nHeight;
    if (!hFont)
    {
        nHeight = -MulDiv(PointSize, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
        hFont = CreateFont(nHeight, 0, 0, 0,
            FW_LIGHT, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_STROKE_PRECIS,
            CLIP_STROKE_PRECIS, PROOF_QUALITY,
            VARIABLE_PITCH | FF_SWISS, _T("Segoe UI Light"));
        GetObject(hFont, sizeof(lf), &lf);
    }
    memcpy(&header->font, &lf, sizeof(lf));
    header->font_color = 0x00;      // black
    memcpy(&header->font_title, &lf, sizeof(lf));
    header->font_color_title = 0x00;  // black
    header->use_same_font = 1;

    // default style
    header->style = WS_OVERLAPPEDWINDOW;
    header->exstyle = WS_EX_ACCEPTFILES | WS_EX_LAYERED;
    header->fs_style = WS_OVERLAPPEDWINDOW;
    header->fs_exstyle = WS_EX_ACCEPTFILES | WS_EX_LAYERED;

    // default placement
    header->placement.length = sizeof(WINDOWPLACEMENT);
    header->placement.flags = 0;
    header->placement.showCmd = SW_SHOWNORMAL;
    header->placement.ptMinPosition.x = -1;
    header->placement.ptMinPosition.y = -1;
    header->placement.ptMaxPosition.x = -1;
    header->placement.ptMaxPosition.y = -1;
    header->placement.rcNormalPosition.left = (GetCxScreenForDpi() - DEFAULT_APP_WIDTH) / 2;
    header->placement.rcNormalPosition.top = (GetCyScreenForDpi() - DEFAULT_APP_HEIGHT) / 2;
    header->placement.rcNormalPosition.right = header->placement.rcNormalPosition.left + DEFAULT_APP_WIDTH;
    header->placement.rcNormalPosition.bottom = header->placement.rcNormalPosition.top + DEFAULT_APP_HEIGHT;
#ifdef _MAC
    header->placement.rcDevice.left = 0;
    header->placement.rcDevice.top = 0;
    header->placement.rcDevice.right = GetCxScreenForDpi();
    header->placement.rcDevice.bottom = GetCyScreenForDpi();
#endif

    header->fs_placement.length = sizeof(WINDOWPLACEMENT);
    header->fs_placement.flags = 0;
    header->fs_placement.showCmd = SW_SHOWNORMAL;
    header->fs_placement.ptMinPosition.x = -1;
    header->fs_placement.ptMinPosition.y = -1;
    header->fs_placement.ptMaxPosition.x = -1;
    header->fs_placement.ptMaxPosition.y = -1;
    header->fs_placement.rcNormalPosition.left = (GetCxScreenForDpi() - DEFAULT_APP_WIDTH) / 2;
    header->fs_placement.rcNormalPosition.top = (GetCyScreenForDpi() - DEFAULT_APP_HEIGHT) / 2;
    header->fs_placement.rcNormalPosition.right = header->fs_placement.rcNormalPosition.left + DEFAULT_APP_WIDTH;
    header->fs_placement.rcNormalPosition.bottom = header->fs_placement.rcNormalPosition.top + DEFAULT_APP_HEIGHT;
#ifdef _MAC
    header->fs_placement.rcDevice.left = 0;
    header->fs_placement.rcDevice.top = 0;
    header->fs_placement.rcDevice.right = GetCxScreenForDpi();
    header->fs_placement.rcDevice.bottom = GetCyScreenForDpi();
#endif

    // default bk color
    header->bg_color = 0x00ffffff;  // White
    header->alpha = 0xff;

    header->char_gap = 0;
    header->line_gap = 5;
    header->paragraph_gap = 15;
    header->left_line_count = 0;
    header->internal_border = { 0 };
    GetCacheVersion(header->version);

    // default others
    header->wheel_speed = ws_single_line;
    header->page_mode = 1;
    header->autopage_mode = 0;
    header->uElapse = 3000;

    header->bg_image.enable = 0;
    header->disable_lrhide = 1;
    header->disable_eschide = 1;

    header->show_systray = 0;
    header->hide_taskbar = 0;
    header->word_wrap = 0;
    header->line_indent = 1;
    header->blank_lines = 1;
    header->chapter_page = 0;

    // default keyset
    header->global_key = 0;
    KS_GetDefaultKeyset(header->keyset);

    // default chapter rule
    header->chapter_rule.rule = 0;

    header->meun_font_follow = 0;

    for (i = 0; i<MAX_CUST_COLOR_COUNT; i++)
    {
        header->cust_colors[i] = 0x00FFFFFF;
    }
    // default tags
#if ENABLE_TAG
    for (i = 0; i < MAX_TAG_COUNT; i++)
    {
        if (header->tags[i].font.lfHeight == 0)
        {
            memcpy(&header->tags[i].font, &lf, sizeof(lf));
        }
        if (header->tags[i].bg_color == 0)
        {
            header->tags[i].bg_color = 0x00FFFFFF;
        }
        //header->tags[i].enable = 0;
    }
#endif
}

BOOL Cache::add_mark(item_t *item, int value)
{
    int i;

    if (!item)
        return FALSE;
    if (item->mark_size >= MAX_MARK_COUNT)
        return FALSE;
    for (i=0; i<item->mark_size; i++)
    {
        if (value == item->mark[i])
            return FALSE;
    }
    item->mark[item->mark_size] = value;
    item->mark_size++;
    return TRUE;
}

BOOL Cache::del_mark(item_t *item, int index)
{
    if (!item)
        return FALSE;
    if (item->mark_size <= 0)
        return FALSE;
    if (index < 0 || index >= item->mark_size)
        return FALSE;

    // delete
    if (item->mark_size - index - 1 > 0)
    {
        memmove(item->mark+index, item->mark+index+1, (item->mark_size-index-1)*sizeof(int));
    }
    item->mark_size--;
    item->mark[item->mark_size] = 0;
    return TRUE;
}

BOOL Cache::move_item(int from, int to)
{
    item_t* first = get_item(from);
    item_t* last = get_item(to);
    if (!first || !last) return FALSE;
    if (from == to) return TRUE;
    item_t temp = *first;
    if (from > to) memmove(last + 1, last, (from - to) * sizeof(item_t));
    else memmove(first, first + 1, (to - from) * sizeof(item_t));
    *last = temp;
    rebuild_indexes();
    return TRUE;
}

BOOL Cache::read(void** data, int* size)
{
    *data = NULL; *size = 0;
    HANDLE file = CreateFile(m_file_name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER length;
    BOOL ok = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 128 * 1024 * 1024;
    DWORD readBytes = 0;
    if (ok)
    {
        *size = (int)length.QuadPart;
        *data = malloc(*size);
        ok = *data && ReadFile(file, *data, *size, &readBytes, NULL) && readBytes == (DWORD)*size;
    }
    CloseHandle(file);
    if (!ok) { free(*data); *data = NULL; *size = 0; }
    return ok;
}

BOOL Cache::write(void* data, int size)
{
    TCHAR temp[MAX_PATH + 16], backup[MAX_PATH + 16], backupTemp[MAX_PATH + 20], legacy[MAX_PATH + 16];
    _stprintf(temp, _T("%s.tmp"), m_file_name);
    _stprintf(backup, _T("%s.bak"), m_file_name);
    _stprintf(backupTemp, _T("%s.bak.tmp"), m_file_name);
    _stprintf(legacy, _T("%s.v1.bak"), m_file_name);
    HANDLE file = CreateFile(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, NULL) && written == (DWORD)size && FlushFileBuffers(file);
    if (!CloseHandle(file)) ok = FALSE;
    if (ok && PathFileExists(m_file_name))
    {
        // A second writer must not silently replace a cache changed since our load.
        void* current = NULL; int currentSize = 0;
        ok = read(&current, &currentSize) && currentSize == m_jsonlen && m_jsonbak
            && memcmp(current, m_jsonbak, currentSize) == 0;
        free(current);
        if (ok && m_needsMigration && !PathFileExists(legacy)) ok = CopyFile(m_file_name, legacy, TRUE);
        if (ok)
        {
            if (PathFileExists(backupTemp)) DeleteFile(backupTemp);
            ok = CopyFile(m_file_name, backupTemp, FALSE)
                && MoveFileEx(backupTemp, backup, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
    if (ok) ok = MoveFileEx(temp, m_file_name, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) { DeleteFile(temp); DeleteFile(backupTemp); }
    else SetFileAttributes(m_file_name, FILE_ATTRIBUTE_HIDDEN);
    return ok;
}

void Cache::update_addr() { if (get_header()) KS_UpdateKeyset(get_header()->keyset); }
void Cache::encode(void* data, int size)
{
    unsigned char* bytes = (unsigned char*)data;
    for (int i = 0; i < size; ++i) bytes[i] ^= 0x3a;
}
void Cache::decode(void* data, int size) { encode(data, size); }
