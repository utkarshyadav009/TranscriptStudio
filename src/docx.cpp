#include "docx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <map>
#include <regex>
#include <sstream>

#include "miniz.h"
#include "platform.h"

namespace ts {

namespace {

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

std::string squash(const std::string& s) {
    std::string out;
    for (const auto& w : tokens(s)) out += (out.empty() ? "" : " ") + w;
    return out;
}

std::string xml_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string xml_unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos) {
            o += s[i];
            continue;
        }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::stoul(ent.substr(2), nullptr, 16)
                                                                     : std::stoul(ent.substr(1));
            if (cp < 0x80) o += char(cp);
            else if (cp < 0x800) o += char(0xC0 | (cp >> 6)), o += char(0x80 | (cp & 0x3F));
            else if (cp < 0x10000) o += char(0xE0 | (cp >> 12)), o += char(0x80 | ((cp >> 6) & 0x3F)), o += char(0x80 | (cp & 0x3F));
            else o += char(0xF0 | (cp >> 18)), o += char(0x80 | ((cp >> 12) & 0x3F)), o += char(0x80 | ((cp >> 6) & 0x3F)), o += char(0x80 | (cp & 0x3F));
        } else {
            o += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return o;
}

// Word-level diff (longest common subsequence): (op, a-range, b-range) with op '=', '-', '+'.
struct Op {
    char op;
    size_t a1, a2, b1, b2;
};
std::vector<Op> diff(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<Op> ops;
    if (n * m > 4000000) {  // pathological size: treat as one replacement
        if (n) ops.push_back({'-', 0, n, 0, 0});
        if (m) ops.push_back({'+', n, n, 0, m});
        return ops;
    }
    std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;) L[i][j] = a[i] == b[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    size_t i = 0, j = 0;
    auto push = [&](char op, size_t a1, size_t a2, size_t b1, size_t b2) {
        if (!ops.empty() && ops.back().op == op && ops.back().a2 == a1 && ops.back().b2 == b1)
            ops.back().a2 = a2, ops.back().b2 = b2;
        else
            ops.push_back({op, a1, a2, b1, b2});
    };
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) push('=', i, i + 1, j, j + 1), i++, j++;
        else if (j < m && (i == n || L[i][j + 1] >= L[i + 1][j])) push('+', i, i, j, j + 1), j++;
        else push('-', i, i + 1, j, j), i++;
    }
    // show replacements as "delete then insert"
    for (size_t k = 0; k + 1 < ops.size(); k++)
        if (ops[k].op == '+' && ops[k + 1].op == '-') std::swap(ops[k], ops[k + 1]);
    return ops;
}

std::string join(const std::vector<std::string>& v, size_t a, size_t b) {
    std::string s;
    for (size_t i = a; i < b; i++) s += (i > a ? " " : "") + v[i];
    return s;
}

bool zip_write(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files, std::string& err) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 16)) {
        err = "could not create the Word file";
        return false;
    }
    for (const auto& [name, data] : files)
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            err = "could not create the Word file";
            return false;
        }
    void* buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    const std::string bytes(static_cast<const char*>(buf), size);
    mz_zip_writer_end(&zip);
    if (!write_file_atomic(path, bytes)) {
        err = "could not save the Word file - is it open in Word? Close it and try again.";
        return false;
    }
    return true;
}

const char* kW = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
const char* kHead = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";

}  // namespace

