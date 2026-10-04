// Transcript Studio projects (.tsproj, JSON).
//
// {
//   "format": "transcript-studio/1", "id", "title", "audio", "duration", "language", "created",
//   "engine": {"device", "asr", "diar"},
//   "speakers": [{"id": 1, "name": "Speaker A", "color": "#D9B26A"}, ...],
//   "turns": [{"id", "spk", "s", "e", "text", "orig", "orig_spk", "words": [[w, s, e, conf], ...],
//              "status": "auto" | "check" | "ok"}]
// }
// "orig"/"orig_spk" keep what the AI produced, so Word export can show every human
// correction as a tracked change.
#pragma once

#include <string>

#include "engine.h"
#include "nlohmann/json.hpp"

namespace ts {

using json = nlohmann::json;

constexpr const char* kProjectExt = "tsproj";

// Turns words + speakers into a project. `ui_lang` ("en"/"it") picks the default speaker names.
json build_project(const Transcript& t, const std::string& audio_path, const std::string& title,
                   const std::string& ui_lang);

bool load_project(const std::string& path, json& out, std::string& err);
bool save_project(const std::string& path, const json& project, std::string& err);

// A new, unused file name in the library folder for `title`.
std::string new_project_path(const std::string& title);

// Summary of every project in the library folder, newest first.
json list_projects();

std::string speaker_color(int index);  // muted palette from the design
std::string format_time(double seconds, bool hours = false);  // "03:07" or "0:03:07"

}  // namespace ts
