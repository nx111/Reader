#include "MobiBook.h"
#include "Utils.h"
#include "types.h"
#include "resource.h"
#include <algorithm>
#include <limits.h>


#include <shlwapi.h>

#include "libxml/xmlreader.h"
#include "libxml/HTMLtree.h"
#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include "libxml/uri.h"


/**
 @brief Messages for libmobi return codes
 For reference see enum MOBI_RET in mobi.h
 */
const char *libmobi_messages[] = {
    "Success",
    "Generic error",
    "Wrong function parameter",
    "Corrupted data",
    "File not found",
    "Document is encrypted",
    "Unsupported document format",
    "Memory allocation error",
    "Initialization error",
    "Buffer error",
    "XML error",
    "Invalid DRM pid",
    "DRM key not found",
    "DRM support not included",
    "Write failed",
    "DRM expired"
};

#define LIBMOBI_MSG_COUNT ARRAYSIZE(libmobi_messages)

/**
 @brief Return message for given libmobi return code
 @param[in] ret Libmobi return code
 @return Message string
 */
const char * libmobi_msg(const MOBI_RET ret) {
    size_t index = ret;
    if (index < LIBMOBI_MSG_COUNT) {
        return libmobi_messages[index];
    }
    return "Unknown error";
}

MobiBook::MobiBook()
    : m_OpenErrorMessage(0)
    , m_Cover(NULL)
    , m_CoverStream(NULL)
{
    /* Initialize main MOBIData structure */
    /* Must be deallocated with mobi_free() when not needed */
}

MobiBook::~MobiBook()
{
    ForceKill();
    FreeFilelist();
    delete m_Cover;
    if (m_CoverStream)
        m_CoverStream->Release();
}

book_type_t MobiBook::GetBookType()
{
    return book_mobi;
}

UINT MobiBook::GetOpenErrorMessage(void)
{
    return m_OpenErrorMessage;
}

BOOL MobiBook::SaveBook(HWND hWnd)
{
    return FALSE;
}

BOOL MobiBook::UpdateChapters(int offset)
{
    return FALSE;
}

namespace
{
bool MobiAsciiSpace(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == '\f' || value == '\v';
}

bool MobiAsciiPrefix(const unsigned char *data, size_t size, const char *prefix)
{
    for (size_t i = 0; prefix[i]; ++i)
    {
        if (i >= size)
            return false;
        unsigned char value = data[i];
        if (value >= 'A' && value <= 'Z')
            value += 'a' - 'A';
        if (value != (unsigned char)prefix[i])
            return false;
    }
    return true;
}

bool MobiHtmlDocument(const unsigned char *data, size_t size)
{
    // PalmDoc predates MOBI headers and can hold plain text OR an HTML
    // document. Require an explicit HTML root/doctype; arbitrary <...> and
    // &entities; in ordinary prose must remain literal text.
    const unsigned char *end = data + size;
    if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
        data += 3;
    while (data < end)
    {
        while (data < end && MobiAsciiSpace(*data))
            ++data;
        size = end - data;
        const char *terminator = NULL;
        size_t markerSize = 0;
        if (MobiAsciiPrefix(data, size, "<!--"))
        {
            terminator = "-->";
            markerSize = 3;
        }
        else if (MobiAsciiPrefix(data, size, "<?xml") && size > 5 && MobiAsciiSpace(data[5]))
        {
            terminator = "?>";
            markerSize = 2;
        }
        if (terminator)
        {
            data = std::search(data, end, terminator, terminator + markerSize);
            if (data == end)
                return false;
            data += markerSize;
            continue;
        }
        if (MobiAsciiPrefix(data, size, "<!doctype") && size > 9 && MobiAsciiSpace(data[9]))
        {
            data += 9;
            while (data < end && MobiAsciiSpace(*data))
                ++data;
            return MobiAsciiPrefix(data, end - data, "html") &&
                data + 4 < end && (MobiAsciiSpace(data[4]) || data[4] == '>');
        }
        return MobiAsciiPrefix(data, size, "<html") && size > 5 &&
            (MobiAsciiSpace(data[5]) || data[5] == '>' || data[5] == '/');
    }
    return false;
}
}

