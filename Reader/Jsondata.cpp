#include "cJSON.h"
#include "Jsondata.h"
#include "Keyset.h"
#include "Utils.h"
#include <stdio.h>
#include <set>
#include <limits.h>
#include <math.h>

// Bound copies even when an older settings file contains unexpected strings.
template<size_t N> static void JsonCopy(wchar_t (&out)[N], const char* value)
{
    if (!value) return;
    const wchar_t* wide = Utf8ToUtf16(value);
    if (wide) { wcsncpy(out, wide, N - 1); out[N - 1] = 0; }
}
template<size_t N> static void JsonCopy(char (&out)[N], const char* value)
{
    if (value) { strncpy(out, value, N - 1); out[N - 1] = 0; }
}



cJSON* cJSON_AddULongToObject(cJSON* const object, const char* const name, const u32 number)
{
    cJSON* item;
    int n;
    n = *((int*)&number);
    item = cJSON_AddNumberToObject(object, name, n);
    return item;
}

class json_point
{
    cJSON* x;
    cJSON* y;

public:
    json_point(cJSON* parent, POINT* data)
    {
        x = cJSON_AddNumberToObject(parent, "x", data->x);
        y = cJSON_AddNumberToObject(parent, "y", data->y);
    }
    json_point(cJSON* parent) // for json parser
    {
        x = cJSON_GetObjectItem(parent, "x");
        y = cJSON_GetObjectItem(parent, "y");
    }
    void GetData(POINT* data)
    {
        if (x)
            data->x = x->valueint;
        if (y)
            data->y = y->valueint;
    }
};

class json_rect_t
{
    cJSON* left;
    cJSON* top;
    cJSON* right;
    cJSON* bottom;

public:
    json_rect_t(cJSON* parent, RECT* data)
    {
        left = cJSON_AddNumberToObject(parent, "left", data->left);
        top = cJSON_AddNumberToObject(parent, "top", data->top);
        right = cJSON_AddNumberToObject(parent, "right", data->right);
        bottom = cJSON_AddNumberToObject(parent, "bottom", data->bottom);
    }
    json_rect_t(cJSON* parent) // for json parser
    {
        left = cJSON_GetObjectItem(parent, "left");
        top = cJSON_GetObjectItem(parent, "top");
        right = cJSON_GetObjectItem(parent, "right");
        bottom = cJSON_GetObjectItem(parent, "bottom");
    }
    void GetData(RECT* data)
    {
        if (left)
            data->left = left->valueint;
        if (top)
            data->top = top->valueint;
        if (right)
            data->right = right->valueint;
        if (bottom)
            data->bottom = bottom->valueint;
    }
};

class json_placement_t
{
    cJSON* length;
    cJSON* flags;
    cJSON* showCmd;
    json_point* ptMinPosition = NULL;
    json_point* ptMaxPosition = NULL;
    json_rect_t* rcNormalPosition = NULL;
#ifdef _MAC
    json_rect_t* rcDevice = NULL;
#endif

public:
    json_placement_t(cJSON* parent, WINDOWPLACEMENT* data)
    {
        cJSON* item;

        length = cJSON_AddULongToObject(parent, "length", data->length);
        flags = cJSON_AddULongToObject(parent, "flags", data->flags);
        showCmd = cJSON_AddULongToObject(parent, "showCmd", data->showCmd);

        item = cJSON_CreateObject();
        ptMinPosition = new json_point(item, &(data->ptMinPosition));
        cJSON_AddItemToObject(parent, "ptMinPosition", item);

        item = cJSON_CreateObject();
        ptMaxPosition = new json_point(item, &(data->ptMaxPosition));
        cJSON_AddItemToObject(parent, "ptMaxPosition", item);

        item = cJSON_CreateObject();
        rcNormalPosition = new json_rect_t(item, &(data->rcNormalPosition));
        cJSON_AddItemToObject(parent, "rcNormalPosition", item);

#ifdef _MAC
        item = cJSON_CreateObject();
        rcDevice = new json_rect_t(item, &(data->rcDevice));
        cJSON_AddItemToObject(parent, "rcDevice", item);
#endif
    }
    json_placement_t(cJSON* parent) // for json parser
    {
        cJSON* item;

        length = cJSON_GetObjectItem(parent, "length");
        flags = cJSON_GetObjectItem(parent, "flags");
        showCmd = cJSON_GetObjectItem(parent, "showCmd");

        item = cJSON_GetObjectItem(parent, "ptMinPosition");
        if (item)
            ptMinPosition = new json_point(item);

        item = cJSON_GetObjectItem(parent, "ptMaxPosition");
        if (item)
            ptMaxPosition = new json_point(item);

        item = cJSON_GetObjectItem(parent, "rcNormalPosition");
        if (item)
            rcNormalPosition = new json_rect_t(item);

#ifdef _MAC
        item = cJSON_GetObjectItem(parent, "rcDevice");
        if (item)
            rcDevice = new json_rect_t(item);
#endif
    }
    ~json_placement_t()
    {
        if (ptMinPosition)
            delete ptMinPosition;
        if (ptMaxPosition)
            delete ptMaxPosition;
        if (rcNormalPosition)
            delete rcNormalPosition;
#ifdef _MAC
        if (rcDevice)
            delete rcDevice;
#endif
    }
    void GetData(WINDOWPLACEMENT* data)
    {
        if (length)
            data->length = *((u32*)&length->valueint);
        if (flags)
            data->flags = *((u32*)&flags->valueint);
        if (showCmd)
            data->showCmd = *((u32*)&showCmd->valueint);

        if (ptMinPosition)
            ptMinPosition->GetData(&(data->ptMinPosition));

        if (ptMaxPosition)
            ptMaxPosition->GetData(&(data->ptMaxPosition));

        if (rcNormalPosition)
            rcNormalPosition->GetData(&(data->rcNormalPosition));

#ifdef _MAC
        if (rcDevice)
            rcDevice->GetData(&(data->rcDevice));
#endif
    }
};

