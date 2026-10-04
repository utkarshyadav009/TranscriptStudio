// Small platform layer: paths, files, dialogs and "open with the default app".
// Implemented in platform_win.cpp (Windows) and platform_posix.cpp / platform_mac.mm.
#pragma once

#include <string>
#include <vector>

namespace ts {

// Paths are UTF-8 everywhere in the app.
std::string join_path(const std::string& a, const std::string& b);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
bool make_dirs(const std::string& path);
std::string file_name(const std::string& path);   // "C:\x\talk.m4a" -> "talk.m4a"
std::string file_stem(const std::string& path);   // -> "talk"
std::string parent_dir(const std::string& path);
bool read_file(const std::string& path, std::string& out);
bool write_file_atomic(const std::string& path, const std::string& data);  // temp file + rename
std::vector<std::string> list_dir(const std::string& dir);                 // file names only

std::string exe_dir();        // folder of the running program
std::string documents_dir();  // the user's Documents folder
std::string library_dir();    // Documents/Transcript Studio (created on demand)

// Native dialogs; return "" if the user cancels.
std::string pick_open_file(const std::string& title, const std::vector<std::string>& extensions);
std::string pick_save_file(const std::string& title, const std::string& default_name, const std::string& extension);

// Opens a file with its default program (for example a .docx in Word).
bool open_with_default_app(const std::string& path);
// Shows the file in Explorer / Finder.
bool reveal_in_file_manager(const std::string& path);

bool delete_file(const std::string& path);

// "it" if the operating system's interface language is Italian, otherwise "en".
std::string system_ui_language();

// Is a CUDA-capable NVIDIA driver / a Vulkan loader present? (used to pick the engine flavour)
bool has_nvidia_driver();
bool has_vulkan_loader();

#if defined(_WIN32)
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
#endif

}  // namespace ts
