// macOS implementation of platform.h (Cocoa / Foundation).
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <mach-o/dyld.h>

#include <fstream>
#include <sstream>

#include "platform.h"

namespace ts {

static NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
static std::string str(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

bool file_exists(const std::string& p) {
    BOOL dir = NO;
    return [[NSFileManager defaultManager] fileExistsAtPath:ns(p) isDirectory:&dir] && !dir;
}

bool dir_exists(const std::string& p) {
    BOOL dir = NO;
    return [[NSFileManager defaultManager] fileExistsAtPath:ns(p) isDirectory:&dir] && dir;
}

bool make_dirs(const std::string& p) {
    return [[NSFileManager defaultManager] createDirectoryAtPath:ns(p) withIntermediateDirectories:YES attributes:nil error:nil];
}

std::string file_name(const std::string& p) {
    const size_t i = p.find_last_of('/');
    return i == std::string::npos ? p : p.substr(i + 1);
}

std::string file_stem(const std::string& p) {
    std::string n = file_name(p);
    const size_t dot = n.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

std::string parent_dir(const std::string& p) {
    const size_t i = p.find_last_of('/');
    return i == std::string::npos ? std::string() : p.substr(0, i);
}

bool read_file(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_atomic(const std::string& p, const std::string& data) {
    NSData* d = [NSData dataWithBytes:data.data() length:data.size()];
    return [d writeToFile:ns(p) atomically:YES];
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    for (NSString* n in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:ns(dir) error:nil])
        if (file_exists(join_path(dir, str(n)))) out.push_back(str(n));
    return out;
}

bool delete_file(const std::string& p) { return [[NSFileManager defaultManager] removeItemAtPath:ns(p) error:nil]; }

std::string exe_dir() {
    // Inside an .app bundle the engine and models live in Contents/Resources.
    NSString* res = [[NSBundle mainBundle] resourcePath];
    if (res && [[NSFileManager defaultManager] fileExistsAtPath:[res stringByAppendingPathComponent:@"models"]])
        return str(res);
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) return parent_dir(str([[NSString stringWithUTF8String:buf] stringByResolvingSymlinksInPath]));
    return ".";
}

std::string documents_dir() {
    NSArray* d = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return d.count ? str(d[0]) : str(NSHomeDirectory());
}

std::string library_dir() {
    const std::string d = join_path(documents_dir(), "Transcript Studio");
    make_dirs(d);
    return d;
}

static NSArray<UTType*>* types_for(const std::vector<std::string>& exts) {
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const auto& e : exts) {
        UTType* t = [UTType typeWithFilenameExtension:ns(e)];
        if (t) [types addObject:t];
    }
    return types;
}

std::string pick_open_file(const std::string& title, const std::vector<std::string>& exts) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.message = ns(title);
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (!exts.empty()) panel.allowedContentTypes = types_for(exts);
    if ([panel runModal] != NSModalResponseOK) return "";
    return str(panel.URL.path);
}

std::string pick_save_file(const std::string& title, const std::string& default_name, const std::string& ext) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.message = ns(title);
    panel.nameFieldStringValue = ns(default_name);
    if (!ext.empty()) panel.allowedContentTypes = types_for({ext});
    if ([panel runModal] != NSModalResponseOK) return "";
    return str(panel.URL.path);
}

bool open_with_default_app(const std::string& p) { return [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns(p)]]; }

bool reveal_in_file_manager(const std::string& p) {
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:ns(p)] ]];
    return true;
}

std::string system_ui_language() {
    NSString* first = [NSLocale preferredLanguages].firstObject;
    return first && [first hasPrefix:@"it"] ? "it" : "en";
}

bool has_nvidia_driver() { return false; }
bool has_vulkan_loader() { return false; }

}  // namespace ts