class json_font_t
{
    cJSON* lfHeight;
    cJSON* lfWidth;
    cJSON* lfEscapement;
    cJSON* lfOrientation;
    cJSON* lfWeight;
    cJSON* lfItalic;
    cJSON* lfUnderline;
    cJSON* lfStrikeOut;
    cJSON* lfCharSet;
    cJSON* lfOutPrecision;
    cJSON* lfClipPrecision;
    cJSON* lfQuality;
    cJSON* lfPitchAndFamily;
    cJSON* lfFaceName;
public:
    json_font_t(cJSON* parent, LOGFONT* data)
    {
        lfHeight = cJSON_AddNumberToObject(parent, "lfHeight", data->lfHeight);
        lfWidth = cJSON_AddNumberToObject(parent, "lfWidth", data->lfWidth);
        lfEscapement = cJSON_AddNumberToObject(parent, "lfEscapement", data->lfEscapement);
        lfOrientation = cJSON_AddNumberToObject(parent, "lfOrientation", data->lfOrientation);
        lfWeight = cJSON_AddNumberToObject(parent, "lfWeight", data->lfWeight);
        lfItalic = cJSON_AddNumberToObject(parent, "lfItalic", data->lfItalic);
        lfUnderline = cJSON_AddNumberToObject(parent, "lfUnderline", data->lfUnderline);
        lfStrikeOut = cJSON_AddNumberToObject(parent, "lfStrikeOut", data->lfStrikeOut);
        lfCharSet = cJSON_AddNumberToObject(parent, "lfCharSet", data->lfCharSet);
        lfOutPrecision = cJSON_AddNumberToObject(parent, "lfOutPrecision", data->lfOutPrecision);
        lfClipPrecision = cJSON_AddNumberToObject(parent, "lfClipPrecision", data->lfClipPrecision);
        lfQuality = cJSON_AddNumberToObject(parent, "lfQuality", data->lfQuality);
        lfPitchAndFamily = cJSON_AddNumberToObject(parent, "lfPitchAndFamily", data->lfPitchAndFamily);
        lfFaceName = cJSON_AddStringToObject(parent, "lfFaceName", Utf16ToUtf8(data->lfFaceName));
    }
    json_font_t(cJSON* parent) // for json parser
    {
        lfHeight = cJSON_GetObjectItem(parent, "lfHeight");
        lfWidth = cJSON_GetObjectItem(parent, "lfWidth");
        lfEscapement = cJSON_GetObjectItem(parent, "lfEscapement");
        lfOrientation = cJSON_GetObjectItem(parent, "lfOrientation");
        lfWeight = cJSON_GetObjectItem(parent, "lfWeight");
        lfItalic = cJSON_GetObjectItem(parent, "lfItalic");
        lfUnderline = cJSON_GetObjectItem(parent, "lfUnderline");
        lfStrikeOut = cJSON_GetObjectItem(parent, "lfStrikeOut");
        lfCharSet = cJSON_GetObjectItem(parent, "lfCharSet");
        lfOutPrecision = cJSON_GetObjectItem(parent, "lfOutPrecision");
        lfClipPrecision = cJSON_GetObjectItem(parent, "lfClipPrecision");
        lfQuality = cJSON_GetObjectItem(parent, "lfQuality");
        lfPitchAndFamily = cJSON_GetObjectItem(parent, "lfPitchAndFamily");
        lfFaceName = cJSON_GetObjectItem(parent, "lfFaceName");
    }
    void GetData(LOGFONT* data)
    {
        if (lfHeight)
            data->lfHeight = lfHeight->valueint;
        if (lfWidth)
            data->lfWidth = lfWidth->valueint;
        if (lfEscapement)
            data->lfEscapement = lfEscapement->valueint;
        if (lfOrientation)
            data->lfOrientation = lfOrientation->valueint;
        if (lfWeight)
            data->lfWeight = lfWeight->valueint;
        if (lfItalic)
            data->lfItalic = (BYTE)lfItalic->valueint;
        if (lfUnderline)
            data->lfUnderline = (BYTE)lfUnderline->valueint;
        if (lfStrikeOut)
            data->lfStrikeOut = (BYTE)lfStrikeOut->valueint;
        if (lfCharSet)
            data->lfCharSet = (BYTE)lfCharSet->valueint;
        if (lfOutPrecision)
            data->lfOutPrecision = (BYTE)lfOutPrecision->valueint;
        if (lfClipPrecision)
            data->lfClipPrecision = (BYTE)lfClipPrecision->valueint;
        if (lfQuality)
            data->lfQuality = (BYTE)lfQuality->valueint;
        if (lfPitchAndFamily)
            data->lfPitchAndFamily = (BYTE)lfPitchAndFamily->valueint;
        if (lfFaceName)
            JsonCopy(data->lfFaceName, lfFaceName->valuestring);
    }
};

class json_bg_image_t
{
    cJSON* enable;
    cJSON* mode;
    cJSON* file_name;
public:
    json_bg_image_t(cJSON* parent, bg_image_t* data)
    {
        enable = cJSON_AddNumberToObject(parent, "enable", data->enable);
        mode = cJSON_AddNumberToObject(parent, "mode", data->mode);
        file_name = cJSON_AddStringToObject(parent, "file_name", Utf16ToUtf8(data->file_name));
    }
    json_bg_image_t(cJSON* parent) // for json parser
    {
        enable = cJSON_GetObjectItem(parent, "enable");
        mode = cJSON_GetObjectItem(parent, "mode");
        file_name = cJSON_GetObjectItem(parent, "file_name");
    }
    void GetData(bg_image_t* data)
    {
        if (enable)
            data->enable = enable->valueint;
        if (mode)
            data->mode = mode->valueint;
        if (file_name)
            JsonCopy(data->file_name, file_name->valuestring);
    }
};

class json_proxy_t
{
    cJSON* enable;
    cJSON* addr;
    cJSON* port;
    cJSON* user;
    cJSON* pass;
public:
    json_proxy_t(cJSON* parent, proxy_t* data)
    {
        enable = cJSON_AddNumberToObject(parent, "enable", data->enable);
        port = cJSON_AddNumberToObject(parent, "port", data->port);
        addr = cJSON_AddStringToObject(parent, "addr", Utf16ToUtf8(data->addr));
        user = cJSON_AddStringToObject(parent, "user", Utf16ToUtf8(data->user));
        pass = cJSON_AddStringToObject(parent, "pass", Utf16ToUtf8(data->pass));
    }
    json_proxy_t(cJSON* parent) // for json parser
    {
        enable = cJSON_GetObjectItem(parent, "enable");
        port = cJSON_GetObjectItem(parent, "port");
        addr = cJSON_GetObjectItem(parent, "addr");
        user = cJSON_GetObjectItem(parent, "user");
        pass = cJSON_GetObjectItem(parent, "pass");
    }
    void GetData(proxy_t* data)
    {
        if (enable)
            data->enable = enable->valueint;
        if (port)
            data->port = port->valueint;
        if (addr)
            JsonCopy(data->addr, addr->valuestring);
        if (user)
            JsonCopy(data->user, user->valuestring);
        if (pass)
            JsonCopy(data->pass, pass->valuestring);
    }
};

class json_keyset_t
{
    cJSON* value;
    cJSON* is_disable;
public:
    json_keyset_t(cJSON* parent, keyset_t* data)
    {
        value = cJSON_AddULongToObject(parent, "value", data->value);
        is_disable = cJSON_AddNumberToObject(parent, "is_disable", data->is_disable);
    }
    json_keyset_t(cJSON* parent) // for json parser
    {
        value = cJSON_GetObjectItem(parent, "value");
        is_disable = cJSON_GetObjectItem(parent, "is_disable");
    }
    void GetData(keyset_t* data)
    {
        if (value)
            data->value = *((u32*)&value->valueint);
        if (is_disable)
            data->is_disable = is_disable->valueint;
    }
};

class json_chapter_rule_t
{
    cJSON* rule;
    cJSON* keyword;
    cJSON* regex;
public:
    json_chapter_rule_t(cJSON* parent, chapter_rule_t* data)
    {
        rule = cJSON_AddNumberToObject(parent, "rule", data->rule);
        keyword = cJSON_AddStringToObject(parent, "keyword", Utf16ToUtf8(data->keyword));
        regex = cJSON_AddStringToObject(parent, "regex", Utf16ToUtf8(data->regex));
    }
    json_chapter_rule_t(cJSON* parent) // for json parser
    {
        rule = cJSON_GetObjectItem(parent, "rule");
        keyword = cJSON_GetObjectItem(parent, "keyword");
        regex = cJSON_GetObjectItem(parent, "regex");
    }
    void GetData(chapter_rule_t* data)
    {
        if (rule)
            data->rule = rule->valueint;
        if (keyword)
            JsonCopy(data->keyword, keyword->valuestring);
        if (regex)
            JsonCopy(data->regex, regex->valuestring);
    }
};

