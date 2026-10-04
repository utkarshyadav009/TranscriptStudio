// Transcript Studio desktop app: a system web view (WebView2 / WebKit) shows the
// interface from ui/ (embedded in the program); this file connects it to the engine,
// the audio player, project files and Word import/export.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "audio_decode.h"
#include "docx.h"
#include "engine.h"
#include "platform.h"
#include "player.h"
#include "project.h"
#include "webview/webview.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ts {

struct UiFile {
    const char* name;
    const unsigned char* data;
    unsigned long size;
};
extern const UiFile kUiFiles[];
extern const int kUiFileCount;

namespace {

std::string base64(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += tbl[v >> 18], out += tbl[(v >> 12) & 63], out += tbl[(v >> 6) & 63], out += tbl[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < in.size()) v |= (unsigned char)in[i + 1] << 8;
        out += tbl[v >> 18], out += tbl[(v >> 12) & 63];
        out += i + 1 < in.size() ? tbl[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}

// UI files come from the program itself, or from TS_UI_DIR while developing.
std::string ui_file(const std::string& name) {
    if (const char* dir = std::getenv("TS_UI_DIR")) {
        std::string s;
        if (read_file(join_path(dir, name), s)) return s;
    }
    for (int i = 0; i < kUiFileCount; i++)
        if (name == kUiFiles[i].name) return std::string(reinterpret_cast<const char*>(kUiFiles[i].data), kUiFiles[i].size);
    return {};
}

std::string build_html() {
    std::string html = ui_file("index.html"), css = ui_file("app.css");
    for (const char* f : {"PlayfairDisplay", "JetBrainsMono", "Inter"})
        replace_all(css, std::string("{{FONT:") + f + "}}",
                    "data:font/woff2;base64," + base64(ui_file(std::string("fonts/") + f + ".woff2")));
    replace_all(html, "/*{{CSS}}*/", css);
    replace_all(html, "/*{{I18N}}*/", ui_file("i18n.js"));
    replace_all(html, "/*{{JS}}*/", ui_file("app.js"));
    return html;
}

json args(const std::string& req) {
    try {
        json a = json::parse(req);
        return a.is_array() ? a : json::array();
    } catch (...) {
        return json::array();
    }
}

// Arguments arrive as a JSON array: ts_play(12.5) -> [12.5]
std::string s_at(const json& a, size_t i) { return i < a.size() && a[i].is_string() ? a[i].get<std::string>() : std::string(); }
double d_at(const json& a, size_t i, double def) { return i < a.size() && a[i].is_number() ? a[i].get<double>() : def; }

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

class App {
   public:
    explicit App(webview::webview& w) : w_(w) {
        settings_path_ = join_path(library_dir(), "settings.json");
        std::string s;
        if (read_file(settings_path_, s)) try { settings_ = json::parse(s); } catch (...) {}
        if (!settings_.is_object()) settings_ = json::object();
        if (!settings_.contains("uiLang")) settings_["uiLang"] = system_ui_language();
    }
    ~App() {
        cancel_ = true;
        if (worker_.joinable()) worker_.join();
        if (loader_.joinable()) loader_.join();
        player_.unload();
    }

    void bind_all() {
        sync("init", [this](const json&) {
            json r = {{"uiLang", settings_.value("uiLang", "en")}, {"reviewer", settings_.value("reviewer", "")}};
            if (const char* f = std::getenv("TS_AUTOTEST")) {
                std::string code;
                if (read_file(f, code)) r["autotest"] = code;
            }
            return r;
        });
        sync("settings", [this](const json& a) {
            if (!a.empty() && a[0].is_object())
                for (auto& [k, v] : a[0].items()) settings_[k] = v;
            write_file_atomic(settings_path_, settings_.dump(1));
            return json{{"ok", true}};
        });
        sync("listProjects", [](const json&) { return list_projects(); });
        sync("openProject", [this](const json& a) { return open_project(s_at(a, 0)); });
        sync("saveProject", [this](const json& a) {
            if (a.empty() || !a[0].is_object()) return json{{"ok", false}, {"error", "nothing to save"}};
            std::string err;
            std::lock_guard<std::mutex> lk(m_);
            project_ = a[0];
            const bool ok = save_project(project_path_, project_, err);
            return ok ? json{{"ok", true}} : json{{"ok", false}, {"error", err}};
        });
        sync("deleteProject", [this](const json& a) {
            const std::string p = s_at(a, 0);
            if (p == project_path_) {
                player_.unload();
                project_ = json();
                project_path_.clear();
            }
            return json{{"ok", delete_file(p)}};
        });
        sync("pickAudio", [this](const json&) -> json {
            const bool it = lang() == "it";
            const std::string p = pick_open_file(it ? "Scegli una registrazione" : "Choose a recording",
                                                 {"mp3", "m4a", "wav", "flac", "aac", "ogg", "wma", "mp4", "mov", "m4v", "mkv", "webm"});
            if (p.empty()) return nullptr;
            return {{"path", p}, {"name", file_name(p)}, {"title", file_stem(p)}};
        });
        sync("transcribe", [this](const json& a) { return start_transcription(s_at(a, 0), s_at(a, 1)); });
        sync("cancel", [this](const json&) {
            cancel_ = true;
            return json{{"ok", true}};
        });
        sync("play", [this](const json& a) {
            if (!a.empty() && a[0].is_number()) player_.seek(a[0].get<double>());
            player_.play();
            return json{{"ok", true}};
        });
        sync("pause", [this](const json&) {
            player_.pause();
            return json{{"ok", true}};
        });
        sync("seek", [this](const json& a) {
            player_.seek(d_at(a, 0, 0.0));
            return json{{"ok", true}};
        });
        sync("playRange", [this](const json& a) {
            player_.play_range(d_at(a, 0, 0.0), d_at(a, 1, 0.0));
            return json{{"ok", true}};
        });
        sync("speed", [this](const json& a) {
            player_.set_speed(d_at(a, 0, 1.0));
            return json{{"ok", true}};
        });
        sync("state", [this](const json&) {
            return json{{"pos", player_.position()}, {"dur", player_.duration()}, {"playing", player_.playing()}};
        });
        sync("peaks", [this](const json& a) {
            json flat = json::array();
            for (const auto& [lo, hi] : peaks(player_.samples(), d_at(a, 0, 0.0), d_at(a, 1, 0.0), (int)d_at(a, 2, 400)))
                flat.push_back(lo), flat.push_back(hi);
            return flat;
        });
        sync("reveal", [](const json& a) { return json{{"ok", reveal_in_file_manager(s_at(a, 0))}}; });
        sync("relocateAudio", [this](const json&) -> json {
            const std::string p = pick_open_file(lang() == "it" ? "Trova il file audio" : "Find the audio file",
                                                 {"mp3", "m4a", "wav", "flac", "aac", "ogg", "wma", "mp4", "mov", "m4v"});
            if (p.empty()) return nullptr;
            {
                std::lock_guard<std::mutex> lk(m_);
                project_["audio"] = p;
            }
            load_audio_async(p);
            return {{"path", p}};
        });
        sync("exportWord", [this](const json&) -> json {
            const std::string p = pick_save_file(lang() == "it" ? "Esporta in Word" : "Export to Word",
                                                 project_.value("title", std::string("Transcript")) + ".docx", "docx");
            if (p.empty()) return {{"cancelled", true}};
            return export_word(p, false);
        });
        sync("openInWord", [this](const json&) {
            const std::string p = word_copy_path();
            json r = export_word(p, true);
            if (r.value("ok", false)) {
                std::lock_guard<std::mutex> lk(m_);
                project_["word_path"] = p;
                std::string err;
                save_project(project_path_, project_, err);
            }
            return r;
        });
        sync("importWord", [this](const json&) -> json {
            std::string p = project_.value("word_path", std::string());
            if (p.empty() || !file_exists(p))
                p = pick_open_file(lang() == "it" ? "Scegli il documento Word" : "Choose the Word document", {"docx"});
            if (p.empty()) return {{"cancelled", true}};
            std::lock_guard<std::mutex> lk(m_);
            json copy = project_;
            ImportReport rep;
            std::string err;
            if (!import_docx(p, copy, rep, err)) return {{"ok", false}, {"error", err}};
            project_ = copy;
            save_project(project_path_, project_, err);
            json out = project_;
            out["path"] = project_path_;
            return {{"ok", true}, {"project", out},
                    {"report", {{"updated", rep.updated}, {"added", rep.added}, {"removed", rep.removed}}}};
        });
        sync("exportText", [this](const json&) -> json {
            const std::string p = pick_save_file(lang() == "it" ? "Esporta testo" : "Export text",
                                                 project_.value("title", std::string("Transcript")) + ".txt", "txt");
            if (p.empty()) return {{"cancelled", true}};
            std::string err;
            if (!export_text(project_, p, err)) return {{"ok", false}, {"error", err}};
            return {{"ok", true}, {"path", p}};
        });
    }

   private:
    std::string lang() const { return settings_.value("uiLang", std::string("en")); }

    void sync(const std::string& name, std::function<json(const json&)> fn) {
        w_.bind("ts_" + name, [fn](const std::string& req) -> std::string {
            try {
                return fn(args(req)).dump();
            } catch (const std::exception& e) {
                return json{{"ok", false}, {"error", e.what()}}.dump();
            }
        });
    }

    // Runs `js` on the UI thread (safe from any thread).
    void call_js(const std::string& js) {
        w_.dispatch([this, js] { w_.eval(js); });
    }

    json open_project(const std::string& path) {
        json p;
        std::string err;
        if (!load_project(path, p, err)) return {{"ok", false}, {"error", err}};
        {
            std::lock_guard<std::mutex> lk(m_);
            project_ = p;
            project_path_ = path;
        }
        player_.unload();
        load_audio_async(p.value("audio", std::string()));
        return {{"ok", true}, {"project", p}};
    }

    void load_audio_async(const std::string& audio) {
        const int gen = ++audio_gen_;
        if (loader_.joinable()) loader_.join();
        loader_ = std::thread([this, audio, gen] {
            auto pcm = std::make_shared<std::vector<float>>();
            std::string err;
            const bool ok = file_exists(audio) && decode_audio(audio, *pcm, err);
            w_.dispatch([this, pcm, ok, gen] {
                if (gen != audio_gen_) return;  // another project was opened meanwhile
                std::string e;
                const bool loaded = ok && player_.load(std::move(*pcm), e);
                w_.eval("TS.onAudio(" + json{{"ok", loaded}, {"duration", player_.duration()}, {"error", e}}.dump() + ")");
            });
        });
    }

    json start_transcription(const std::string& path, const std::string& title) {
        if (busy_) return {{"ok", false}, {"error", "a transcription is already running"}};
        if (!file_exists(path)) return {{"ok", false}, {"error", "file not found: " + path}};
        busy_ = true;
        cancel_ = false;
        if (worker_.joinable()) worker_.join();
        worker_ = std::thread([this, path, title] {
            json done = run_transcription(path, title);
            busy_ = false;
            call_js("TS.onDone(" + done.dump() + ")");
        });
        return {{"ok", true}};
    }

    json run_transcription(const std::string& path, const std::string& title) {
        const double t0 = now_s();
        int last_pct = -1;
        std::string last_stage;
        auto progress = [&](const char* stage, double f, const std::string& device) {
            const int pct = (int)(f * 1000);
            if (pct == last_pct && stage == last_stage) return;
            last_pct = pct;
            last_stage = stage;
            const double el = now_s() - t0;
            const double eta = f > 0.05 ? el / f - el : -1;
            call_js("TS.onProgress(" + json{{"stage", stage}, {"fraction", f}, {"device", device}, {"eta", eta}}.dump() + ")");
        };
        progress("read", 0.0, "");
        std::vector<float> pcm;
        std::string err;
        if (!decode_audio(path, pcm, err)) return {{"ok", false}, {"error", err}};
        if (cancel_) return {{"ok", false}, {"cancelled", true}};

        std::lock_guard<std::mutex> lk(engine_m_);
        if (!engine_.is_open()) {
            progress("words", 0.0, "");
            EngineConfig cfg;
            cfg.engine_root = exe_dir();
            std::string models = join_path(exe_dir(), "models");
            if (const char* m = std::getenv("TS_MODELS")) models = m;
            if (const char* e = std::getenv("TS_ENGINE_DIRS")) {  // "cuda=dir;cpu=dir" while developing
                std::string s = e;
                for (size_t p = 0; p < s.size();) {
                    size_t q = s.find(';', p);
                    if (q == std::string::npos) q = s.size();
                    const std::string item = s.substr(p, q - p);
                    const size_t eq = item.find('=');
                    if (eq != std::string::npos) cfg.flavours.emplace_back(item.substr(0, eq), item.substr(eq + 1));
                    p = q + 1;
                }
            }
            cfg.asr_model = join_path(models, "parakeet-tdt-0.6b-v3.q8_0.gguf");
            cfg.diar_model = join_path(models, "Nemotron-3-Diarization.q8_0.gguf");
            if (settings_.value("device", std::string()) == "cpu") cfg.device = Device::Cpu;
            if (!engine_.open(cfg, err)) return {{"ok", false}, {"error", "The speech engine could not start:\n" + err}};
        }
        const std::string device = engine_.device();
        Transcript tr;
        const bool ok = engine_.run(pcm, "", [&](const Progress& p) {
            progress(p.stage == Progress::Speakers ? "speakers" : p.stage == Progress::Done ? "done" : "words", p.fraction, device);
            return !cancel_.load();
        }, tr, err);
        if (!ok) return cancel_ ? json{{"ok", false}, {"cancelled", true}} : json{{"ok", false}, {"error", err}};
        json project = build_project(tr, path, title.empty() ? file_stem(path) : title, lang());
        const std::string out = new_project_path(project["title"].get<std::string>());
        if (!save_project(out, project, err)) return {{"ok", false}, {"error", err}};
        return {{"ok", true}, {"path", out}};
    }

    std::string word_copy_path() {
        const std::string title = project_.value("title", std::string("Transcript"));
        std::string safe;
        for (const char c : title) safe += std::string("\\/:*?\"<>|").find(c) == std::string::npos ? c : '_';
        return join_path(library_dir(), safe + (lang() == "it" ? " (per Word).docx" : " (for Word).docx"));
    }

    json export_word(const std::string& path, bool open) {
        DocxOptions opt;
        opt.author = settings_.value("reviewer", std::string());
        opt.ui_lang = lang();
        std::string err;
        json copy;
        {
            std::lock_guard<std::mutex> lk(m_);
            copy = project_;
        }
        if (!export_docx(copy, path, opt, err)) return {{"ok", false}, {"error", err}};
        if (open) open_with_default_app(path);
        return {{"ok", true}, {"path", path}};
    }

    webview::webview& w_;
    json settings_;
    std::string settings_path_;
    std::mutex m_;
    json project_;
    std::string project_path_;
    Player player_;
    std::mutex engine_m_;
    Engine engine_;
    std::thread worker_, loader_;
    std::atomic<bool> cancel_{false}, busy_{false};
    std::atomic<int> audio_gen_{0};
};

int run_app() {
#if defined(_WIN32)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    const bool debug = std::getenv("TS_DEBUG") != nullptr;
    webview::webview w(debug, nullptr);
    w.set_title("Transcript Studio");
    w.set_size(1440, 900, WEBVIEW_HINT_NONE);
    w.set_size(980, 620, WEBVIEW_HINT_MIN);
#if defined(_WIN32)
    // open maximised so the player is never hidden behind the taskbar on smaller screens
    if (auto hwnd = w.window(); hwnd.ok()) ShowWindow(static_cast<HWND>(hwnd.value()), SW_MAXIMIZE);
#endif
    App app(w);
    app.bind_all();
    w.set_html(build_html());
    w.run();
    return 0;
}

}  // namespace ts

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return ts::run_app(); }
#else
int main() { return ts::run_app(); }
#endif
