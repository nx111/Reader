// Built by test_mobi_parser.py with the production parser, not a parser copy.
#include "mobi_parser_compat.h"
#include "MobiBook.h"
#include "resource.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #condition); } while (0)

class TestBook : public MobiBook {
    std::map<std::string, std::string> storage;
public:
    using MobiBook::UnzipBook;
    using MobiBook::ParserOps;
    using MobiBook::ParserOpf;
    using MobiBook::ParserNcx;
    using MobiBook::ParserChapters;
    using MobiBook::ParserCover;
    using MobiBook::GetCover;
    using MobiBook::GetTextBeginIndex;
    BOOL Load(const std::string &filename)
    {
        CloseBook();
        const std::wstring wide = TestWide(filename);
        SetFileName(wide.c_str());
        return ParserBook(nullptr);
    }
    void Add(const std::string &name, const std::string &data)
    {
        storage[name] = data;
        file_data_t file = { const_cast<char *>(storage[name].data()), static_cast<int>(data.size()) };
        m_flist[name] = file;
    }
    std::wstring Text() const { return m_Text ? std::wstring(m_Text, m_Length) : std::wstring(); }
    const chapters_t &Chapters() const { return m_Chapters; }
    int Length() const { return m_Length; }
};

struct Package {
    mobi_t mobi;
    ~Package()
    {
        for (const auto &item : mobi.manifests) delete item.second;
        for (const auto &item : mobi.navpoints) delete item.second;
    }
};

static void CheckChapters(const TestBook &book)
{
    int previous = -1;
    for (const auto &chapter : book.Chapters()) {
        REQUIRE(chapter.index >= 0);
        REQUIRE(chapter.index < book.Length());
        REQUIRE(chapter.index >= previous);
        REQUIRE(!chapter.title.empty());
        REQUIRE(chapter.title_len == static_cast<int>(chapter.title.size()));
        previous = chapter.index;
    }
}

static void MarkupAnchors()
{
    TestBook book;
    const std::string html = u8"<html><head><meta charset='utf-8'/><title>Metadata &amp; 标题</title></head>"
        u8"<body><p>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Alpha &copy; 2026&nbsp;中文🙂</p>\n\n"
        u8"<p>　   Mixed indent</p>\n<h2 id='chapter-one'>Repeated</h2><p>Repeated is not another chapter</p>"
        u8"<a name='legacy'></a><h2 id='chapter-two'>Repeated</h2><p>Tail</p></body></html>";
    file_data_t file = { const_cast<char *>(html.data()), static_cast<int>(html.size()) };
    wchar_t *text = nullptr, *title = nullptr;
    int length = 0, titleLength = 0;
    std::map<std::string, int> anchors = {
        {"chapter-one", -1}, {"chapter-two", -1}, {"legacy", -1}, {"missing", -1}
    };
    REQUIRE(book.ParserOps(&file, &text, &length, &title, &titleLength, TRUE, &anchors));
    const std::wstring body(text, length), heading(title, titleLength);
    std::free(text);
    std::free(title);
    REQUIRE(heading == TestWide(u8"Metadata & 标题"));
    REQUIRE(body.find(TestWide(u8"© 2026 中文🙂")) != std::wstring::npos);
    REQUIRE(anchors["chapter-one"] >= 0);
    REQUIRE(anchors["chapter-two"] > anchors["chapter-one"]);
    REQUIRE(body.substr(anchors["chapter-one"], 8) == L"Repeated");
    REQUIRE(body.substr(anchors["chapter-two"], 8) == L"Repeated");
    REQUIRE(anchors["legacy"] >= 0 && anchors["legacy"] <= anchors["chapter-two"]);
    REQUIRE(anchors["missing"] == -1);
    // UTF-16 surrogate units (not Linux's Unicode scalar wchar_t count).
    const auto emoji = body.find(TestWide(u8"🙂"));
    REQUIRE(emoji != std::wstring::npos);
    REQUIRE(body[emoji] >= 0xd800 && body[emoji] <= 0xdbff);
}