#if ENABLE_TAG
class json_tagitem_t
{
    cJSON* enable;
    json_font_t* font = NULL;
    cJSON* font_color;
    cJSON* bg_color;
    cJSON* keyword;
public:
    json_tagitem_t(cJSON* parent, tagitem_t* data)
    {
        cJSON* item;
        item = cJSON_CreateObject();
        enable = cJSON_AddNumberToObject(parent, "enable", data->enable);
        font_color = cJSON_AddULongToObject(parent, "font_color", data->font_color);
        bg_color = cJSON_AddULongToObject(parent, "bg_color", data->bg_color);
        keyword = cJSON_AddStringToObject(parent, "keyword", Utf16ToUtf8(data->keyword));
        font = new json_font_t(item, &(data->font));
        cJSON_AddItemToObject(parent, "font", item);
    }
    json_tagitem_t(cJSON* parent) // for json parser
        : font(NULL)
    {
        cJSON* item;
        item = cJSON_GetObjectItem(parent, "font");
        enable = cJSON_GetObjectItem(parent, "enable");
        font_color = cJSON_GetObjectItem(parent, "font_color");
        bg_color = cJSON_GetObjectItem(parent, "bg_color");
        keyword = cJSON_GetObjectItem(parent, "keyword");
        if (item)
            font = new json_font_t(item);
    }
    ~json_tagitem_t()
    {
        if (font)
            delete font;
    }
    void GetData(tagitem_t* data)
    {
        if (enable)
            data->enable = enable->valueint;
        if (font_color)
            data->font_color = *((u32*)&font_color->valueint);
        if (bg_color)
            data->bg_color = *((u32*)&bg_color->valueint);
        if (keyword)
            JsonCopy(data->keyword, keyword->valuestring);
        if (font)
            font->GetData(&(data->font));
    }
};
#endif

class json_book_source_t
{
    cJSON* title;
    cJSON* host;
    cJSON* query_url;
    cJSON* query_method;
    cJSON* query_params;
    cJSON* query_charset;
    cJSON* book_name_xpath;
    cJSON* book_mainpage_xpath;
    cJSON* book_author_xpath;
    cJSON* enable_chapter_page;
    cJSON* chapter_page_xpath;
    cJSON* chapter_title_xpath;
    cJSON* chapter_url_xpath;
    cJSON* enable_chapter_next;
    cJSON* chapter_next_url_xpath;
    cJSON* chapter_next_keyword_xpath;
    cJSON* chapter_next_keyword;
    cJSON* content_xpath;
    cJSON* enable_content_next;
    cJSON* content_next_url_xpath;
    cJSON* content_next_keyword_xpath;
    cJSON* content_next_keyword;
    cJSON* content_filter_type;
    cJSON* content_filter_keyword;
public:
    json_book_source_t(cJSON* parent, const book_source_t* data)
    {
        title = cJSON_AddStringToObject(parent, "title", Utf16ToUtf8(data->title));
        host = cJSON_AddStringToObject(parent, "host", data->host);
        query_url = cJSON_AddStringToObject(parent, "query_url", data->query_url);
        query_method = cJSON_AddNumberToObject(parent, "query_method", data->query_method);
        query_params = cJSON_AddStringToObject(parent, "query_params", data->query_params);
        query_charset = cJSON_AddNumberToObject(parent, "query_charset", data->query_charset);
        book_name_xpath = cJSON_AddStringToObject(parent, "book_name_xpath", data->book_name_xpath);
        book_mainpage_xpath = cJSON_AddStringToObject(parent, "book_mainpage_xpath", data->book_mainpage_xpath);
        book_author_xpath = cJSON_AddStringToObject(parent, "book_author_xpath", data->book_author_xpath);
        enable_chapter_page = cJSON_AddNumberToObject(parent, "enable_chapter_page", data->enable_chapter_page);
        chapter_page_xpath = cJSON_AddStringToObject(parent, "chapter_page_xpath", data->chapter_page_xpath);
        chapter_title_xpath = cJSON_AddStringToObject(parent, "chapter_title_xpath", data->chapter_title_xpath);
        chapter_url_xpath = cJSON_AddStringToObject(parent, "chapter_url_xpath", data->chapter_url_xpath);
        enable_chapter_next = cJSON_AddNumberToObject(parent, "enable_chapter_next", data->enable_chapter_next);
        chapter_next_url_xpath = cJSON_AddStringToObject(parent, "chapter_next_url_xpath", data->chapter_next_url_xpath);
        chapter_next_keyword_xpath = cJSON_AddStringToObject(parent, "chapter_next_keyword_xpath", data->chapter_next_keyword_xpath);
        chapter_next_keyword = cJSON_AddStringToObject(parent, "chapter_next_keyword", data->chapter_next_keyword);
        content_xpath = cJSON_AddStringToObject(parent, "content_xpath", data->content_xpath);
        enable_content_next = cJSON_AddNumberToObject(parent, "enable_content_next", data->enable_content_next);
        content_next_url_xpath = cJSON_AddStringToObject(parent, "content_next_url_xpath", data->content_next_url_xpath);
        content_next_keyword_xpath = cJSON_AddStringToObject(parent, "content_next_keyword_xpath", data->content_next_keyword_xpath);
        content_next_keyword = cJSON_AddStringToObject(parent, "content_next_keyword", data->content_next_keyword);
        content_filter_type = cJSON_AddNumberToObject(parent, "content_filter_type", data->content_filter_type);
        content_filter_keyword = cJSON_AddStringToObject(parent, "content_filter_keyword", Utf16ToUtf8(data->content_filter_keyword));
    }
    json_book_source_t(cJSON* parent) // for json parser
    {
        title = cJSON_GetObjectItem(parent, "title");
        host = cJSON_GetObjectItem(parent, "host");
        query_url = cJSON_GetObjectItem(parent, "query_url");
        query_method = cJSON_GetObjectItem(parent, "query_method");
        query_params = cJSON_GetObjectItem(parent, "query_params");
        query_charset = cJSON_GetObjectItem(parent, "query_charset");
        book_name_xpath = cJSON_GetObjectItem(parent, "book_name_xpath");
        book_mainpage_xpath = cJSON_GetObjectItem(parent, "book_mainpage_xpath");
        book_author_xpath = cJSON_GetObjectItem(parent, "book_author_xpath");
        enable_chapter_page = cJSON_GetObjectItem(parent, "enable_chapter_page");
        chapter_page_xpath = cJSON_GetObjectItem(parent, "chapter_page_xpath");
        chapter_title_xpath = cJSON_GetObjectItem(parent, "chapter_title_xpath");
        chapter_url_xpath = cJSON_GetObjectItem(parent, "chapter_url_xpath");
        enable_chapter_next = cJSON_GetObjectItem(parent, "enable_chapter_next");
        chapter_next_url_xpath = cJSON_GetObjectItem(parent, "chapter_next_url_xpath");
        chapter_next_keyword_xpath = cJSON_GetObjectItem(parent, "chapter_next_keyword_xpath");
        chapter_next_keyword = cJSON_GetObjectItem(parent, "chapter_next_keyword");
        content_xpath = cJSON_GetObjectItem(parent, "content_xpath");
        enable_content_next = cJSON_GetObjectItem(parent, "enable_content_next");
        content_next_url_xpath = cJSON_GetObjectItem(parent, "content_next_url_xpath");
        content_next_keyword_xpath = cJSON_GetObjectItem(parent, "content_next_keyword_xpath");
        content_next_keyword = cJSON_GetObjectItem(parent, "content_next_keyword");
        content_filter_type = cJSON_GetObjectItem(parent, "content_filter_type");
        content_filter_keyword = cJSON_GetObjectItem(parent, "content_filter_keyword");
    }
    void GetData(book_source_t* data)
    {
        if (title)
            JsonCopy(data->title, title->valuestring);
        if (host)
            JsonCopy(data->host, host->valuestring);
        if (query_url)
            JsonCopy(data->query_url, query_url->valuestring);
        if (query_method)
            data->query_method = query_method->valueint;
        if (query_params)
            JsonCopy(data->query_params, query_params->valuestring);
        if (query_charset)
            data->query_charset = query_charset->valueint;
        if (book_name_xpath)
            JsonCopy(data->book_name_xpath, book_name_xpath->valuestring);
        if (book_mainpage_xpath)
            JsonCopy(data->book_mainpage_xpath, book_mainpage_xpath->valuestring);
        if (book_author_xpath)
            JsonCopy(data->book_author_xpath, book_author_xpath->valuestring);
        if (enable_chapter_page)
            data->enable_chapter_page = enable_chapter_page->valueint;
        if (chapter_page_xpath)
            JsonCopy(data->chapter_page_xpath, chapter_page_xpath->valuestring);
        if (chapter_title_xpath)
            JsonCopy(data->chapter_title_xpath, chapter_title_xpath->valuestring);
        if (chapter_url_xpath)
            JsonCopy(data->chapter_url_xpath, chapter_url_xpath->valuestring);
        if (enable_chapter_next)
            data->enable_chapter_next = enable_chapter_next->valueint;
        if (chapter_next_url_xpath)
            JsonCopy(data->chapter_next_url_xpath, chapter_next_url_xpath->valuestring);
        if (chapter_next_keyword_xpath)
            JsonCopy(data->chapter_next_keyword_xpath, chapter_next_keyword_xpath->valuestring);
        if (chapter_next_keyword)
            JsonCopy(data->chapter_next_keyword, chapter_next_keyword->valuestring);
        if (content_xpath)
            JsonCopy(data->content_xpath, content_xpath->valuestring);
        if (enable_content_next)
            data->enable_content_next = enable_content_next->valueint;
        if (content_next_url_xpath)
            JsonCopy(data->content_next_url_xpath, content_next_url_xpath->valuestring);
        if (content_next_keyword_xpath)
            JsonCopy(data->content_next_keyword_xpath, content_next_keyword_xpath->valuestring);
        if (content_next_keyword)
            JsonCopy(data->content_next_keyword, content_next_keyword->valuestring);
        if (content_filter_type)
            data->content_filter_type = content_filter_type->valueint;
        if (content_filter_keyword)
            JsonCopy(data->content_filter_keyword, content_filter_keyword->valuestring);
    }
};