bool export_docx(const json& p, const std::string& path, const DocxOptions& opt, std::string& err) {
    const bool it = opt.ui_lang == "it";
    std::map<int, std::string> names;
    for (const auto& s : p["speakers"]) names[s["id"].get<int>()] = s["name"].get<std::string>();
    const double duration = p.value("duration", 0.0);
    const bool hours = duration >= 3600;

    char date[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", &tm);
    int rev = 1;
    const std::string author = xml_escape(opt.author.empty() ? "Reviewer" : opt.author);
    auto run = [](const std::string& t, bool bold) {
        return std::string("<w:r>") + (bold ? "<w:rPr><w:b/></w:rPr>" : "") + "<w:t xml:space=\"preserve\">" +
               xml_escape(t) + "</w:t></w:r>";
    };
    auto ins = [&](const std::string& t, bool bold) {
        return "<w:ins w:id=\"" + std::to_string(rev++) + "\" w:author=\"" + author + "\" w:date=\"" + date + "\">" +
               run(t, bold) + "</w:ins>";
    };
    auto del = [&](const std::string& t, bool bold) {
        return "<w:del w:id=\"" + std::to_string(rev++) + "\" w:author=\"" + author + "\" w:date=\"" + date +
               "\"><w:r>" + (bold ? "<w:rPr><w:b/></w:rPr>" : "") + "<w:delText xml:space=\"preserve\">" +
               xml_escape(t) + "</w:delText></w:r></w:del>";
    };
    auto para = [](const std::string& inner, const char* style = nullptr) {
        return std::string("<w:p>") + (style ? std::string("<w:pPr><w:pStyle w:val=\"") + style + "\"/></w:pPr>" : "") +
               inner + "</w:p>";
    };

    std::string body = para(run(p.value("title", std::string("Transcript")), false), "Title");
    std::string meta = (it ? "Durata " : "Duration ") + format_time(duration, hours) + "  ·  " +
                       std::to_string(p["speakers"].size()) + (it ? " interlocutori" : " speakers");
    body += para(run(meta, false), "Subtitle");
    body += para(run(it ? "Le correzioni fatte dopo la trascrizione automatica sono indicate come revisioni (Revisioni > Rileva modifiche)."
                        : "Corrections made after the automatic transcript are shown as tracked changes.",
                     false),
                 "Note");
    int bm = 1;
    for (const auto& t : p["turns"]) {
        const int id = t["id"].get<int>();
        const int spk = t["spk"].get<int>(), spk0 = t.value("orig_spk", spk);
        std::string inner = "<w:bookmarkStart w:id=\"" + std::to_string(bm) + "\" w:name=\"ts_turn_" + std::to_string(id) +
                            "\"/>";
        inner += run(format_time(t["s"].get<double>(), hours) + "  ", true);
        if (spk != spk0) inner += del(names[spk0], true) + ins(names[spk], true);
        else inner += run(names[spk], true);
        inner += run(": ", true);
        const auto a = tokens(t.value("orig", std::string())), b = tokens(t.value("text", std::string()));
        bool first = true;
        for (const auto& op : diff(a, b)) {
            const std::string sep = first ? "" : " ";
            first = false;
            if (op.op == '=') inner += run(sep + join(a, op.a1, op.a2), false);
            else if (op.op == '-') inner += del(sep + join(a, op.a1, op.a2), false);
            else inner += ins(sep + join(b, op.b1, op.b2), false);
        }
        inner += "<w:bookmarkEnd w:id=\"" + std::to_string(bm++) + "\"/>";
        body += para(inner);
    }

    const std::string rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
    const std::string ct = "application/vnd.openxmlformats-officedocument.wordprocessingml.";
    std::vector<std::pair<std::string, std::string>> files = {
        {"[Content_Types].xml",
         std::string(kHead) + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
         "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
         "<Override PartName=\"/word/document.xml\" ContentType=\"" + ct + "document.main+xml\"/>"
         "<Override PartName=\"/word/styles.xml\" ContentType=\"" + ct + "styles+xml\"/>"
         "<Override PartName=\"/word/settings.xml\" ContentType=\"" + ct + "settings+xml\"/></Types>"},
        {"_rels/.rels",
         std::string(kHead) + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"" + rel + "officeDocument\" Target=\"word/document.xml\"/></Relationships>"},
        {"word/_rels/document.xml.rels",
         std::string(kHead) + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"" + rel + "styles\" Target=\"styles.xml\"/>"
         "<Relationship Id=\"rId2\" Type=\"" + rel + "settings\" Target=\"settings.xml\"/></Relationships>"},
        {"word/document.xml",
         std::string(kHead) + "<w:document " + kW + "><w:body>" + body +
         "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" "
         "w:left=\"1440\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>"},
        {"word/styles.xml",
         std::string(kHead) + "<w:styles " + kW + "><w:docDefaults><w:rPrDefault><w:rPr>"
         "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/>"
         "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"" + std::string(it ? "it-IT" : "en-GB") + "\"/></w:rPr></w:rPrDefault>"
         "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
         "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
         "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/><w:basedOn w:val=\"Normal\"/>"
         "<w:pPr><w:spacing w:after=\"60\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"36\"/></w:rPr></w:style>"
         "<w:style w:type=\"paragraph\" w:styleId=\"Subtitle\"><w:name w:val=\"Subtitle\"/><w:basedOn w:val=\"Normal\"/>"
         "<w:rPr><w:color w:val=\"6B7280\"/></w:rPr></w:style>"
         "<w:style w:type=\"paragraph\" w:styleId=\"Note\"><w:name w:val=\"Note\"/><w:basedOn w:val=\"Normal\"/>"
         "<w:pPr><w:spacing w:after=\"280\"/></w:pPr><w:rPr><w:i/><w:color w:val=\"6B7280\"/><w:sz w:val=\"20\"/></w:rPr></w:style>"
         "</w:styles>"},
        {"word/settings.xml", std::string(kHead) + "<w:settings " + kW + "><w:trackRevisions/></w:settings>"},
    };
    return zip_write(path, files, err);
}

bool import_docx(const std::string& path, json& p, ImportReport& rep, std::string& err) {
    rep = {};
    std::string bytes;
    if (!read_file(path, bytes)) {
        err = "could not read " + path;
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        err = "this is not a Word (.docx) file";
        return false;
    }
    size_t n = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, "word/document.xml", &n, 0);
    mz_zip_reader_end(&zip);
    if (!data) {
        err = "this Word file has no document";
        return false;
    }
    const std::string xml(static_cast<const char*>(data), n);
    mz_free(data);

    // paragraphs as accepted text (deleted text skipped) + their ts_turn bookmark
    struct Para {
        int turn = -1;
        std::string text;
    };
    std::vector<Para> paras;
    Para cur;
    int in_del = 0;
    bool in_t = false;
    size_t i = 0;
    for (size_t lt; (lt = xml.find('<', i)) != std::string::npos;) {
        if (in_t && !in_del) cur.text += xml_unescape(xml.substr(i, lt - i));
        const size_t gt = xml.find('>', lt);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(lt + 1, gt - lt - 1);
        const bool closing = !tag.empty() && tag[0] == '/';
        const bool self = !tag.empty() && tag.back() == '/';
        const std::string name = tag.substr(closing ? 1 : 0, tag.find_first_of(" />", closing ? 1 : 0) - (closing ? 1 : 0));
        if (name == "w:p") {
            if (!closing) cur = Para{};
            if (closing || self) paras.push_back(cur);
        } else if (name == "w:del" || name == "w:moveFrom") {
            if (closing) in_del = std::max(0, in_del - 1);
            else if (!self) in_del++;
        } else if (name == "w:t") {
            in_t = !closing && !self;
        } else if (name == "w:tab" || name == "w:br") {
            if (!in_del) cur.text += ' ';
        } else if (name == "w:bookmarkStart") {
            static const std::regex bmrx("w:name=\"ts_turn_(\\d+)\"");
            std::smatch m;
            if (std::regex_search(tag, m, bmrx)) cur.turn = std::stoi(m[1]);
        }
        i = gt + 1;
    }

    static const std::regex line(R"(^\s*(\d{1,2}(?::\d{2}){1,2})\s+(.+?):\s*(.*)$)");
    std::map<std::string, int> by_name;
    int max_spk = 0;
    for (const auto& s : p["speakers"]) {
        std::string k = s["name"].get<std::string>();
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        by_name[k] = s["id"].get<int>();
        max_spk = std::max(max_spk, s["id"].get<int>());
    }
    auto speaker_id = [&](const std::string& name) {
        std::string k = squash(name);
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        const auto f = by_name.find(k);
        if (f != by_name.end()) return f->second;
        const int id = ++max_spk;
        p["speakers"].push_back({{"id", id}, {"name", squash(name)}, {"color", speaker_color(id - 1)}});
        by_name[k] = id;
        rep.speakers_added++;
        return id;
    };
    auto seconds = [](const std::string& ts) {
        double v = 0;
        std::istringstream in(ts);
        std::string part;
        while (std::getline(in, part, ':')) v = v * 60 + std::stod(part);
        return v;
    };

    std::map<int, size_t> index;
    for (size_t k = 0; k < p["turns"].size(); k++) index[p["turns"][k]["id"].get<int>()] = k;
    int next_id = 0;
    for (const auto& [id, _] : index) next_id = std::max(next_id, id);
    std::vector<bool> seen(p["turns"].size(), false);
    json added = json::array();
    double last_time = 0;
    for (const auto& para : paras) {
        std::smatch m;
        if (!std::regex_match(para.text, m, line)) continue;
        const double t = seconds(m[1]);
        const int spk = speaker_id(m[2]);
        const std::string text = squash(m[3]);
        const auto f = para.turn >= 0 ? index.find(para.turn) : index.end();
        if (f != index.end() && !seen[f->second]) {
            seen[f->second] = true;
            json& turn = p["turns"][f->second];
            const bool changed = turn["text"] != text || turn["spk"].get<int>() != spk;
            if (changed) {
                turn["text"] = text;
                turn["spk"] = spk;
                turn["edited"] = true;
                rep.updated++;
            }
            if (std::abs(t - turn["s"].get<double>()) >= 1.0) turn["s"] = t;
            last_time = turn["s"].get<double>();
        } else {
            added.push_back({{"id", ++next_id}, {"spk", spk}, {"s", std::max(t, last_time)}, {"e", std::max(t, last_time)},
                             {"text", text}, {"orig", ""}, {"orig_spk", spk}, {"words", json::array()},
                             {"status", "ok"}, {"edited", true}});
            rep.added++;
        }
    }
    if (rep.updated == 0 && rep.added == 0 && std::none_of(seen.begin(), seen.end(), [](bool b) { return b; })) {
        err = "no transcript lines were found in this Word file";
        return false;
    }
    json kept = json::array();
    for (size_t k = 0; k < seen.size(); k++) {
        if (seen[k]) kept.push_back(p["turns"][k]);
        else rep.removed++;
    }
    for (auto& a : added) kept.push_back(a);
    std::stable_sort(kept.begin(), kept.end(), [](const json& a, const json& b) { return a["s"].get<double>() < b["s"].get<double>(); });
    p["turns"] = kept;
    return true;
}

bool export_text(const json& p, const std::string& path, std::string& err) {
    std::map<int, std::string> names;
    for (const auto& s : p["speakers"]) names[s["id"].get<int>()] = s["name"].get<std::string>();
    const bool hours = p.value("duration", 0.0) >= 3600;
    std::string out;
    for (const auto& t : p["turns"])
        out += format_time(t["s"].get<double>(), hours) + "  " + names[t["spk"].get<int>()] + ": " +
               t["text"].get<std::string>() + "\n";
    if (!write_file_atomic(path, out)) {
        err = "could not save " + path;
        return false;
    }
    return true;
}

}  // namespace ts
