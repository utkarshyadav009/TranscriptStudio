// Word round trip.
//   export_docx: the transcript as a Word document. Everything a person changed after the
//                automatic transcript (words and speaker labels) is a tracked change under
//                their name, so the record of checking is visible in Word.
//   import_docx: reads a document that was edited in Word (changes accepted or not) and
//                brings the text back into the project. Each turn carries a hidden bookmark
//                (ts_turn_<id>), so edits find their turn even if timestamps were changed.
#pragma once

#include <string>

#include "project.h"

namespace ts {

struct DocxOptions {
    std::string author = "Reviewer";  // name shown on tracked changes
    std::string ui_lang = "en";       // language of the few fixed phrases in the document
};

bool export_docx(const json& project, const std::string& path, const DocxOptions& opt, std::string& err);

struct ImportReport {
    int updated = 0, added = 0, removed = 0, speakers_added = 0;
};
bool import_docx(const std::string& path, json& project, ImportReport& rep, std::string& err);

bool export_text(const json& project, const std::string& path, std::string& err);

}  // namespace ts