class json_header_t
{
    cJSON* version;
    cJSON* item_count;
    cJSON* item_id;
    cJSON* style;
    cJSON* exstyle;
    cJSON* fs_style;
    cJSON* fs_exstyle;
    cJSON* char_gap;
    cJSON* line_gap;
    cJSON* paragraph_gap;
    cJSON* left_line_count;
    cJSON* font_color;
    cJSON* font_color_title;
    cJSON* use_same_font;
    cJSON* bg_color;
    cJSON* alpha;
    cJSON* meun_font_follow;
    cJSON* wheel_speed;
    cJSON* page_mode;
    cJSON* autopage_mode;
    cJSON* uElapse;
    cJSON* hide_taskbar;
    cJSON* show_systray;
    cJSON* disable_lrhide;
    cJSON* disable_eschide;
    cJSON* word_wrap;
    cJSON* line_indent;
    cJSON* blank_lines;
    cJSON* chapter_page;
    cJSON* ingore_version;
    cJSON* checkver_time;
    cJSON* global_key;
    cJSON* cust_colors;
    json_keyset_t* keyset[KI_MAXCOUNT];
    json_placement_t* placement = NULL;
    json_placement_t* fs_placement = NULL;
    json_rect_t* fs_rect = NULL;
    json_rect_t* internal_border = NULL;
    json_font_t* font = NULL;
    json_font_t* font_title = NULL;
    json_bg_image_t* bg_image = NULL;
    json_proxy_t* proxy = NULL;
    json_chapter_rule_t* chapter_rule = NULL;
#if ENABLE_TAG
    cJSON* tag_count;
    json_tagitem_t* tags[MAX_TAG_COUNT];
#endif
    cJSON* book_source_count;
    json_book_source_t* book_sources[MAX_BOOKSRC_COUNT];
public:
    json_header_t(cJSON* parent, header_t* data)
    {
        int i, n;
        cJSON* item;
        cJSON* array;

        version = cJSON_AddStringToObject(parent, "version", Utf16ToUtf8(data->version));
        item_count = cJSON_AddNumberToObject(parent, "item_count", data->item_count);
        item_id = cJSON_AddNumberToObject(parent, "item_id", data->item_id);
        style = cJSON_AddULongToObject(parent, "style", data->style);
        exstyle = cJSON_AddULongToObject(parent, "exstyle", data->exstyle);
        fs_style = cJSON_AddULongToObject(parent, "fs_style", data->fs_style);
        fs_exstyle = cJSON_AddULongToObject(parent, "fs_exstyle", data->fs_exstyle);
        char_gap = cJSON_AddNumberToObject(parent, "char_gap", data->char_gap);
        line_gap = cJSON_AddNumberToObject(parent, "line_gap", data->line_gap);
        paragraph_gap = cJSON_AddNumberToObject(parent, "paragraph_gap", data->paragraph_gap);
        left_line_count = cJSON_AddNumberToObject(parent, "left_line_count", data->left_line_count);
        font_color = cJSON_AddULongToObject(parent, "font_color", data->font_color);
        font_color_title = cJSON_AddULongToObject(parent, "font_color_title", data->font_color_title);
        use_same_font = cJSON_AddNumberToObject(parent, "use_same_font", data->use_same_font);
        bg_color = cJSON_AddULongToObject(parent, "bg_color", data->bg_color);
        alpha = cJSON_AddNumberToObject(parent, "alpha", data->alpha);
        meun_font_follow = cJSON_AddNumberToObject(parent, "meun_font_follow", data->meun_font_follow);
        wheel_speed = cJSON_AddNumberToObject(parent, "wheel_speed", data->wheel_speed);
        page_mode = cJSON_AddNumberToObject(parent, "page_mode", data->page_mode);
        autopage_mode = cJSON_AddNumberToObject(parent, "autopage_mode", data->autopage_mode);
        uElapse = cJSON_AddULongToObject(parent, "uElapse", data->uElapse);
        hide_taskbar = cJSON_AddNumberToObject(parent, "hide_taskbar", data->hide_taskbar);
        show_systray = cJSON_AddNumberToObject(parent, "show_systray", data->show_systray);
        disable_lrhide = cJSON_AddNumberToObject(parent, "disable_lrhide", data->disable_lrhide);
        disable_eschide = cJSON_AddNumberToObject(parent, "disable_eschide", data->disable_eschide);
        word_wrap = cJSON_AddNumberToObject(parent, "word_wrap", data->word_wrap);
        line_indent = cJSON_AddNumberToObject(parent, "line_indent", data->line_indent);
        blank_lines = cJSON_AddNumberToObject(parent, "blank_lines", data->blank_lines);
        chapter_page = cJSON_AddNumberToObject(parent, "chapter_page", data->chapter_page);
        ingore_version = cJSON_AddStringToObject(parent, "ingore_version", Utf16ToUtf8(data->ingore_version));
        checkver_time = cJSON_AddULongToObject(parent, "checkver_time", data->checkver_time);

        global_key = cJSON_AddNumberToObject(parent, "global_key", data->global_key);

        cust_colors = cJSON_AddArrayToObject(parent, "cust_colors");
        for (i = 0; i < MAX_CUST_COLOR_COUNT; i++)
        {
            n = *((int*)&data->cust_colors[i]);
            item = cJSON_CreateNumber(n);
            cJSON_AddItemToArray(cust_colors, item);
        }

        memset(keyset, 0, sizeof(json_keyset_t*) * KI_MAXCOUNT);
        array = cJSON_AddArrayToObject(parent, "keyset");
        for (i = KI_HIDE; i < KI_MAXCOUNT; i++)
        {
            item = cJSON_CreateObject();
            keyset[i] = new json_keyset_t(item, &(data->keyset[i]));
            cJSON_AddItemToArray(array, item);
        }

        item = cJSON_CreateObject();
        placement = new json_placement_t(item, &(data->placement));
        cJSON_AddItemToObject(parent, "placement", item);

        item = cJSON_CreateObject();
        fs_placement = new json_placement_t(item, &(data->fs_placement));
        cJSON_AddItemToObject(parent, "fs_placement", item);

        item = cJSON_CreateObject();
        fs_rect = new json_rect_t(item, &(data->fs_rect));
        cJSON_AddItemToObject(parent, "fs_rect", item);

        item = cJSON_CreateObject();
        internal_border = new json_rect_t(item, &(data->internal_border));
        cJSON_AddItemToObject(parent, "internal_border", item);

        item = cJSON_CreateObject();
        font = new json_font_t(item, &(data->font));
        cJSON_AddItemToObject(parent, "font", item);

        item = cJSON_CreateObject();
        font_title = new json_font_t(item, &(data->font_title));
        cJSON_AddItemToObject(parent, "font_title", item);

        item = cJSON_CreateObject();
        bg_image = new json_bg_image_t(item, &(data->bg_image));
        cJSON_AddItemToObject(parent, "bg_image", item);

        item = cJSON_CreateObject();
        proxy = new json_proxy_t(item, &(data->proxy));
        cJSON_AddItemToObject(parent, "proxy", item);

        item = cJSON_CreateObject();
        chapter_rule = new json_chapter_rule_t(item, &(data->chapter_rule));
        cJSON_AddItemToObject(parent, "chapter_rule", item);

#if ENABLE_TAG
        memset(tags, 0, sizeof(json_tagitem_t*) * MAX_TAG_COUNT);
        tag_count = cJSON_AddNumberToObject(parent, "tag_count", data->tag_count);
        array = cJSON_AddArrayToObject(parent, "tags");
        for (i = 0; i < data->tag_count; i++)
        {
            item = cJSON_CreateObject();
            tags[i] = new json_tagitem_t(item, &(data->tags[i]));
            cJSON_AddItemToArray(array, item);
        }
#endif
        memset(book_sources, 0, sizeof(json_book_source_t*) * MAX_BOOKSRC_COUNT);
        book_source_count = cJSON_AddNumberToObject(parent, "book_source_count", data->book_source_count);
        array = cJSON_AddArrayToObject(parent, "book_sources");
        for (i = 0; i < data->book_source_count; i++)
        {
            item = cJSON_CreateObject();
            book_sources[i] = new json_book_source_t(item, &(data->book_sources[i]));
            cJSON_AddItemToArray(array, item);
        }
    }
    json_header_t(cJSON* parent) // for json parser
        : placement(NULL)
        , fs_placement(NULL)
        , fs_rect(NULL)
        , internal_border(NULL)
        , font(NULL)
        , font_title(NULL)
        , bg_image(NULL)
        , proxy(NULL)
        , chapter_rule(NULL)
    {
        cJSON* item;
        int i, size;
        cJSON* array;

        version = cJSON_GetObjectItem(parent, "version");
        item_count = cJSON_GetObjectItem(parent, "item_count");
        item_id = cJSON_GetObjectItem(parent, "item_id");
        style = cJSON_GetObjectItem(parent, "style");
        exstyle = cJSON_GetObjectItem(parent, "exstyle");
        fs_style = cJSON_GetObjectItem(parent, "fs_style");
        fs_exstyle = cJSON_GetObjectItem(parent, "fs_exstyle");
        char_gap = cJSON_GetObjectItem(parent, "char_gap");
        line_gap = cJSON_GetObjectItem(parent, "line_gap");
        paragraph_gap = cJSON_GetObjectItem(parent, "paragraph_gap");
        left_line_count = cJSON_GetObjectItem(parent, "left_line_count");
        font_color = cJSON_GetObjectItem(parent, "font_color");
        font_color_title = cJSON_GetObjectItem(parent, "font_color_title");
        use_same_font = cJSON_GetObjectItem(parent, "use_same_font");
        bg_color = cJSON_GetObjectItem(parent, "bg_color");
        alpha = cJSON_GetObjectItem(parent, "alpha");
        meun_font_follow = cJSON_GetObjectItem(parent, "meun_font_follow");
        wheel_speed = cJSON_GetObjectItem(parent, "wheel_speed");
        page_mode = cJSON_GetObjectItem(parent, "page_mode");
        autopage_mode = cJSON_GetObjectItem(parent, "autopage_mode");
        uElapse = cJSON_GetObjectItem(parent, "uElapse");
        hide_taskbar = cJSON_GetObjectItem(parent, "hide_taskbar");
        show_systray = cJSON_GetObjectItem(parent, "show_systray");
        disable_lrhide = cJSON_GetObjectItem(parent, "disable_lrhide");
        disable_eschide = cJSON_GetObjectItem(parent, "disable_eschide");
        word_wrap = cJSON_GetObjectItem(parent, "word_wrap");
        line_indent = cJSON_GetObjectItem(parent, "line_indent");
        blank_lines = cJSON_GetObjectItem(parent, "blank_lines");
        chapter_page = cJSON_GetObjectItem(parent, "chapter_page");
        ingore_version = cJSON_GetObjectItem(parent, "ingore_version");
        checkver_time = cJSON_GetObjectItem(parent, "checkver_time");

        global_key = cJSON_GetObjectItem(parent, "global_key");
        cust_colors = cJSON_GetObjectItem(parent, "cust_colors");

        memset(keyset, 0, sizeof(json_keyset_t*) * KI_MAXCOUNT);
        array = cJSON_GetObjectItem(parent, "keyset");
        if (array)
        {
            size = cJSON_GetArraySize(array);
            for (i = 0; i < size && i < KI_MAXCOUNT; i++)
            {
                item = cJSON_GetArrayItem(array, i);
                if (item)
                    keyset[i] = new json_keyset_t(item);
            }
        }

        item = cJSON_GetObjectItem(parent, "placement");
        if (item)
            placement = new json_placement_t(item);

        item = cJSON_GetObjectItem(parent, "fs_placement");
        if (item)
            fs_placement = new json_placement_t(item);

        item = cJSON_GetObjectItem(parent, "fs_rect");
        if (item)
            fs_rect = new json_rect_t(item);

        item = cJSON_GetObjectItem(parent, "internal_border");
        if (item)
            internal_border = new json_rect_t(item);

        item = cJSON_GetObjectItem(parent, "font");
        if (item)
            font = new json_font_t(item);

        item = cJSON_GetObjectItem(parent, "font_title");
        if (item)
            font_title = new json_font_t(item);

        item = cJSON_GetObjectItem(parent, "bg_image");
        if (item)
            bg_image = new json_bg_image_t(item);

        item = cJSON_GetObjectItem(parent, "proxy");
        if (item)
            proxy = new json_proxy_t(item);

        item = cJSON_GetObjectItem(parent, "chapter_rule");
        if (item)
            chapter_rule = new json_chapter_rule_t(item);

#if ENABLE_TAG
        tag_count = cJSON_GetObjectItem(parent, "tag_count");
        memset(tags, 0, sizeof(json_tagitem_t*) * MAX_TAG_COUNT);
        array = cJSON_GetObjectItem(parent, "tags");
        if (array)
        {
            size = cJSON_GetArraySize(array);
            for (i = 0; i < size && i < MAX_TAG_COUNT; i++)
            {
                item = cJSON_GetArrayItem(array, i);
                if (item)
                    tags[i] = new json_tagitem_t(item);
            }
        }
#endif
        book_source_count = cJSON_GetObjectItem(parent, "book_source_count");
        memset(book_sources, 0, sizeof(json_book_source_t*) * MAX_BOOKSRC_COUNT);
        array = cJSON_GetObjectItem(parent, "book_sources");
        if (array)
        {
            size = cJSON_GetArraySize(array);
            for (i = 0; i < size && i < MAX_BOOKSRC_COUNT; i++)
            {
                item = cJSON_GetArrayItem(array, i);
                if (item)
                    book_sources[i] = new json_book_source_t(item);
            }
        }
    }
    ~json_header_t()
    {
        int i;

        if (placement)
            delete placement;
        if (fs_placement)
            delete fs_placement;
        if (fs_rect)
            delete fs_rect;
        if (internal_border)
            delete internal_border;
        if (font)
            delete font;
        if (font_title)
            delete font_title;
        if (bg_image)
            delete bg_image;
        if (proxy)
            delete proxy;
        if (chapter_rule)
            delete chapter_rule;
        for (i = 0; i < KI_MAXCOUNT; i++)
        {
            if (keyset[i])
                delete keyset[i];
        }
#if ENABLE_TAG
        for (i = 0; i < MAX_TAG_COUNT; i++)
        {
            if (tags[i])
                delete tags[i];
        }
#endif
        for (i = 0; i < MAX_BOOKSRC_COUNT; i++)
        {
            if (book_sources[i])
                delete book_sources[i];
        }
    }
    void GetData(header_t* data)
    {
        int i, size;
        cJSON* item;

        if (version)
            JsonCopy(data->version, version->valuestring);
        if (item_count)
            data->item_count = item_count->valueint;
        if (item_id)
            data->item_id = item_id->valueint;
        if (style)
            data->style = *((u32*)&style->valueint);
        if (exstyle)
            data->exstyle = *((u32*)&exstyle->valueint);
        if (fs_style)
            data->fs_style = *((u32*)&fs_style->valueint);
        if (fs_exstyle)
            data->fs_exstyle = *((u32*)&fs_exstyle->valueint);
        if (char_gap)
            data->char_gap = char_gap->valueint;
        if (line_gap)
            data->line_gap = line_gap->valueint;
        if (paragraph_gap)
            data->paragraph_gap = paragraph_gap->valueint;
        if (left_line_count)
            data->left_line_count = left_line_count->valueint;
        if (font_color)
            data->font_color = *((u32*)&font_color->valueint);
        if (font_color_title)
            data->font_color_title = *((u32*)&font_color_title->valueint);
        if (use_same_font)
            data->use_same_font = use_same_font->valueint;
        if (bg_color)
            data->bg_color = *((u32*)&bg_color->valueint);
        if (alpha)
            data->alpha = (BYTE)alpha->valueint;
        if (meun_font_follow)
            data->meun_font_follow = meun_font_follow->valueint;
        if (wheel_speed)
            data->wheel_speed = wheel_speed->valueint;
        if (page_mode)
            data->page_mode = page_mode->valueint;
        if (autopage_mode)
            data->autopage_mode = autopage_mode->valueint;
        if (uElapse)
            data->uElapse = *((u32*)&uElapse->valueint);
        if (hide_taskbar)
            data->hide_taskbar = hide_taskbar->valueint;
        if (show_systray)
            data->show_systray = show_systray->valueint;
        if (disable_lrhide)
            data->disable_lrhide = disable_lrhide->valueint;
        if (disable_eschide)
            data->disable_eschide = disable_eschide->valueint;
        if (word_wrap)
            data->word_wrap = word_wrap->valueint;
        if (line_indent)
            data->line_indent = line_indent->valueint;
        if (blank_lines)
            data->blank_lines = blank_lines->valueint;
        if (chapter_page)
            data->chapter_page = chapter_page->valueint;
        if (ingore_version)
            JsonCopy(data->ingore_version, ingore_version->valuestring);
        if (checkver_time)
            data->checkver_time = *((u32*)&checkver_time->valueint);

        if (global_key)
            data->global_key = global_key->valueint;
        
        if (cust_colors)
        {
            size = cJSON_GetArraySize(cust_colors);
            if (size > MAX_CUST_COLOR_COUNT)
                size = MAX_CUST_COLOR_COUNT;
            for (i = 0; i < size; i++)
            {
                item = cJSON_GetArrayItem(cust_colors, i);
                if (item)
                    data->cust_colors[i] = *((u32*)&item->valueint);
            }
        }

        for (i = 0; i < KI_MAXCOUNT; i++)
        {
            if (keyset[i])
                keyset[i]->GetData(&(data->keyset[i]));
        }

        if (placement)
            placement->GetData(&(data->placement));
        if (fs_placement)
            fs_placement->GetData(&(data->fs_placement));
        if (fs_rect)
            fs_rect->GetData(&(data->fs_rect));
        if (internal_border)
            internal_border->GetData(&(data->internal_border));
        if (font)
            font->GetData(&(data->font));
        if (font_title)
            font_title->GetData(&(data->font_title));
        if (bg_image)
            bg_image->GetData(&(data->bg_image));
        if (proxy)
            proxy->GetData(&(data->proxy));
        if (chapter_rule)
            chapter_rule->GetData(&(data->chapter_rule));

#if ENABLE_TAG
        if (tag_count)
            data->tag_count = tag_count->valueint;
        for (i = 0; i < data->tag_count; i++)
        {
            if (tags[i])
                tags[i]->GetData(&(data->tags[i]));
        }
#endif
        if (book_source_count)
            data->book_source_count = book_source_count->valueint;
        for (i = 0; i < data->book_source_count; i++)
        {
            if (book_sources[i])
                book_sources[i]->GetData(&(data->book_sources[i]));
        }
    }
};

