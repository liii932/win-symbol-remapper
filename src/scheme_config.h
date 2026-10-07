#pragma once

#include "mapping_table.h"

#include <string>
#include <vector>

namespace symbols {

constexpr std::size_t MaxConfigBytes = 64 * 1024;
constexpr std::size_t MaxCustomSchemes = 32;

struct Hotkey {
    bool control = true;
    bool alt = true;
    bool shift = false;
    bool windows = false;
    unsigned key = 0x79; // F10
};

struct Scheme {
    std::string id;
    std::string name; // UTF-8
    MappingTable table;
};

struct ConfigIssue {
    std::size_t line;
    std::string section;
    std::string message;
};

struct Configuration {
    Configuration();
    std::vector<Scheme> schemes;
    std::string startupId = "builtin";
    Hotkey nextHotkey;
    std::vector<ConfigIssue> issues;
};

// Pure parser: all inheritance is expanded here, with no file I/O at runtime.
Configuration parseConfig(const std::string& bytes);
std::string hotkeyText(const Hotkey& hotkey);
std::string mappingKeyName(unsigned key, bool shift);

} // namespace symbols
