#include "scheme_config.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace symbols {
namespace {
struct KeyName { const char* name; unsigned key; };
const KeyName keyNames[] = {
    {"Key0", 0x30}, {"Key1", 0x31}, {"Key2", 0x32}, {"Key3", 0x33},
    {"Key4", 0x34}, {"Key5", 0x35}, {"Key6", 0x36}, {"Key7", 0x37},
    {"Key8", 0x38}, {"Key9", 0x39}, {"Space", 0x20}, {"Minus", Minus},
    {"Equal", 0xBB}, {"Comma", Comma}, {"Period", Period},
    {"LeftBracket", LeftBracket}, {"RightBracket", RightBracket},
    {"Backslash", Backslash}, {"Semicolon", Semicolon}, {"Quote", Quote},
    {"Slash", Slash}, {"Backtick", 0xC0}
};

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}

bool identifier(const std::string& text) {
    if (text.empty() || text.size() > 48) return false;
    for (char ch : text) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    }
    return true;
}

// Reject malformed UTF-8, overlong sequences, surrogates, and embedded control
// characters. Newlines/tabs are allowed only in the file, not display names.
bool validUtf8(const std::string& text, bool name = false) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned lead = static_cast<unsigned char>(text[i++]);
        unsigned value = lead;
        unsigned count = 0;
        unsigned minimum = 0;
        if (lead >= 0xC2 && lead <= 0xDF) { count = 1; value = lead & 0x1F; minimum = 0x80; }
        else if (lead >= 0xE0 && lead <= 0xEF) { count = 2; value = lead & 0x0F; minimum = 0x800; }
        else if (lead >= 0xF0 && lead <= 0xF4) { count = 3; value = lead & 0x07; minimum = 0x10000; }
        else if (lead >= 0x80) return false;
        if (i + count > text.size()) return false;
        for (unsigned n = 0; n < count; ++n) {
            const unsigned next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) return false;
        if (value < 0x20 && (name || (value != '\n' && value != '\r' && value != '\t')))
            return false;
        if (value >= 0x7F && value <= 0x9F) return false;
    }
    return true;
}

bool parseKey(std::string text, unsigned& key, bool& shift) {
    shift = text.compare(0, 6, "Shift+") == 0;
    if (shift) text.erase(0, 6);
    for (const auto& entry : keyNames) {
        if (text == entry.name) { key = entry.key; return true; }
    }
    return false;
}

bool parseCharacter(const std::string& text, wchar_t& output) {
    if (text == "pass") { output = 0; return true; }
    if (text.size() != 6 || text.compare(0, 2, "U+") != 0) return false;
    unsigned value = 0;
    for (std::size_t i = 2; i < text.size(); ++i) {
        const char ch = text[i];
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else return false;
        value = value * 16 + digit;
    }
    if (value < 0x20 || (value >= 0x7F && value <= 0x9F) ||
        (value >= 0xD800 && value <= 0xDFFF) ||
        (value >= 0xFDD0 && value <= 0xFDEF) || value >= 0xFFFE ||
        value == 0x2028 || value == 0x2029) return false;
    output = static_cast<wchar_t>(value);
    return true;
}

bool parseHotkey(const std::string& text, Hotkey& output) {
    Hotkey result;
    result.control = result.alt = false;
    std::size_t begin = 0;
    while (true) {
        const auto end = text.find('+', begin);
        const std::string token = text.substr(begin, end - begin);
        if (end == std::string::npos) {
            if (token.size() == 1 && ((token[0] >= 'A' && token[0] <= 'Z') ||
                                      (token[0] >= '0' && token[0] <= '9'))) {
                result.key = static_cast<unsigned>(token[0]);
            } else {
                unsigned f = 0;
                if (token.size() < 2 || token.size() > 3 || token[0] != 'F' || token[1] == '0')
                    return false;
                for (std::size_t i = 1; i < token.size(); ++i) {
                    if (token[i] < '0' || token[i] > '9') return false;
                    f = f * 10 + token[i] - '0';
                }
                // F12 is reserved by Windows for the debugger.
                if (f < 1 || f > 24 || f == 12) return false;
                result.key = 0x70 + f - 1;
            }
            break;
        }
        bool* flag = nullptr;
        if (token == "Ctrl") flag = &result.control;
        else if (token == "Alt") flag = &result.alt;
        else if (token == "Shift") flag = &result.shift;
        else if (token == "Win") flag = &result.windows;
        if (!flag || *flag) return false;
        *flag = true;
        begin = end + 1;
    }
    // Avoid interception by the symbol remapper and the existing pause hotkey.
    if (!(result.control || result.alt || result.windows)) return false;
    if (result.control && result.alt && !result.shift && !result.windows && result.key == 0x78)
        return false;
    output = result;
    return true;
}

struct Entry { std::string key; std::string value; std::size_t line; };
struct Section {
    std::string id;
    std::size_t line;
    std::vector<Entry> entries;
    bool valid = true;
};
} // namespace

Configuration::Configuration() {
    schemes.push_back({"builtin", u8"内置默认", builtinMappingTable()});
}

std::string hotkeyText(const Hotkey& hotkey) {
    std::string text;
    if (hotkey.control) text += "Ctrl+";
    if (hotkey.alt) text += "Alt+";
    if (hotkey.shift) text += "Shift+";
    if (hotkey.windows) text += "Win+";
    if (hotkey.key >= 0x70 && hotkey.key <= 0x87) text += "F" + std::to_string(hotkey.key - 0x70 + 1);
    else text += static_cast<char>(hotkey.key);
    return text;
}

std::string mappingKeyName(unsigned key, bool shift) {
    for (const auto& entry : keyNames) {
        if (entry.key == key) return (shift ? "Shift+" : std::string()) + entry.name;
    }
    return "VK_" + std::to_string(key);
}