static bool JsonInteger(cJSON* value, double low, double high)
{
    return cJSON_IsNumber(value) && value->valuedouble >= low
        && value->valuedouble <= high && floor(value->valuedouble) == value->valuedouble;
}

static bool ReadLibraryItem(cJSON* parent, item_t* data, int schema, int row)
{
    if (!cJSON_IsObject(parent)) return false;
    cJSON* path = cJSON_GetObjectItem(parent, "file_name");
    cJSON* pos = cJSON_GetObjectItem(parent, "index");
    cJSON* marks = cJSON_GetObjectItem(parent, "mark");
    cJSON* count = cJSON_GetObjectItem(parent, "mark_size");
    cJSON* id = cJSON_GetObjectItem(parent, "id");
    if (!cJSON_IsString(path) || !path->valuestring[0] || !JsonInteger(pos, 0, INT_MAX)
        || !JsonInteger(count, 0, MAX_MARK_COUNT) || !cJSON_IsArray(marks)
        || cJSON_GetArraySize(marks) != count->valueint) return false;
    const wchar_t* full = Utf8ToUtf16(path->valuestring);
    if (!full || wcslen(full) >= MAX_PATH) return false;
    wcscpy(data->file_name, full);
    data->id = schema == 1 ? row + 1 : (JsonInteger(id, 1, INT_MAX - 1) ? id->valueint : -1);
    if (data->id < 1) return false;
    data->index = pos->valueint;
    data->mark_size = count->valueint;
    for (int i = 0; i < data->mark_size; ++i)
    {
        cJSON* mark = cJSON_GetArrayItem(marks, i);
        if (!JsonInteger(mark, 0, INT_MAX)) return false;
        data->mark[i] = mark->valueint;
    }
    cJSON* value = cJSON_GetObjectItem(parent, "is_new");
    data->is_new = value ? value->valueint != 0 : 0;
    data->read_state = data->index > 0 ? 1 : 0;
    if (schema >= 2)
    {
        const char* fields[] = { "title", "author", "file_key" };
        for (int i = 0; i < 3; ++i)
        {
            value = cJSON_GetObjectItem(parent, fields[i]);
            if (value && !cJSON_IsString(value)) return false;
            if (!value) continue;
            full = Utf8ToUtf16(value->valuestring);
            const size_t cap = i == 2 ? 64 : 256;
            if (!full || wcslen(full) >= cap) return false;
            wcscpy(i == 0 ? data->title : i == 1 ? data->author : data->file_key, full);
        }
        value = cJSON_GetObjectItem(parent, "text_length");
        if (value && !JsonInteger(value, 0, INT_MAX)) return false;
        data->text_length = value ? value->valueint : 0;
        value = cJSON_GetObjectItem(parent, "read_state");
        if (value && !JsonInteger(value, 0, 2)) return false;
        if (value) data->read_state = value->valueint;
        value = cJSON_GetObjectItem(parent, "favorite");
        if (value && !JsonInteger(value, 0, 1)) return false;
        data->favorite = value ? value->valueint : 0;
        value = cJSON_GetObjectItem(parent, "last_read");
        if (value && !JsonInteger(value, 0, 9007199254740991.0)) return false;
        data->last_read = value ? (u64)value->valuedouble : 0;
    }
    cJSON* zoom = cJSON_GetObjectItem(parent, "pdf_zoom");
    cJSON* scroll = cJSON_GetObjectItem(parent, "pdf_scroll");
    cJSON* horizontal = cJSON_GetObjectItem(parent, "pdf_x");
    if (zoom && (!JsonInteger(zoom, -1, 400) || (zoom->valueint > 0 && zoom->valueint < 25))) return false;
    if ((scroll && !JsonInteger(scroll, 0, 1000000)) || (horizontal && !JsonInteger(horizontal, 0, 1000000))) return false;
    data->pdf_zoom = zoom ? zoom->valueint : 0; data->pdf_scroll = scroll ? scroll->valueint : 0; data->pdf_x = horizontal ? horizontal->valueint : 0;
    // Legacy order is evidence of order only, never of a reading timestamp.
    if (!data->title[0])
    {
        const wchar_t* base = data->file_name;
        for (const wchar_t* p = base; *p; ++p) if (*p == L'\\' || *p == L'/') base = p + 1;
        wcsncpy(data->title, base, 255);
        wchar_t* ext = wcsrchr(data->title, L'.');
        if (ext) *ext = 0;
    }
    return true;
}

