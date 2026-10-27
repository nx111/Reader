#pragma once
#include "BookmarkData.h"
#include <sstream>

inline std::wstring ReaderExportEscape(const std::wstring& value, bool markdown)
{
    std::wstring result;
    for (wchar_t c : value)
    {
        if (c < 32 && c != L'\n' && c != L'\t') continue;
        if (markdown && (c == L'\\' || c == L'*' || c == L'_' || c == L'[' || c == L']'
            || c == L'#' || c == L'>' || c == L'`')) result += L'\\';
        result += c;
    }
    return result;
}
inline std::wstring ReaderExportBookmarks(const std::wstring& title,
    const std::vector<bookmark_detail_t>& marks, bool markdown)
{
    std::wostringstream out;
    out << (markdown ? L"# " : L"") << ReaderExportEscape(title, markdown) << L"\r\n\r\n";
    for (size_t i = 0; i < marks.size(); ++i)
    {
        const auto& mark = marks[i];
        out << (markdown ? L"## " : L"") << i + 1 << L". " << ReaderExportEscape(mark.title, markdown)
            << L"\r\n" << L"位置: " << mark.position + 1 << L"\r\n";
        if (!mark.summary.empty()) out << ReaderExportEscape(mark.summary, markdown) << L"\r\n";
        if (!mark.note.empty()) out << L"备注: " << ReaderExportEscape(mark.note, markdown) << L"\r\n";
        out << L"\r\n";
    }
    return out.str();
}
