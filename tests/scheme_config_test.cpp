#include "scheme_config.h"
#include "scheme_manager.h"
#include "remapper.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
const std::string header = "[app]\nversion=1\n";
const std::string valid =
    "[app]\nversion=1\nstartup=chinese\nnext_scheme_hotkey=Alt+Shift+F11\n"
    "[scheme.coding]\nname=Code\nShift+LeftBracket=U+007B\nQuote=pass\nbase=builtin\n"
    "[scheme.chinese]\nname=\xE4\xB8\xAD\xE6\x96\x87\nbase=empty\nComma=U+FF0C\n";
}

int main(int argc, char** argv) {
    using namespace symbols;
    for (const std::string& text : {std::string(), std::string(" ; comment\r\n# comment\n"), header}) {
        const auto config = parseConfig(text);
        check(config.issues.empty(), "empty/comment/default config is valid");
        check(config.schemes.size() == 1 && config.schemes[0].id == "builtin", "builtin always present");
        check(config.schemes[0].table.plain[Quote] == L'\'', "builtin retains original table");
    }
    {
        const auto config = parseConfig(valid);
        check(config.issues.empty(), "valid config has no errors");
        check(config.schemes.size() == 3, "all schemes are loaded in one parse");
        check(config.schemes[1].id == "coding" && config.schemes[2].id == "chinese", "file order preserved");
        check(config.schemes[2].name == "\xE4\xB8\xAD\xE6\x96\x87", "UTF-8 display name retained");
        check(config.schemes[1].table.shifted[LeftBracket] == L'{', "inheritance overrides applied");
        check(config.schemes[1].table.plain[Quote] == 0, "pass clears inherited mapping");
        check(config.schemes[1].table.plain[Semicolon] == L';', "base metadata can follow overrides");
        check(config.schemes[2].table.plain[Comma] == L'\uFF0C', "BMP character decoded");
        check(config.schemes[2].table.plain[Period] == 0, "empty base does not inherit");
        check(hotkeyText(config.nextHotkey) == "Alt+Shift+F11", "custom hotkey decoded");
    }
    {
        const auto config = parseConfig("\xEF\xBB\xBF[app]\r\n version = 1 \r\n[scheme.a]\r\n"
                                       "name= A&B \r\nbase=empty\r\nShift+Key2 = U+0040\r\n");
        check(config.issues.empty() && config.schemes.size() == 2, "BOM/CRLF and outer whitespace supported");
        check(config.schemes[1].table.shifted[0x32] == L'@', "additional digit keys supported");
    }
    for (const std::string line : {
        "Typo=U+0021", "Ctrl+Comma=U+0021", "Shift+Shift+Comma=U+0021", "Comma=abc",
        "Comma=U+0000", "Comma=U+001F", "Comma=U+007F", "Comma=U+0085", "Comma=U+D800",
        "Comma=U+DFFF", "Comma=U+FFFF", "Comma=U+FDD0", "Comma=U+2028", "Comma=U+1F600",
        "Comma=U+002G", "Comma=U+0021 ; comment", "base=other", "name=", "missing-equals",
        "Comma=U+0021\nComma=U+003F", "base=empty\nbase=builtin"}) {
        const auto config = parseConfig(header + "[scheme.bad]\n" + line +
            "\n[scheme.good]\nbase=empty\nPeriod=U+3002\n");
        check(!config.issues.empty(), "invalid scheme diagnosed");
        check(config.schemes.size() == 2 && config.schemes[1].id == "good", "bad scheme isolated without partial load");
        check(config.issues[0].line >= 4, "scheme error includes source line");
    }
    for (const std::string value : {"U+0020", "U+005C", "U+0022", "U+ff0c", "U+E000", "U+FFFD", "pass"}) {
        check(parseConfig(header + "[scheme.ok]\nComma=" + value).issues.empty(), "valid output form accepted");
    }
    for (const std::string id : {"builtin", "", "a.b", "a b"}) {
        const auto config = parseConfig(header + "[scheme." + id + "]\nComma=U+0021");
        check(config.schemes.size() == 1 && !config.issues.empty(), "reserved/invalid IDs cannot override builtin");
    }
    {
        const auto config = parseConfig(header + "[scheme.same]\nComma=U+0021\n"
            "[scheme.same]\nComma=U+003F\n[scheme.good]\nbase=empty\n");
        check(config.schemes.size() == 2 && config.schemes[1].id == "good", "all duplicate IDs are rejected");
    }
    for (const std::string text : {
        "[app]\nversion=2\n", "[scheme.a]\nbase=empty", "[app]\nversion=1\nversion=1",
        "[app]\nversion=1\nunknown=1", "[app]\nversion=1\n[app]", "outside=1",
        "[app]\nversion=1\n[unknown]", "[app]\nversion=1\n[scheme.a", "[app]\nno-equals"}) {
        const auto config = parseConfig(text);
        check(config.schemes.size() == 1 && !config.issues.empty(), "structural/global errors fall back to builtin");
    }
    for (const std::string& bytes : {std::string("\xC0\xAF"), std::string("\xED\xA0\x80"),
        std::string("\xF4\x90\x80\x80"), std::string("\xE4\xB8"), std::string("\0", 1)}) {
        const auto config = parseConfig(valid + bytes);
        check(config.schemes.size() == 1 && !config.issues.empty(), "invalid UTF-8/NUL rejects whole snapshot");
    }
    {
        auto config = parseConfig(header + "startup=missing\n[scheme.ok]\nbase=empty\n");
        check(config.startupId == "builtin" && config.schemes.size() == 2 && !config.issues.empty(),
              "invalid startup falls back without losing valid schemes");
        config = parseConfig(header + "startup=bad\n[scheme.bad]\nComma=bad\n");
        check(config.startupId == "builtin" && config.schemes.size() == 1, "rejected startup scheme falls back");
    }
    for (const std::string hotkey : {"Ctrl+Alt+F9", "Ctrl+F12", "F10", "Shift+F10", "Ctrl+Ctrl+F10",
        "Ctrl+F0", "Ctrl+F25", "Ctrl+F01", "Alt+", "Ctrl+Comma", "Ctrl+F10+Alt", "ctrl+F10"}) {
        const auto config = parseConfig(header + "next_scheme_hotkey=" + hotkey + "\n[scheme.ok]\nbase=empty");
        check(!config.issues.empty() && hotkeyText(config.nextHotkey) == "Ctrl+Alt+F10", "bad hotkey uses safe default");
        check(config.schemes.size() == 2, "bad hotkey preserves valid schemes");
    }
    for (const std::string hotkey : {"Ctrl+Alt+F10", "Ctrl+Shift+F9", "Alt+F24", "Win+F1", "Ctrl+9", "Ctrl+Alt+Z"}) {
        const auto config = parseConfig(header + "next_scheme_hotkey=" + hotkey);
        check(config.issues.empty() && hotkeyText(config.nextHotkey) == hotkey, "supported hotkeys round-trip");
    }
    {
        std::string text = header;
        for (unsigned i = 0; i < MaxCustomSchemes; ++i) text += "[scheme.s" + std::to_string(i) + "]\nbase=empty\n";
        check(parseConfig(text).schemes.size() == MaxCustomSchemes + 1, "scheme count limit accepted");
        check(parseConfig(text + "[scheme.too_many]\n").schemes.size() == 1, "scheme overflow rejects snapshot");
        check(!parseConfig(std::string(MaxConfigBytes + 1, ' ')).issues.empty(), "file size bound enforced");
        check(parseConfig(std::string(MaxConfigBytes, ' ')).issues.empty(), "file size boundary accepted");
        check(parseConfig(header + "[scheme.long]\nname=" + std::string(129, 'a')).schemes.size() == 1,
              "display name bound enforced");
    }
    {
        SchemeManager onlyBuiltin{Configuration{}};
        check(!onlyBuiltin.next() && onlyBuiltin.current().id == "builtin", "single scheme cycles as a no-op");
        auto config = parseConfig(valid);
        SchemeManager manager(config);
        const auto* savedTable = &manager.current().table;
        config.schemes.clear();
        check(manager.current().id == "chinese", "manager owns immutable startup snapshot");
        check(manager.next() && manager.current().id == "builtin", "cycle wraps to builtin");
        check(manager.next() && manager.current().id == "coding", "cycle follows file order");
        check(manager.select("chinese") && &manager.current().table == savedTable, "table address stable after selection");
        check(!manager.select("missing") && !manager.select(99), "invalid selection leaves current scheme unchanged");
        check(!manager.select("chinese"), "selecting current scheme is a no-op");

        Remapper mapper(manager.current().table);
        std::wstring output;
        auto send = [&](wchar_t ch) { output += ch; return true; };
        check(mapper.process({Comma, true, false}, true, send), "loaded startup table drives key processing");
        manager.next();
        mapper.selectTable(manager.current().table);
        mapper.process({Comma, true, false}, true, send);
        mapper.process({Comma, false, false}, true, send);
        mapper.process({Comma, true, false}, true, send);
        mapper.process({Comma, false, false}, true, send);
        check(output == L"\uFF0C\uFF0C,", "loaded tables are pinned during hold and switch on next press");
    }
    check(mappingKeyName(LeftBracket, true) == "Shift+LeftBracket", "mapping display uses config key names");
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        check(file.good(), "distributed example file opens");
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto config = parseConfig(bytes);
        check(config.issues.empty() && config.schemes.size() == 3 && config.startupId == "builtin",
              "distributed example loads all schemes with default behavior");
    }
    std::cout << "PASS: " << checks << " configuration/selection checks\n";
}