static void WriteLibraryItem(cJSON* parent, const item_t* data)
{
    cJSON_AddNumberToObject(parent, "id", data->id);
    cJSON_AddNumberToObject(parent, "index", data->index);
    cJSON_AddStringToObject(parent, "file_name", Utf16ToUtf8(data->file_name));
    cJSON_AddNumberToObject(parent, "mark_size", data->mark_size);
    cJSON_AddNumberToObject(parent, "is_new", data->is_new);
    cJSON_AddStringToObject(parent, "title", Utf16ToUtf8(data->title));
    cJSON_AddStringToObject(parent, "author", Utf16ToUtf8(data->author));
    cJSON_AddStringToObject(parent, "file_key", Utf16ToUtf8(data->file_key));
    cJSON_AddNumberToObject(parent, "text_length", data->text_length);
    cJSON_AddNumberToObject(parent, "read_state", data->read_state);
    cJSON_AddNumberToObject(parent, "favorite", data->favorite);
    cJSON_AddNumberToObject(parent, "last_read", (double)data->last_read);
    cJSON_AddNumberToObject(parent, "pdf_zoom", data->pdf_zoom);
    cJSON_AddNumberToObject(parent, "pdf_scroll", data->pdf_scroll);
    cJSON_AddNumberToObject(parent, "pdf_x", data->pdf_x);
    cJSON* marks = cJSON_AddArrayToObject(parent, "mark");
    for (int i = 0; i < data->mark_size; ++i) cJSON_AddItemToArray(marks, cJSON_CreateNumber(data->mark[i]));
}