BOOL MobiBook::ParserBook(HWND hWnd)
{
    BOOL ret = FALSE;
    MOBIData *m = mobi_init();
    MOBIRawml *rawml = NULL;
    FILE *file = NULL;
    MOBI_RET mobi_ret = MOBI_SUCCESS;
    mobi_t mobi;
    manifests_t::iterator itor;
    navpoints_t::iterator it;

    delete m_Cover;
    m_Cover = NULL;
    if (m_CoverStream)
    {
        m_CoverStream->Release();
        m_CoverStream = NULL;
    }
    m_OpenErrorMessage = IDS_MOBI_INVALID;
    if (m == NULL)
        goto end;

    // libmobi's filename API takes an ANSI path on Windows. Open the file here
    // so Chinese (and other Unicode) filenames work without a lossy conversion.
    file = _wfopen(m_fileName, L"rb");
    if (file == NULL)
    {
        m_OpenErrorMessage = 0;
        goto end;
    }
    // AZW3 is KF8. Prefer KF8 also when the container has a KF7 fallback.
    m->use_kf8 = true;
    mobi_ret = mobi_load_file(m, file);
    fclose(file);
    file = NULL;
    if (mobi_ret != MOBI_SUCCESS)
        goto end;

    // Do not try to decrypt books. Hybrid containers may have a second header;
    // reject any nonzero encryption type, including unknown future variants.
    if ((m->rh && m->rh->encryption_type != MOBI_ENCRYPTION_NONE) ||
        (m->next && m->next->rh && m->next->rh->encryption_type != MOBI_ENCRYPTION_NONE))
    {
        m_OpenErrorMessage = IDS_MOBI_ENCRYPTED;
        goto end;
    }
    if (mobi_is_replica(m))
    {
        m_OpenErrorMessage = IDS_MOBI_UNSUPPORTED;
        goto end;
    }
    if (m_bForceKill)
        goto end;

    rawml = mobi_init_rawml(m);
    if (rawml == NULL)
        goto end;
    if (m->mh == NULL)
    {
        // Older TEXt/REAd PalmDoc may be real plain text. First unpack without
        // HTML/link reconstruction (which itself rewrites literal filepos
        // attributes), while still letting libmobi normalize CP1252 to UTF-8.
        mobi_ret = mobi_parse_rawml_opt(rawml, m, false, false, false);
        if (mobi_ret != MOBI_SUCCESS || m_bForceKill)
            goto end;
        MOBIPart *part = rawml->markup;
        if (!part || !part->data || part->size == 0 || part->size > INT_MAX)
            goto end;
        if (!MobiHtmlDocument(part->data, part->size))
        {
            wchar_t *text = NULL;
            int length = 0;
            if (!DecodeText((const char *)part->data, (int)part->size, &text, &length) ||
                length <= 0 || m_bForceKill)
            {
                free(text);
                goto end;
            }
            free(m_Text);
            m_Text = text;
            m_Length = length;
            m_Chapters.clear();
            ret = TRUE;
            m_OpenErrorMessage = 0;
            goto end;
        }
        // PalmDoc containing an explicit HTML document keeps metadata, TOC,
        // resource and link reconstruction via the normal Mobipocket path.
        mobi_free_rawml(rawml);
        rawml = mobi_init_rawml(m);
        if (rawml == NULL)
            goto end;
    }
    mobi_ret = mobi_parse_rawml(rawml, m);
    if (mobi_ret != MOBI_SUCCESS || m_bForceKill)
        goto end;

    // The same reconstructed OPF, NCX, markup and resources serve MOBI and KF8.
    if (!UnzipBook(rawml, m, mobi) || !ParserOpf(mobi))
        goto end;
    if (!ParserNcx(mobi))
        goto end;
    ParserCover(mobi, m); // A missing/unsupported cover must not hide the text.
    if (!ParserChapters(mobi))
        goto end;
    ret = TRUE;
    m_OpenErrorMessage = 0;

end:
    if (!ret && mobi_ret != MOBI_SUCCESS)
    {
        printf("Error reading MOBI/KF8 (%s)\n", libmobi_msg(mobi_ret));
        if (mobi_ret == MOBI_FILE_ENCRYPTED || mobi_ret == MOBI_DRM_UNSUPPORTED ||
            mobi_ret == MOBI_DRM_KEYNOTFOUND || mobi_ret == MOBI_DRM_EXPIRED)
            m_OpenErrorMessage = IDS_MOBI_ENCRYPTED;
        else if (mobi_ret == MOBI_FILE_UNSUPPORTED)
            m_OpenErrorMessage = IDS_MOBI_UNSUPPORTED;
    }
    for (itor = mobi.manifests.begin(); itor != mobi.manifests.end(); ++itor)
        delete itor->second;
    for (it = mobi.navpoints.begin(); it != mobi.navpoints.end(); ++it)
        delete it->second;
    FreeFilelist(); // Its pointers belong to rawml, not to this map.
    if (rawml != NULL)
        mobi_free_rawml(rawml);
    if (file != NULL)
        fclose(file);
    mobi_free(m);

    if (!ret)
    {
        delete m_Cover;
        m_Cover = NULL;
        if (m_CoverStream)
        {
            m_CoverStream->Release();
            m_CoverStream = NULL;
        }
        CloseBook();
    }
    return ret;
}

