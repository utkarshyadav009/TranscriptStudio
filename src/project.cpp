#include "project.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>

#include "platform.h"

namespace ts {

std::string speaker_color(int i) {
    static const char* palette[] = {"#D9B26A", "#7FA7C9", "#9DBF8F", "#C98F8F",
                                    "#8FC1BB", "#C9B38F", "#A7A0C9", "#A0A7B4"};
    return palette[(i % 8 + 8) % 8];
}

std::string format_time(double s, bool hours) {
    if (s < 0) s = 0;
    const int t = (int)s, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
    char buf[32];
    if (hours || h)
        std::snprintf(buf, sizeof buf, "%d:%02d:%02d", h, m, sec);
    else
        std::snprintf(buf, sizeof buf, "%02d:%02d", m, sec);
    return buf;
}

static std::string now_iso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

static bool ends_sentence(const std::string& w) {
    return !w.empty() && (w.back() == '.' || w.back() == '?' || w.back() == '!');
}

json build_project(const Transcript& t, const std::string& audio_path, const std::string& title,
                   const std::string& ui_lang) {
    // speakers numbered in the order they first speak (A, B, C, ...)
    std::map<int, int> order;
    for (const auto& w : t.words)
        if (w.speaker > 0 && !order.count(w.speaker)) {
            const int n = (int)order.size() + 1;
            order[w.speaker] = n;
        }
    const bool it = ui_lang == "it";
    json speakers = json::array();
    for (int n = 1; n <= (int)order.size(); n++) {
        const std::string letter(1, char('A' + (n - 1) % 26));
        speakers.push_back({{"id", n}, {"name", (it ? "Interlocutore " : "Speaker ") + letter}, {"color", speaker_color(n - 1)}});
    }
    bool has_unknown = false;

    json turns = json::array();
    json cur;
    auto flush = [&]() {
        if (cur.is_null()) return;
        double lowest = 1.0;
        for (const auto& w : cur["words"]) lowest = std::min(lowest, w[3].get<double>());
        const bool unknown = cur["spk"].get<int>() == 0;
        cur["status"] = (unknown || lowest < 0.45) ? "check" : "auto";
        cur["orig"] = cur["text"];
        cur["orig_spk"] = cur["spk"];
        turns.push_back(cur);
        cur = nullptr;
    };
    int next_id = 1;
    for (const auto& w : t.words) {
        const int spk = w.speaker > 0 ? order[w.speaker] : 0;
        if (spk == 0) has_unknown = true;
        bool split = cur.is_null() || cur["spk"].get<int>() != spk || w.start - cur["e"].get<double>() > 2.0;
        if (!split && w.start - cur["s"].get<double>() > 45.0 && ends_sentence(cur["words"].back()[0].get<std::string>()))
            split = true;  // keep very long monologues readable
        if (split) {
            flush();
            cur = {{"id", next_id++}, {"spk", spk}, {"s", w.start}, {"e", w.end}, {"text", ""}, {"words", json::array()}};
        }
        std::string text = cur["text"].get<std::string>();
        cur["text"] = text.empty() ? w.text : text + " " + w.text;
        cur["e"] = w.end;
        cur["words"].push_back({w.text, w.start, w.end, w.conf});
    }
    flush();
    if (has_unknown)
        speakers.push_back({{"id", 0}, {"name", it ? "Voce non identificata" : "Unknown speaker"}, {"color", "#6B6F76"}});

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    return {{"format", "transcript-studio/1"},
            {"id", std::to_string(ms)},
            {"title", title},
            {"audio", audio_path},
            {"duration", t.duration},
            {"language", t.language},
            {"created", now_iso()},
            {"engine", {{"device", t.device}, {"asr", "Parakeet TDT 0.6B v3"}, {"diar", t.segments.empty() ? "" : "Nemotron 3 Diarization"}}},
            {"speakers", speakers},
            {"turns", turns}};
}

bool load_project(const std::string& path, json& out, std::string& err) {
    std::string text;
    if (!read_file(path, text)) {
        err = "could not read " + path;
        return false;
    }
    try {
        out = json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("the project file is damaged: ") + e.what();
        return false;
    }
    if (!out.is_object() || !out.contains("turns")) {
        err = "not a Transcript Studio project";
        return false;
    }
    out["path"] = path;
    return true;
}

bool save_project(const std::string& path, const json& p, std::string& err) {
    json copy = p;
    copy.erase("path");
    if (!write_file_atomic(path, copy.dump(1))) {
        err = "could not save " + path;
        return false;
    }
    return true;
}

static std::string safe_name(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) out += (c < 32 || std::string("\\/:*?\"<>|").find((char)c) != std::string::npos) ? '_' : (char)c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "Recording" : out;
}

std::string new_project_path(const std::string& title) {
    const std::string base = join_path(library_dir(), safe_name(title));
    std::string p = base + "." + kProjectExt;
    for (int n = 2; file_exists(p); n++) p = base + " (" + std::to_string(n) + ")." + kProjectExt;
    return p;
}

json list_projects() {
    json out = json::array();
    const std::string dir = library_dir();
    for (const auto& name : list_dir(dir)) {
        const std::string ext = std::string(".") + kProjectExt;
        if (name.size() <= ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;
        json p;
        std::string err;
        if (!load_project(join_path(dir, name), p, err)) continue;
        int check = 0;
        for (const auto& t : p["turns"]) check += t.value("status", "") == "check";
        out.push_back({{"path", p["path"]},
                       {"title", p.value("title", name)},
                       {"created", p.value("created", "")},
                       {"duration", p.value("duration", 0.0)},
                       {"language", p.value("language", "")},
                       {"speakers", p["speakers"].size()},
                       {"turns", p["turns"].size()},
                       {"to_check", check}});
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a["created"] > b["created"]; });
    return out;
}

}  // namespace ts