static void MarkupEncoding()
{
    const std::string content = u8"<title>标题🙂</title></head><body><p>正文🙂</p>"
        u8"<h2 id='章节🙂'>尾声🙂</h2></body></html>";
    const std::vector<std::string> cases = {
        "<html><head>" + content,
        "<?xml version='1.0' encoding='utf-8'?><html><head>" + content,
        "<html><head><meta http-equiv='Content-Type' content='text/html; charset=windows-1252'/>" + content
    };
    for (const auto &html : cases) {
        TestBook book;
        file_data_t file = { const_cast<char *>(html.data()), static_cast<int>(html.size()) };
        wchar_t *text = nullptr, *title = nullptr;
        int length = 0, titleLength = 0;
        std::map<std::string, int> anchors = { {u8"章节🙂", -1} };
        REQUIRE(book.ParserOps(&file, &text, &length, &title, &titleLength, TRUE, &anchors));
        const std::wstring body(text, length), heading(title, titleLength);
        std::free(text);
        std::free(title);
        REQUIRE(heading == TestWide(u8"标题🙂"));
        REQUIRE(body.find(TestWide(u8"正文🙂")) != std::wstring::npos);
        REQUIRE(anchors[u8"章节🙂"] >= 0);
        REQUIRE(body.substr(anchors[u8"章节🙂"], TestWide(u8"尾声🙂").size()) == TestWide(u8"尾声🙂"));
    }
}

static void Navigation()
{
    TestBook book;
    Package package;
    package.mobi.opf = "book.opf";
    book.Add("book.opf", R"(<package xmlns='http://www.idpf.org/2007/opf'><manifest>
<item id='first' href='part00001.html' media-type='application/xhtml+xml'/>
<item id='second' href='part00000.html' media-type='application/xhtml+xml'/>
<item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>
</manifest><spine toc='ncx'><itemref idref='first'/><itemref idref='second'/></spine></package>)");
    book.Add("toc.ncx", u8R"(<ncx xmlns='http://www.daisy.org/z3986/2005/ncx/'><navMap>
<navPoint id='one' playOrder='1'><navLabel><text>First chapter</text></navLabel><content src='part00001.html#intro'/>
<navPoint id='two' playOrder='2'><navLabel><text>Second chapter</text></navLabel><content src='part00001.html#repeat'/></navPoint></navPoint>
<navPoint id='three' playOrder='3'><navLabel><text>末章</text></navLabel><content src='part00000.html#%E7%AB%A0'/></navPoint>
<navPoint id='duplicate' playOrder='4'><navLabel><text>Duplicate destination</text></navLabel><content src='part00001.html#repeat'/></navPoint>
</navMap></ncx>)");
    book.Add("part00001.html", "<html><head><title>Fallback one</title></head><body>\n\n"
        "<h1 id='intro'>Repeated</h1><p>Repeated appears in ordinary text.</p>"
        "<h2 id='repeat'>Repeated</h2><p>Second body</p></body></html>");
    book.Add("part00000.html", u8"<html><head><meta charset='utf-8'/><title>Fallback two</title></head>"
        u8"<body><h1 id='章'>Ending</h1><p>Last body</p></body></html>");
    REQUIRE(book.ParserOpf(package.mobi));
    REQUIRE(book.ParserNcx(package.mobi));
    REQUIRE(book.ParserChapters(package.mobi));
    const auto &chapters = book.Chapters();
    const auto text = book.Text();
    REQUIRE(chapters.size() == 3);
    REQUIRE(chapters[0].title == L"First chapter");
    REQUIRE(chapters[1].title == L"Second chapter");
    REQUIRE(chapters[2].title == TestWide(u8"末章"));
    REQUIRE(text.substr(chapters[0].index, 8) == L"Repeated");
    REQUIRE(text.substr(chapters[1].index, 8) == L"Repeated");
    REQUIRE(text.substr(chapters[2].index, 6) == L"Ending");
    REQUIRE(chapters[1].index > chapters[0].index + 8);
    REQUIRE(text.find(L"Second body") < text.find(L"Ending"));
    CheckChapters(book);
}

static void NoTocAndEmptyBody()
{
    TestBook book;
    Package package;
    REQUIRE(book.ParserNcx(package.mobi));
    package.mobi.ncx = "missing.ncx";
    REQUIRE(book.ParserNcx(package.mobi));
    package.mobi.ncx = "broken.ncx";
    book.Add("broken.ncx", "<ncx><navMap><navPoint>");
    REQUIRE(book.ParserNcx(package.mobi));
    REQUIRE(package.mobi.navpoints.empty());
    auto item = new manifest_t;
    item->id = "main";
    item->href = "part00000.html";
    item->media_type = "application/xhtml+xml";
    package.mobi.manifests[item->id] = item;
    package.mobi.spines.push_back(item->id);
    book.Add(item->href, "<html><head><title>Fallback title</title></head><body>X</body></html>");
    REQUIRE(book.ParserChapters(package.mobi));
    REQUIRE(book.Text() == L"X");
    REQUIRE(!book.Chapters().empty());
    CheckChapters(book);

    TestBook empty;
    empty.Add(item->href, "<html><body></body></html>");
    REQUIRE(!empty.ParserChapters(package.mobi));
    REQUIRE(empty.Length() == 0);
}