Gdiplus::Bitmap* MobiBook::GetCover(void)
{
    return m_Cover;
}

int MobiBook::GetTextBeginIndex(void)
{
    return m_Cover ? 1 : 0;
}

void MobiBook::FreeFilelist(void)
{
    m_flist.clear();
}

BOOL MobiBook::UnzipBook(MOBIRawml *rawml, MOBIData *m, mobi_t &mobi)
{
    char partname[FILENAME_MAX];

    file_data_t fdata;

    FreeFilelist();

    
    if (rawml->markup != NULL) {
        /* Linked list of MOBIPart structures in rawml->markup holds main text files */
        MOBIPart *curr = rawml->markup;
        while (curr != NULL) {
            if (curr->size > INT_MAX || (curr->size && !curr->data))
                return FALSE;
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            snprintf(partname, sizeof(partname), "part%05zu.%s", curr->uid, file_meta.extension);
            printf("%s\n", partname);
            
            // save to file map
            fdata.data = curr->data;
            fdata.size = curr->size;
            m_flist.insert(std::make_pair(partname, fdata));            
            
            curr = curr->next;
        }
    }
    if (rawml->flow != NULL) {
        /* Linked list of MOBIPart structures in rawml->flow holds supplementary text files */
        MOBIPart *curr = rawml->flow;
        /* skip raw html file */
        curr = curr->next;
        while (curr != NULL) {
            if (curr->size > INT_MAX || (curr->size && !curr->data))
                return FALSE;
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            snprintf(partname, sizeof(partname), "flow%05zu.%s", curr->uid, file_meta.extension);
            printf("%s\n", partname);
            
            // save to file map
            fdata.data = curr->data;
            fdata.size = curr->size;
            m_flist.insert(std::make_pair(partname, fdata));                   
            
            curr = curr->next;
        }
    }
    if (rawml->resources != NULL) {
        /* Linked list of MOBIPart structures in rawml->resources holds binary files, also opf files */
        MOBIPart *curr = rawml->resources;
        /* jpg, gif, png, bmp, font, audio, video also opf, ncx */
        while (curr != NULL) {
            if (curr->size > INT_MAX || (curr->size && !curr->data))
                return FALSE;
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            if (curr->size > 0) {
                int n = snprintf(partname, sizeof(partname), "resource%05zu.%s", curr->uid, file_meta.extension);
                if (n < 0) {
                    printf("Creating file name failed\n");
                    return FALSE;
                }
                if ((size_t) n >= sizeof(partname)) {
                    printf("File name too long: %s\n", partname);
                    return FALSE;
                }
                
                printf("%s\n", partname);
 
                // save to file map
                fdata.data = curr->data;
                fdata.size = curr->size;
                m_flist.insert(std::make_pair(partname, fdata));

                if (file_meta.type == T_OPF)
                {
                    mobi.opf = partname;
                }

                if (file_meta.type == T_NCX)
                {
                    mobi.ncx = partname;
                }

            }
            curr = curr->next;
        }
    }
    return TRUE;
}