Configuration parseConfig(const std::string& bytes) {
    Configuration result;
    auto report = [&](std::size_t line, const std::string& section, const std::string& message) {
        result.issues.push_back({line, section, message});
    };
    auto fatal = [&](std::size_t line, const std::string& message) {
        Configuration fallback;
        fallback.issues = std::move(result.issues);
        fallback.issues.push_back({line, "file", message});
        return fallback;
    };
    if (bytes.size() > MaxConfigBytes) return fatal(0, "File exceeds 64 KiB.");
    if (!validUtf8(bytes)) return fatal(0, "Expected valid UTF-8 without control characters.");
    std::string content = bytes;
    if (content.compare(0, 3, "\xEF\xBB\xBF") == 0) content.erase(0, 3);
    std::istringstream stream(content);
    std::vector<Section> sections;
    std::vector<Entry> app;
    bool inApp = false;
    bool seenApp = false;
    bool hasContent = false;
    std::size_t lineNumber = 0;
    std::string line;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        hasContent = true;
        if (line[0] == '[') {
            if (line.back() != ']') return fatal(lineNumber, "Malformed section header.");
            const std::string header = line.substr(1, line.size() - 2);
            if (header == "app") {
                if (seenApp) return fatal(lineNumber, "Duplicate [app] section.");
                inApp = seenApp = true;
            } else if (header.compare(0, 7, "scheme.") == 0) {
                inApp = false;
                if (sections.size() >= MaxCustomSchemes) return fatal(lineNumber, "Too many scheme sections (maximum 32).");
                Section section{header.substr(7), lineNumber, {}, true};
                if (!identifier(section.id) || section.id == "builtin") {
                    report(lineNumber, header, "Invalid or reserved scheme ID.");
                    section.valid = false;
                }
                sections.push_back(std::move(section));
            } else return fatal(lineNumber, "Unknown section: " + header);
            continue;
        }
        const auto equal = line.find('=');
        if (!inApp && sections.empty()) return fatal(lineNumber, "Entry outside a section.");
        if (equal == std::string::npos || trim(line.substr(0, equal)).empty()) {
            if (inApp) return fatal(lineNumber, "Expected key=value in [app].");
            report(lineNumber, sections.back().id, "Expected key=value; scheme skipped.");
            sections.back().valid = false;
            continue;
        }
        Entry entry{trim(line.substr(0, equal)), trim(line.substr(equal + 1)), lineNumber};
        if (inApp) app.push_back(std::move(entry));
        else sections.back().entries.push_back(std::move(entry));
    }
    if (!hasContent) return result;
    std::set<std::string> appKeys;
    std::size_t startupLine = 0;
    bool version = false;
    for (const auto& entry : app) {
        if (!appKeys.insert(entry.key).second) return fatal(entry.line, "Duplicate application setting: " + entry.key);
        if (entry.key == "version") {
            if (entry.value != "1") return fatal(entry.line, "Unsupported config version; expected 1.");
            version = true;
        } else if (entry.key == "startup") {
            result.startupId = entry.value;
            startupLine = entry.line;
        } else if (entry.key == "next_scheme_hotkey") {
            if (!parseHotkey(entry.value, result.nextHotkey))
                report(entry.line, "app", "Invalid/conflicting hotkey; using Ctrl+Alt+F10.");
        } else return fatal(entry.line, "Unknown application setting: " + entry.key);
    }
    if (!version) return fatal(0, "Non-empty config requires [app] version=1.");

    std::map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto inserted = ids.emplace(sections[i].id, i);
        if (!inserted.second) {
            sections[i].valid = sections[inserted.first->second].valid = false;
            report(sections[i].line, sections[i].id, "Duplicate scheme ID; all definitions skipped.");
        }
    }
    for (auto& section : sections) {
        Scheme scheme{section.id, section.id, {}};
        std::string base = "builtin";
        std::set<std::string> keys;
        for (const auto& entry : section.entries) {
            if (!keys.insert(entry.key).second) {
                report(entry.line, section.id, "Duplicate entry: " + entry.key);
                section.valid = false;
            }
            if (entry.key == "name") {
                if (entry.value.empty() || entry.value.size() > 128 || !validUtf8(entry.value, true)) {
                    report(entry.line, section.id, "Name must contain 1-128 UTF-8 bytes without controls.");
                    section.valid = false;
                } else scheme.name = entry.value;
            } else if (entry.key == "base") {
                base = entry.value;
                if (base != "builtin" && base != "empty") {
                    report(entry.line, section.id, "Base must be builtin or empty.");
                    section.valid = false;
                }
            }
        }
        scheme.table = base == "builtin" ? builtinMappingTable() : MappingTable{};
        for (const auto& entry : section.entries) {
            if (entry.key == "name" || entry.key == "base") continue;
            unsigned key = 0;
            bool shift = false;
            wchar_t output = 0;
            if (!parseKey(entry.key, key, shift) || !parseCharacter(entry.value, output)) {
                report(entry.line, section.id, "Invalid mapping: " + entry.key + "=" + entry.value);
                section.valid = false;
            } else (shift ? scheme.table.shifted : scheme.table.plain)[key] = output;
        }
        if (section.valid) result.schemes.push_back(std::move(scheme));
    }
    const auto selected = std::find_if(result.schemes.begin(), result.schemes.end(),
        [&](const Scheme& scheme) { return scheme.id == result.startupId; });
    if (selected == result.schemes.end()) {
        report(startupLine, "app", "Startup scheme unavailable; using builtin.");
        result.startupId = "builtin";
    }
    return result;
}
} // namespace symbols
