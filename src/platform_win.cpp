// Windows implementation of platform.h
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "platform.h"

namespace ts {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '\\' || last == '/') ? a + b : a + "\\" + b;
}

bool file_exists(const std::string& p) {
    const DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::string& p) {
    const DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool make_dirs(const std::string& p) {
    if (p.empty() || dir_exists(p)) return true;
    return SHCreateDirectoryExW(nullptr, widen(p).c_str(), nullptr) == ERROR_SUCCESS || dir_exists(p);
}

static size_t last_sep(const std::string& p) { return p.find_last_of("\\/"); }

std::string file_name(const std::string& p) {
    const size_t i = last_sep(p);
    return i == std::string::npos ? p : p.substr(i + 1);
}

std::string file_stem(const std::string& p) {
    std::string n = file_name(p);
    const size_t dot = n.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

std::string parent_dir(const std::string& p) {
    const size_t i = last_sep(p);
    return i == std::string::npos ? std::string() : p.substr(0, i);
}

bool read_file(const std::string& p, std::string& out) {
    std::ifstream f(widen(p), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_atomic(const std::string& p, const std::string& data) {
    const std::wstring tmp = widen(p + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), widen(p).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(join_path(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(narrow(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::string exe_dir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return parent_dir(narrow(std::wstring(buf, n)));
}

std::string documents_dir() {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) out = narrow(p);
    CoTaskMemFree(p);
    return out;
}

std::string library_dir() {
    const std::string d = join_path(documents_dir(), "Transcript Studio");
    make_dirs(d);
    return d;
}

static std::string run_file_dialog(bool save, const std::string& title, const std::vector<std::string>& exts,
                                   const std::string& default_name) {
    std::string result;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   save ? IID_IFileSaveDialog : IID_IFileOpenDialog, (void**)&dlg))) {
        dlg->SetTitle(widen(title).c_str());
        std::wstring spec;
        for (const auto& e : exts) spec += (spec.empty() ? L"" : L";") + std::wstring(L"*.") + widen(e);
        COMDLG_FILTERSPEC filters[2] = {{L"Supported files", spec.c_str()}, {L"All files", L"*.*"}};
        if (!exts.empty()) dlg->SetFileTypes(2, filters);
        if (save) {
            dlg->SetFileName(widen(default_name).c_str());
            if (!exts.empty()) dlg->SetDefaultExtension(widen(exts[0]).c_str());
        }
        if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) result = narrow(path);
                CoTaskMemFree(path);
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
}

std::string pick_open_file(const std::string& title, const std::vector<std::string>& exts) {
    return run_file_dialog(false, title, exts, "");
}

std::string pick_save_file(const std::string& title, const std::string& default_name, const std::string& ext) {
    return run_file_dialog(true, title, {ext}, default_name);
}

bool open_with_default_app(const std::string& p) {
    return (INT_PTR)ShellExecuteW(nullptr, L"open", widen(p).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

bool reveal_in_file_manager(const std::string& p) {
    const std::wstring args = L"/select,\"" + widen(p) + L"\"";
    return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

bool delete_file(const std::string& p) { return DeleteFileW(widen(p).c_str()) != 0; }

std::string system_ui_language() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ITALIAN ? "it" : "en";
}

static bool can_load(const wchar_t* dll) {
    HMODULE h = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return false;
    FreeLibrary(h);
    return true;
}

bool has_nvidia_driver() { return can_load(L"nvcuda.dll"); }
bool has_vulkan_loader() { return can_load(L"vulkan-1.dll"); }

}  // namespace ts