namespace
{
xmlNodePtr MobiChild(xmlNodePtr parent, const char *name)
{
    for (xmlNodePtr node = parent ? parent->children : NULL; node; node = node->next)
        if (node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name))
            return node;
    return NULL;
}

std::string MobiProperty(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, BAD_CAST name);
    std::string result = value ? (const char *)value : "";
    xmlFree(value);
    return result;
}

std::string MobiHref(const std::string &href)
{
    // Unlike form URL decoding, '+' in an ebook path is a literal plus sign.
    char *value = xmlURIUnescapeString(href.c_str(), 0, NULL);
    std::string result = value ? value : href;
    xmlFree(value);
    return result;
}

void MobiNavPoints(xmlNodePtr parent, navpoints_t &navpoints, int &order)
{
    for (xmlNodePtr node = parent ? parent->children : NULL; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE || !xmlStrEqual(node->name, BAD_CAST "navPoint"))
            continue;
        xmlNodePtr label = MobiChild(node, "navLabel");
        xmlNodePtr content = MobiChild(node, "content");
        xmlChar *text = label ? xmlNodeGetContent(label) : NULL;
        std::string src = content ? MobiHref(MobiProperty(content, "src")) : "";
        if (text && *text && !src.empty())
        {
            navpoint_t *nav = new navpoint_t;
            nav->id = MobiProperty(node, "id");
            nav->src = src;
            nav->text = (const char *)text;
            nav->order = order++;
            // Duplicate destinations are common in nested NCX files. Keep the
            // first label instead of discarding the entire table of contents.
            if (!navpoints.insert(std::make_pair(src, nav)).second)
                delete nav;
        }
        xmlFree(text);
        MobiNavPoints(node, navpoints, order);
    }
}
}

BOOL MobiBook::ParserOpf(mobi_t &mobi)
{
    filelist_t::iterator file = m_flist.find(mobi.opf);
    if (file == m_flist.end() || m_bForceKill)
        return FALSE;
    xmlDocPtr doc = xmlReadMemory((const char *)file->second.data, file->second.size,
        NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (!doc)
        return FALSE;

    BOOL ret = FALSE;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    xmlNodePtr manifest = MobiChild(root, "manifest");
    xmlNodePtr spine = MobiChild(root, "spine");
    if (!root || !xmlStrEqual(root->name, BAD_CAST "package") || !manifest || !spine)
        goto end;
    for (xmlNodePtr node = manifest->children; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE || !xmlStrEqual(node->name, BAD_CAST "item"))
            continue;
        manifest_t *item = new manifest_t;
        item->id = MobiProperty(node, "id");
        item->href = MobiHref(MobiProperty(node, "href"));
        item->media_type = MobiProperty(node, "media-type");
        if (item->id.empty() || item->href.empty() || item->media_type.empty() ||
            !mobi.manifests.insert(std::make_pair(item->id, item)).second)
        {
            delete item;
            goto end;
        }
        if (m_bForceKill)
            goto end;
    }
    for (xmlNodePtr node = spine->children; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE || !xmlStrEqual(node->name, BAD_CAST "itemref"))
            continue;
        std::string id = MobiProperty(node, "idref");
        if (mobi.manifests.find(id) == mobi.manifests.end())
            goto end;
        mobi.spines.push_back(id);
        if (m_bForceKill)
            goto end;
    }
    if (mobi.spines.empty())
        goto end;
    {
        manifests_t::iterator toc = mobi.manifests.find(MobiProperty(spine, "toc"));
        if (toc != mobi.manifests.end())
            mobi.ncx = toc->second->href;
    }
    ret = TRUE;
