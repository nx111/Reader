#include "BookmarkActions.h"
#include "BookmarkExport.h"
#include "Theme.h"
#include "Utils.h"
#include "resource.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <string>

namespace {
INT_PTR CALLBACK NoteProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        SetWindowLongPtr(dialog, DWLP_USER, lParam); ThemeAttachWindow(dialog);
        SendDlgItemMessage(dialog, IDC_BOOKMARK_NOTE, EM_SETLIMITTEXT, 500, 0);
        SetDlgItemText(dialog, IDC_BOOKMARK_NOTE, ((std::wstring*)lParam)->c_str());
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t text[501] = {}; GetDlgItemText(dialog, IDC_BOOKMARK_NOTE, text, 501);
            auto note = (std::wstring*)GetWindowLongPtr(dialog, DWLP_USER);
            *note = text;
            if (!note->empty() && note->back() >= 0xd800 && note->back() <= 0xdbff) note->pop_back();
        }
        EndDialog(dialog, LOWORD(wParam)); return TRUE;
    }
    return FALSE;
}
}
BOOL EditReaderBookmarkNote(HWND owner, std::wstring& note)
{
    std::wstring edited = note;
    if (DialogBoxParam((HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE), MAKEINTRESOURCE(IDD_BOOKMARK_NOTE), owner, NoteProc, (LPARAM)&edited) != IDOK) return FALSE;
    note.swap(edited); return TRUE;
}
BOOL ExportReaderBookmarks(HWND owner, const wchar_t* title, const std::vector<bookmark_detail_t>& marks)
{
    wchar_t destination[MAX_PATH] = L"书签.md";
    OPENFILENAME dialog = {}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Markdown (*.md)\0*.md\0纯文本 (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = destination; dialog.nMaxFile = MAX_PATH; dialog.lpstrDefExt = L"md";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileName(&dialog)) return FALSE;
    const std::wstring output = ReaderExportBookmarks(title ? title : L"书签", marks, _wcsicmp(PathFindExtension(destination), L".txt") != 0);
    const std::string bytes = Utf16ToUtf8(output.c_str());
    wchar_t directory[MAX_PATH], temporary[MAX_PATH] = {}; wcscpy(directory, destination); PathRemoveFileSpec(directory);
    BOOL ok = GetTempFileName(directory, L"Rbm", 0, temporary) != 0;
    HANDLE file = ok ? CreateFile(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    DWORD written = 0;
    ok = file != INVALID_HANDLE_VALUE && WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, NULL)
        && written == bytes.size() && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (ok) ok = MoveFileEx(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
    {
        if (temporary[0]) DeleteFile(temporary);
        MessageBox(owner, L"导出失败，原目标文件保持不变。请选择可写入的位置后重试。", L"导出书签", MB_OK | MB_ICONERROR);
    }
    return ok;
}