static bool ReadBookmarkDetails(cJSON* root, const item_t* items, int count, bookmark_store_t& output)
{
    cJSON* array = cJSON_GetObjectItem(root, "bookmark_details");
    if (!array) return true;
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > count * MAX_MARK_COUNT) return false;
    std::map<int, const item_t*> books;
    for (int i = 0; i < count; ++i) books[items[i].id] = &items[i];
    std::set<std::pair<int, int> > seen;
    for (int i = 0; i < cJSON_GetArraySize(array); ++i)
    {
        cJSON* record = cJSON_GetArrayItem(array, i);
        cJSON* book = cJSON_GetObjectItem(record, "book_id");
        cJSON* index = cJSON_GetObjectItem(record, "mark_index");
        cJSON* chapter = cJSON_GetObjectItem(record, "chapter");
        cJSON* position = cJSON_GetObjectItem(record, "position");
        cJSON* title = cJSON_GetObjectItem(record, "title");
        cJSON* summary = cJSON_GetObjectItem(record, "summary");
        if (!cJSON_IsObject(record) || !JsonInteger(book, 1, INT_MAX - 1)
            || !JsonInteger(index, 0, MAX_MARK_COUNT - 1) || !JsonInteger(chapter, -1, INT_MAX)
            || !JsonInteger(position, 0, INT_MAX) || !cJSON_IsString(title) || !cJSON_IsString(summary)) return false;
        std::map<int, const item_t*>::const_iterator found = books.find(book->valueint);
        if (found == books.end() || index->valueint >= found->second->mark_size
            || position->valueint != found->second->mark[index->valueint]
            || !seen.insert(std::make_pair(book->valueint, index->valueint)).second) return false;
        bookmark_detail_t detail(chapter->valueint, position->valueint);
        const wchar_t* value = Utf8ToUtf16(title->valuestring);
        if (!value || wcslen(value) > 255) return false;
        detail.title = value;
        value = Utf8ToUtf16(summary->valuestring);
        if (!value || wcslen(value) > 199) return false;
        detail.summary = value;
        cJSON* note = cJSON_GetObjectItem(record, "note");
        if (note)
        {
            if (!cJSON_IsString(note)) return false;
            value = Utf8ToUtf16(note->valuestring);
            if (!value || wcslen(value) > 500) return false;
            detail.note = value;
        }
        std::vector<bookmark_detail_t>& records = output[book->valueint];
        if (records.empty())
            for (int j = 0; j < found->second->mark_size; ++j) records.push_back(bookmark_detail_t(-1, found->second->mark[j]));
        records[index->valueint] = detail;
    }
    return true;
}

static void WriteBookmarkDetails(cJSON* root, header_t* data, const bookmark_store_t* bookmarks)
{
    if (!bookmarks || bookmarks->empty()) return;
    std::map<int, const item_t*> books;
    const item_t* items = (const item_t*)((const char*)data + sizeof(header_t));
    for (int i = 0; i < data->item_count; ++i) books[items[i].id] = &items[i];
    cJSON* array = cJSON_AddArrayToObject(root, "bookmark_details");
    for (bookmark_store_t::const_iterator book = bookmarks->begin(); book != bookmarks->end(); ++book)
    {
        std::map<int, const item_t*>::const_iterator found = books.find(book->first);
        if (found == books.end()) continue;
        for (int i = 0; i < (int)book->second.size() && i < found->second->mark_size; ++i)
        {
            const bookmark_detail_t& detail = book->second[i];
            if (detail.position != found->second->mark[i] || (detail.chapter < 0 && detail.title.empty() && detail.summary.empty() && detail.note.empty())) continue;
            cJSON* record = cJSON_CreateObject();
            cJSON_AddNumberToObject(record, "book_id", book->first);
            cJSON_AddNumberToObject(record, "mark_index", i);
            cJSON_AddNumberToObject(record, "chapter", detail.chapter);
            cJSON_AddNumberToObject(record, "position", detail.position);
            cJSON_AddStringToObject(record, "title", Utf16ToUtf8(detail.title.c_str()));
            cJSON_AddStringToObject(record, "summary", Utf16ToUtf8(detail.summary.c_str()));
            if (!detail.note.empty()) cJSON_AddStringToObject(record, "note", Utf16ToUtf8(detail.note.c_str()));
            cJSON_AddItemToArray(array, record);
        }
    }
}