end:
    xmlFreeDoc(doc);
    return ret;
}

BOOL MobiBook::ParserNcx(mobi_t &mobi)
{
    if (m_bForceKill)
        return FALSE;
    // NCX is optional. Reading order still comes from the OPF spine if a TOC
    // is absent or unusable; it must not prevent an otherwise valid KF8 book.
    filelist_t::iterator file = m_flist.find(mobi.path + mobi.ncx);
    if (mobi.ncx.empty() || file == m_flist.end())
        return TRUE;
    xmlDocPtr doc = xmlReadMemory((const char *)file->second.data, file->second.size,
        NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (!doc)
        return TRUE;
    int order = 0;
    MobiNavPoints(MobiChild(xmlDocGetRootElement(doc), "navMap"), mobi.navpoints, order);
    xmlFreeDoc(doc);
    return !m_bForceKill;
}

namespace
{
int MobiUtf16Length(const xmlChar *text)
{
    int length = 0;
    for (const xmlChar *p = text; p && *p; ++p)
    {
        if ((*p & 0xc0) != 0x80)
            length += *p >= 0xf0 ? 2 : 1;
    }
    return length;
}

void MobiAnchors(xmlNodePtr node, int &offset, std::map<std::string, int> &anchors)
{
    if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE ||
        node->type == XML_ENTITY_REF_NODE)
    {
        xmlChar *value = xmlNodeGetContent(node);
        offset += MobiUtf16Length(value);
        xmlFree(value);
        return;
    }
    if (node->type != XML_ELEMENT_NODE)
        return;
    const char *names[] = { "id", "name" };
    for (size_t i = 0; i < ARRAYSIZE(names); ++i)
    {
        std::map<std::string, int>::iterator anchor = anchors.find(MobiProperty(node, names[i]));
        if (anchor != anchors.end() && anchor->second < 0)
            anchor->second = offset;
    }
    for (xmlNodePtr child = node->children; child; child = child->next)
        MobiAnchors(child, offset, anchors);
}

bool MobiIndentBlank(wchar_t value)
{
    return value == 0x20 || value == 0x09 || value == 0x0b || value == 0x0c ||
        value == 0x3000 || value == 0xa0;
}

void MobiFormattedAnchors(const wchar_t *raw, int rawlen, const wchar_t *text, int len,
    std::map<std::string, int> &anchors)
{
    std::vector<std::pair<int, std::string> > offsets;
    for (std::map<std::string, int>::iterator it = anchors.begin(); it != anchors.end(); ++it)
        if (it->second >= 0)
            offsets.push_back(std::make_pair(it->second, it->first));
    std::sort(offsets.begin(), offsets.end());
    // Book::FormatText removes whitespace (including CR in CRLF) and may
    // normalize mixed/NBSP indentation to spaces. Align equivalent blanks so
    // destinations survive both operations, without guessing from TOC labels
    // or treating KF8 IDs as numbers.
    int source = 0, target = 0;
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        while (source < rawlen && source < offsets[i].first)
        {
            if (target < len && (raw[source] == text[target] ||
                (MobiIndentBlank(raw[source]) && MobiIndentBlank(text[target]))))
                ++target;
            ++source;
        }
        anchors[offsets[i].second] = target;
    }
}
}