static void CorruptPackageBoundaries()
{
    TestBook book;
    Package package;
    package.mobi.opf = "bad.opf";
    book.Add("bad.opf", R"(<package><manifest>
<item id='exists' href='part.html' media-type='application/xhtml+xml'/>
</manifest><spine><itemref idref='missing'/></spine></package>)");
    REQUIRE(!book.ParserOpf(package.mobi));

    MOBIData mobi = {};
    MOBIRawml rawml = {};
    MOBIPart part = {};
    part.type = T_HTML;
    part.size = static_cast<size_t>(INT_MAX) + 1;
    // No large allocation is needed: the boundary must be rejected before any
    // dereference or narrowing conversion to file_data_t::size (an int).
    part.data = reinterpret_cast<unsigned char *>(1);
    rawml.markup = &part;
    REQUIRE(!book.UnzipBook(&rawml, &mobi, package.mobi));
    part.size = 1;
    part.data = nullptr;
    REQUIRE(!book.UnzipBook(&rawml, &mobi, package.mobi));
    rawml.markup = nullptr;
    rawml.resources = &part;
    REQUIRE(!book.UnzipBook(&rawml, &mobi, package.mobi));
    rawml.resources = nullptr;
    MOBIPart root = {};
    root.next = &part;
    rawml.flow = &root; // The first raw HTML flow is intentionally skipped.
    REQUIRE(!book.UnzipBook(&rawml, &mobi, package.mobi));
}

static void LoadFile(const std::string &filename, const std::string &expectation)
{
    TestBook book;
    const BOOL loaded = book.Load(filename);
    if (expectation == "ok" || expectation == "cover" || expectation == "plain" || expectation == "text" || expectation == "htmlpalm") {
        REQUIRE(loaded);
        REQUIRE(book.Length() > 0);
        REQUIRE(!book.Text().empty());
        if (expectation != "plain" && expectation != "text") REQUIRE(!book.Chapters().empty());
        REQUIRE(book.GetOpenErrorMessage() == 0);
        CheckChapters(book);
        if (expectation == "cover") REQUIRE(book.GetCover() != nullptr);
        if (expectation == "text")
            REQUIRE(book.Text().find(L"This is a sample for testing libmobi project.") != std::wstring::npos);
        if (expectation == "htmlpalm") {
            REQUIRE(book.Text().find(TestWide(u8"HTML PalmDOC & Café")) != std::wstring::npos);
            REQUIRE(book.Text().find(L"literal <safe>") != std::wstring::npos);
            REQUIRE(book.Chapters().front().title == L"HTML PalmDOC title");
        }
        if (expectation == "plain") {
            REQUIRE(book.Text().find(L"First plain PalmDOC line.") != std::wstring::npos);
            REQUIRE(book.Text().find(L"Second plain PalmDOC line.") != std::wstring::npos);
            REQUIRE(book.Text().find(L"A <not-a-tag> B &copy;") != std::wstring::npos);
            REQUIRE(book.Text().find(TestWide(u8"Café plain text.")) != std::wstring::npos);
        }
    } else {
        REQUIRE(!loaded);
        REQUIRE(book.Length() == 0);
        REQUIRE(book.Text().empty());
        REQUIRE(book.Chapters().empty());
        if (expectation == "encrypted")
            REQUIRE(book.GetOpenErrorMessage() == IDS_MOBI_ENCRYPTED);
        else
            REQUIRE(book.GetOpenErrorMessage() == IDS_MOBI_INVALID ||
                book.GetOpenErrorMessage() == IDS_MOBI_UNSUPPORTED);
    }
    // Loading another file through the same instance must not retain failed
    // resources, text, chapter entries, or an old error code.
    if (expectation == "ok" || expectation == "cover" || expectation == "plain" || expectation == "text" || expectation == "htmlpalm") {
        REQUIRE(!book.Load(filename + ".does-not-exist"));
        REQUIRE(book.Length() == 0);
        REQUIRE(book.GetCover() == nullptr);
        REQUIRE(book.Load(filename));
        REQUIRE(book.GetOpenErrorMessage() == 0);
        CheckChapters(book);
    }
}

int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "synthetic") {
            MarkupAnchors();
            Navigation();
            NoTocAndEmptyBody();
            CorruptPackageBoundaries();
        } else if (argc == 2 && std::string(argv[1]) == "encoding") {
            MarkupEncoding();
        } else if (argc == 4 && std::string(argv[1]) == "load") {
            LoadFile(argv[2], argv[3]);
        } else {
            std::cerr << "usage: mobi-parser-test synthetic|encoding | load FILE ok|cover|encrypted|reject\n";
            return 2;
        }
        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