char* create_json(header_t *data, const bookmark_store_t* bookmarks)
{
    cJSON* root, * header, * items, * item;
    json_header_t* headerobj = NULL;
    item_t* itemdata;
    int i, offset;
    char* json = NULL;

    if (!data) return NULL;
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", CACHE_SCHEMA_VERSION);
    header = cJSON_CreateObject();
    cJSON_AddNumberToObject(header, "next_item_id", data->next_item_id);
    cJSON_AddItemToObject(root, "header", header);

    cJSON* ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "theme", data->ui_theme);
    cJSON_AddNumberToObject(ui, "reading_custom", data->reading_custom);
    cJSON_AddNumberToObject(ui, "reading_border_custom", data->reading_border_custom);
    cJSON_AddNumberToObject(ui, "sidebar_width", data->sidebar_width);
    headerobj = new json_header_t(header, data);
    if (data->item_count > 0)
    {
        items = cJSON_AddArrayToObject(root, "items");
        for (i = 0; i < data->item_count; i++)
        {
            offset = sizeof(header_t) + i * sizeof(item_t);
            itemdata = (item_t*)((char*)data + offset);

            item = cJSON_CreateObject();
            WriteLibraryItem(item, itemdata);
            cJSON_AddItemToArray(items, item);
        }
    }
    delete headerobj;

    WriteBookmarkDetails(root, data, bookmarks);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void create_json_free(char* json)
{
    if (json)
        free(json);
}

BOOL parser_json(const char* json, header_t* defhdr, void** data, int* size, int* source_schema, bookmark_store_t* bookmarks)
{
    *data = NULL; *size = 0;
    cJSON* root = cJSON_ParseWithOpts(json, NULL, TRUE);
    if (!root) return FALSE;
    cJSON* version = cJSON_GetObjectItem(root, "schema_version");
    const int schema = version ? version->valueint : 1;
    cJSON* header = cJSON_GetObjectItem(root, "header");
    cJSON* items = cJSON_GetObjectItem(root, "items");
    cJSON* count = cJSON_GetObjectItem(header, "item_count");
    if (!cJSON_IsObject(root) || (version && !JsonInteger(version, 1, CACHE_SCHEMA_VERSION))
        || !cJSON_IsObject(header) || !JsonInteger(count, 0, MAX_LIBRARY_ITEMS)
        || (items && !cJSON_IsArray(items)) || cJSON_GetArraySize(items) != count->valueint)
    {
        cJSON_Delete(root); return FALSE;
    }
    // Settings also use fixed-size arrays. Reject counts that would drop records
    // or exceed those bounds rather than normalizing a damaged cache to empty.
    const char* arrays[] = { "keyset", "book_sources", "cust_colors" };
    const int limits[] = { KI_MAXCOUNT, MAX_BOOKSRC_COUNT, MAX_CUST_COLOR_COUNT };
    for (int i = 0; i < 3; ++i)
    {
        cJSON* array = cJSON_GetObjectItem(header, arrays[i]);
        if (array && (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > limits[i]))
        { cJSON_Delete(root); return FALSE; }
    }
    cJSON* sourceCount = cJSON_GetObjectItem(header, "book_source_count");
    if (sourceCount && (!JsonInteger(sourceCount, 0, MAX_BOOKSRC_COUNT)
        || sourceCount->valueint != cJSON_GetArraySize(cJSON_GetObjectItem(header, "book_sources"))))
    { cJSON_Delete(root); return FALSE; }
    json_header_t headerobj(header);
    headerobj.GetData(defhdr);
    cJSON* ui = cJSON_GetObjectItem(root, "ui");
    defhdr->ui_theme = 0; defhdr->reading_custom = 1; defhdr->reading_border_custom = 0; defhdr->sidebar_width = 240;
    if (schema >= 4 && !ui) { cJSON_Delete(root); return FALSE; }
    if (ui)
    {
        cJSON* mode = cJSON_GetObjectItem(ui, "theme");
        cJSON* custom = cJSON_GetObjectItem(ui, "reading_custom");
        cJSON* width = cJSON_GetObjectItem(ui, "sidebar_width");
        cJSON* border = cJSON_GetObjectItem(ui, "reading_border_custom");
        if (!cJSON_IsObject(ui) || !JsonInteger(mode, 0, 1) || !JsonInteger(custom, 0, 1) || !JsonInteger(width, 160, 640)
            || (border && !JsonInteger(border, 0, 1)))
        { cJSON_Delete(root); return FALSE; }
        defhdr->ui_theme = mode->valueint; defhdr->reading_custom = custom->valueint; defhdr->sidebar_width = width->valueint;
        if (border) defhdr->reading_border_custom = border->valueint;
    }
    defhdr->cache_schema = CACHE_SCHEMA_VERSION;
    defhdr->item_count = count->valueint;
    defhdr->next_item_id = 1;
    *size = sizeof(header_t) + sizeof(item_t) * defhdr->item_count;
    *data = calloc(1, *size);
    if (!*data) { *size = 0; cJSON_Delete(root); return FALSE; }
    item_t* out = (item_t*)((char*)*data + sizeof(header_t));
    std::set<int> ids;
    bool valid = true;
    for (int i = 0; i < defhdr->item_count && valid; ++i)
    {
        valid = ReadLibraryItem(cJSON_GetArrayItem(items, i), &out[i], schema, i)
            && ids.insert(out[i].id).second;
        if (valid && defhdr->next_item_id <= out[i].id) defhdr->next_item_id = out[i].id + 1;
    }
    cJSON* next = cJSON_GetObjectItem(header, "next_item_id");
    if (schema >= 2)
    {
        valid = valid && JsonInteger(next, defhdr->next_item_id, INT_MAX);
        if (valid) defhdr->next_item_id = next->valueint;
        if (defhdr->item_id != -1 && ids.find(defhdr->item_id) == ids.end()) valid = false;
    }
    else
    {
        // Old open_item moved the active book to the front but stored its old row.
        defhdr->item_id = defhdr->item_count ? out[0].id : -1;
    }
    bookmark_store_t details;
    if (valid) valid = ReadBookmarkDetails(root, out, defhdr->item_count, details);
    if (valid)
    {
        if (bookmarks) bookmarks->swap(details);
        memcpy(*data, defhdr, sizeof(header_t));
        if (source_schema) *source_schema = schema;
    }
    else { free(*data); *data = NULL; *size = 0; }
    cJSON_Delete(root);
    return valid ? TRUE : FALSE;
}

BOOL import_book_source(const char* json, book_source_t *bs, int* count)
{
    cJSON* root, * book_sources, * item;
    json_book_source_t* json_bs = NULL;
    int i;

    root = cJSON_Parse(json);
    if (!root)
    {
        return FALSE;
    }

    book_sources = cJSON_GetObjectItem(root, "book_sources");
    if (!book_sources || cJSON_GetArraySize(book_sources) <= 0 || cJSON_GetArraySize(book_sources) > MAX_BOOKSRC_COUNT)
    {
        cJSON_Delete(root);
        return FALSE;
    }

    *count = 0;
    for (i = 0; i < cJSON_GetArraySize(book_sources); i++)
    {
        item = cJSON_GetArrayItem(book_sources, i);
        if (item)
        {
            json_bs = new json_book_source_t(item);
            json_bs->GetData(&(bs[i]));
            (*count)++;
            delete json_bs;
        }   
    }
    cJSON_Delete(root);
    return TRUE;
}

BOOL export_book_source(const book_source_t* bs, int count, char** json)
{
    cJSON* root, * book_sources, * item;
    json_book_source_t* json_bs = NULL;
    int i;

    *json = NULL;
    if (!bs || count < 0 || count > MAX_BOOKSRC_COUNT)
    {
        return FALSE;
    }

    root = cJSON_CreateObject();
    book_sources = cJSON_AddArrayToObject(root, "book_sources");

    for (i = 0; i < count; i++)
    {
        item = cJSON_CreateObject();
        json_bs = new json_book_source_t(item, &(bs[i]));
        cJSON_AddItemToArray(book_sources, item);
        delete json_bs;
    }

    //*json = cJSON_PrintUnformatted(root);
    *json = cJSON_Print(root);
    cJSON_Delete(root);
    return TRUE;
}

void export_book_source_free(char* json)
{
    if (json)
        free(json);
}