BOOL MobiBook::ParserOps(file_data_t *fdata, wchar_t **text, int *len, wchar_t **title,
    int *tlen, BOOL parsertitle, std::map<std::string, int> *anchors)
{
    *text = NULL;
    *title = NULL;
    *len = *tlen = 0;
    if (!fdata || !fdata->data || fdata->size <= 0 || m_bForceKill)
        return FALSE;

    // libmobi normalizes legacy CP1252 and KF8 markup to UTF-8. Old charset
    // declarations (or no meta tag) must not reinterpret those bytes as Latin1.
    xmlDocPtr doc = htmlReadMemory((const char *)fdata->data, fdata->size, NULL, "UTF-8",
        HTML_PARSE_RECOVER | HTML_PARSE_NOBLANKS | HTML_PARSE_NONET);
    if (!doc)
        return FALSE;
    // Preserve the existing text-oriented renderer's paragraph formatting.
    xmlChar *formatted = NULL;
    int size = 0;
    htmlDocDumpMemoryFormat(doc, &formatted, &size, 1);
    xmlFreeDoc(doc);
    if (!formatted || size <= 0)
    {
        xmlFree(formatted);
        return FALSE;
    }
    // Reparse as HTML, not XML: named HTML entities must retain their text.
    doc = htmlReadMemory((const char *)formatted, size, NULL, "UTF-8",
        HTML_PARSE_RECOVER | HTML_PARSE_NONET);
    xmlFree(formatted);
    if (!doc)
        return FALSE;

    BOOL ret = FALSE;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    xmlNodePtr body = MobiChild(root, "body");
    if (body && !m_bForceKill)
    {
        if (parsertitle)
        {
            xmlNodePtr titleNode = MobiChild(MobiChild(root, "head"), "title");
            xmlChar *value = titleNode ? xmlNodeGetContent(titleNode) : NULL;
            if (value && *value)
                DecodeText((const char *)value, xmlStrlen(value), title, tlen);
            xmlFree(value);
        }
        xmlChar *value = xmlNodeGetContent(body);
        if (value)
        {
            ret = DecodeText((const char *)value, xmlStrlen(value), text, len);
            if (ret && anchors && !anchors->empty())
            {
                int offset = 0, rawlen = 0;
                MobiAnchors(body, offset, *anchors);
                wchar_t *raw = utf8_to_utf16((const char *)value, xmlStrlen(value), &rawlen);
                if (raw)
                {
                    MobiFormattedAnchors(raw, rawlen, *text, *len, *anchors);
                    free(raw);
                }
                else
                    ret = FALSE;
            }
            xmlFree(value);
        }
    }
    xmlFreeDoc(doc);
    return ret && !m_bForceKill;
}

BOOL MobiBook::ParserChapters(mobi_t &mobi)
{
    std::wstring document;
    BOOL hasText = FALSE;
    m_Chapters.clear();
    if (GetCover())
        document += L'\n';

    for (spines_t::iterator spine = mobi.spines.begin(); spine != mobi.spines.end(); ++spine)
    {
        if (m_bForceKill)
            return FALSE;
        manifests_t::iterator item = mobi.manifests.find(*spine);
        if (item == mobi.manifests.end())
            return FALSE;
        const std::string &href = item->second->href;
        filelist_t::iterator file = m_flist.find(mobi.path + href);
        if (file == m_flist.end())
            return FALSE;

        std::map<std::string, int> anchors;
        std::vector<navpoint_t *> navigation;
        for (navpoints_t::iterator nav = mobi.navpoints.begin(); nav != mobi.navpoints.end(); ++nav)
        {
            const std::string &src = nav->second->src;
            size_t separator = src.find('#');
            if (src.substr(0, separator) == href)
            {
                navigation.push_back(nav->second);
                if (separator != std::string::npos && separator + 1 < src.size())
                    anchors[src.substr(separator + 1)] = -1;
            }
        }
        std::sort(navigation.begin(), navigation.end(), [](const navpoint_t *a, const navpoint_t *b) {
            return a->order < b->order;
        });

        wchar_t *text = NULL, *title = NULL;
        int len = 0, tlen = 0;
        BOOL parsed = ParserOps(&file->second, &text, &len, &title, &tlen, TRUE, &anchors);
        if (!parsed || len < 0 || (size_t)len > (size_t)INT_MAX - document.size())
        {
            free(text);
            free(title);
            return FALSE;
        }
        if (len > 0)
        {
            hasText = TRUE;
            int start = (int)document.size();
            document.append(text, len);
            size_t chaptersBefore = m_Chapters.size();
            for (size_t i = 0; i < navigation.size(); ++i)
            {
                const navpoint_t *nav = navigation[i];
                wchar_t *label = NULL;
                int labelLen = 0;
                if (DecodeText(nav->text.c_str(), (int)nav->text.size(), &label, &labelLen) && labelLen > 0)
                {
                    int offset = 0;
                    size_t separator = nav->src.find('#');
                    if (separator != std::string::npos && separator + 1 < nav->src.size())
                    {
                        std::map<std::string, int>::iterator anchor = anchors.find(nav->src.substr(separator + 1));
                        if (anchor != anchors.end() && anchor->second >= 0)
                            offset = anchor->second;
                        else
                        {
                            // Damaged/legacy filepos links: a label match is a
                            // safe fallback. Never parse a KF8 anchor with stoi.
                            size_t match = std::wstring(text, len).find(label);
                            if (match != std::wstring::npos)
                                offset = (int)match;
                        }
                    }
                    chapter_item_t chapter = {};
                    chapter.index = start + (offset < len ? offset : len - 1);
                    chapter.title.assign(label, labelLen);
                    chapter.title_len = labelLen;
                    m_Chapters.push_back(chapter);
                }
                free(label);
            }
            if (chaptersBefore == m_Chapters.size() && tlen > 0)
            {
                chapter_item_t chapter = {};
                chapter.index = start;
                chapter.title.assign(title, tlen);
                chapter.title_len = tlen;
                m_Chapters.push_back(chapter);
            }
        }
        free(text);
        free(title);
    }
    if (!hasText || m_bForceKill)
        return FALSE;

    // Nested NCX entries may be unordered; chapter jumps require text order.
    std::stable_sort(m_Chapters.begin(), m_Chapters.end(), [](const chapter_item_t &a, const chapter_item_t &b) {
        return a.index < b.index;
    });
    if (document.size() >= SIZE_MAX / sizeof(wchar_t))
        return FALSE;
    wchar_t *text = (wchar_t *)malloc((document.size() + 1) * sizeof(wchar_t));
    if (!text)
        return FALSE;
    memcpy(text, document.c_str(), (document.size() + 1) * sizeof(wchar_t));
    free(m_Text);
    m_Text = text;
    m_Length = (int)document.size();
    return TRUE;
}

BOOL MobiBook::ParserCover(mobi_t &mobi, MOBIData *m)
{
    delete m_Cover;
    m_Cover = NULL;
    if (m_CoverStream)
    {
        m_CoverStream->Release();
        m_CoverStream = NULL;
    }

    MOBIExthHeader *exth = mobi_get_exthrecord_by_tag(m, EXTH_COVEROFFSET);
    if (!exth || !exth->data || exth->size != sizeof(uint32_t))
        return FALSE;
    size_t first = mobi_get_first_resource_record(m);
    uint32_t offset = mobi_decode_exthvalue((unsigned char *)exth->data, exth->size);
    if (first == MOBI_NOTSET || offset == MOBI_NOTSET || first > SIZE_MAX - offset)
        return FALSE;
    MOBIPdbRecord *record = mobi_get_record_by_seqnumber(m, first + offset);
    if (!record || !record->data || record->size < 4 || record->size > UINT_MAX)
        return FALSE;

    IStream *stream = SHCreateMemStream((const BYTE *)record->data, (UINT)record->size);
    if (!stream)
        return FALSE;
    m_Cover = new Gdiplus::Bitmap(stream);
    if (!m_Cover || m_Cover->GetLastStatus() != Gdiplus::Ok)
    {
        delete m_Cover;
        m_Cover = NULL;
        stream->Release();
        return FALSE;
    }
    // GDI+ requires its input stream to stay alive for the bitmap's lifetime.
    // SHCreateMemStream owns a copy, independent of libmobi's freed records.
    m_CoverStream = stream;
    return TRUE;
}
