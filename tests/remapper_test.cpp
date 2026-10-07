#include "remapper.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
unsigned checks = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

struct Fixture {
    symbols::Remapper mapper;
    std::wstring output;
    bool enabled = true;
    bool canSend = true;

    Fixture() = default;
    explicit Fixture(const symbols::MappingTable& table) : mapper(table) {}

    bool key(unsigned code, bool down = true, bool injected = false) {
        return mapper.process({code, down, injected}, enabled, [this](wchar_t ch) {
            if (canSend) output += ch;
            return canSend;
        });
    }
};

struct Mapping {
    unsigned key;
    bool shift;
    wchar_t output;
};

const Mapping mappings[] = {
    {symbols::Key1, true, L'!'},
    {symbols::Comma, true, L'<'},
    {symbols::Period, true, L'>'},
    {symbols::Comma, false, L','},
    {symbols::Period, false, L'.'},
    {symbols::Key4, true, L'$'},
    {symbols::Key6, true, L'^'},
    {symbols::Key9, true, L'('},
    {symbols::Key0, true, L')'},
    {symbols::Minus, true, L'_'},
    {symbols::LeftBracket, false, L'['},
    {symbols::RightBracket, false, L']'},
    {symbols::Backslash, false, L'\\'},
    {symbols::Backslash, true, L'|'},
    {symbols::Semicolon, false, L';'},
    {symbols::Semicolon, true, L':'},
    {symbols::Quote, false, L'\''},
    {symbols::Quote, true, L'"'},
    {symbols::Slash, true, L'?'}
};
}

int main() {
    using namespace symbols;
    // Keep the expected rules independent of the production table, and cover
    // every slot so accidentally enabled mappings also fail the regression.
    for (unsigned key = 0; key < KeyCount; ++key) {
        for (bool shift : {false, true}) {
            wchar_t expected = 0;
            for (const auto& mapping : mappings) {
                if (mapping.key == key && mapping.shift == shift)
                    expected = mapping.output;
            }
            Fixture f;
            if (shift) f.key(LeftShift);
            check(f.key(key) == (expected != 0), "all default key-down slots match the original rules");
            check(f.key(key) == (expected != 0), "all default repeat slots match the original rules");
            check(f.key(key, false) == (expected != 0), "all default key-up slots remain paired");
            check(f.output == (expected ? std::wstring(2, expected) : std::wstring()),
                  "all default slots produce only the original characters");
        }
    }
    {
        const MappingTable empty;
        for (const auto& mapping : mappings) {
            Fixture f(empty);
            if (mapping.shift) f.key(LeftShift);
            check(!f.key(mapping.key), "empty table passes formerly mapped key-downs");
            check(!f.key(mapping.key, false), "empty table passes matching key-ups");
            check(f.output.empty(), "empty table emits no characters");
        }
    }
    {
        MappingTable custom = builtinMappingTable();
        custom.plain[Comma] = L'\uFF0C';
        custom.shifted[LeftBracket] = L'{';
        custom.plain[Quote] = 0;
        Fixture f(custom);
        check(f.key(Comma), "custom table overrides an existing mapping");
        check(f.key(Comma, false), "custom output retains its paired release");
        check(!f.key(Quote), "zero override disables a built-in mapping");
        check(!f.key(Quote, false), "disabled mapping passes its release");
        check(f.key(Semicolon), "custom table retains inherited mappings");
        check(f.key(Semicolon, false), "inherited mapping retains its paired release");
        f.key(LeftShift);
        check(f.key(LeftBracket), "custom table enables a previously unmapped combination");
        check(f.key(LeftBracket, false), "new mapping retains its paired release");
        check(f.output == L"\uFF0C;{", "remapper uses exact characters from the supplied table");

        Fixture builtin;
        builtin.key(Comma);
        builtin.key(Comma, false);
        builtin.key(Quote);
        builtin.key(Quote, false);
        builtin.key(LeftShift);
        check(!builtin.key(LeftBracket), "custom additions do not change the built-in table");
        check(!builtin.key(LeftBracket, false), "built-in unmapped release still passes");
        check(builtin.output == L",'", "custom overrides leave built-in characters unchanged");
    }
    for (const auto& mapping : mappings) {
        for (unsigned shift : {LeftShift, RightShift}) {
            Fixture f;
            if (mapping.shift) f.key(shift);
            check(f.key(mapping.key), "mapped down is replaced");
            check(f.key(mapping.key), "mapped repeat is replaced");
            check(f.key(mapping.key, false), "mapped up is paired");
            check(f.output == std::wstring(2, mapping.output),
                  "one exact ASCII character per down/repeat, no automatic pairs");
        }
        for (unsigned modifier : {LeftControl, RightControl, LeftAlt,
                                 RightAlt, LeftWindows, RightWindows}) {
            Fixture f;
            if (mapping.shift) f.key(LeftShift);
            f.key(modifier);
            check(!f.key(mapping.key), "all mapped shortcuts pass through");
            check(!f.key(mapping.key, false), "all shortcut key-ups pass through");
            check(f.output.empty(), "shortcuts produce no replacement");
        }
        {
            Fixture f;
            if (mapping.shift) f.key(LeftShift);
            f.enabled = false;
            check(!f.key(mapping.key), "all mappings pass while paused");
            check(!f.key(mapping.key, false), "paused release passes through");
            check(f.output.empty(), "paused mappings produce no replacement");
        }
        {
            Fixture f;
            if (mapping.shift) f.key(LeftShift);
            f.canSend = false;
            check(!f.key(mapping.key), "all mappings pass original on failure");
            check(!f.key(mapping.key, false), "failed release passes through");
            check(f.output.empty(), "failed mappings produce no replacement");
        }
        {
            Fixture f;
            if (mapping.shift) f.key(LeftShift);
            check(!f.key(mapping.key, true, true), "all synthetic mappings ignored");
            check(!f.key(mapping.key, false, true), "all synthetic releases ignored");
            check(f.output.empty(), "synthetic mappings do not loop");
        }
    }
    {
        Fixture f;
        for (unsigned key : {Key4, Key6, Key9, Key0, Minus, Slash}) {
            check(!f.key(key), "unshifted digits, hyphen and slash stay unchanged");
            check(!f.key(key, false), "unmapped plain key-up passes");
        }
        f.key(LeftShift);
        for (unsigned key : {LeftBracket, RightBracket, 0x32u, 0x33u, 0x35u, 0x37u, 0x38u}) {
            check(!f.key(key), "unrequested shifted symbols stay unchanged");
            check(!f.key(key, false), "unrequested shifted key-up passes");
        }
        check(f.output.empty(), "unrequested characters produce no replacement");
    }
    for (unsigned key : {Backslash, Semicolon, Quote, Comma, Period}) {
        Fixture f;
        check(f.key(key), "unshifted punctuation down replaced");
        f.key(LeftShift);
        check(f.key(key), "adding Shift during punctuation repeat uses shifted mapping");
        f.key(LeftShift, false);
        check(f.key(key), "releasing Shift returns repeat to unshifted mapping");
        check(f.key(key, false), "punctuation repeat retains paired release");
        const wchar_t* expected = key == Backslash ? L"\\|\\" :
                                  key == Semicolon ? L";:;" :
                                  key == Quote ? L"'\"'" :
                                  key == Comma ? L",<," : L".>.";
        check(f.output == expected, "shift changes select the exact punctuation variant");
    }
    {
        Fixture f;
        check(f.key(LeftBracket), "plain bracket replaced");
        f.key(LeftShift);
        check(f.key(LeftBracket), "held bracket cannot leak an unmatched shifted repeat");
        check(f.key(LeftBracket, false), "held bracket release still consumed");
        check(!f.key(LeftBracket), "fresh shifted bracket remains untouched");
        check(!f.key(LeftBracket, false), "fresh shifted bracket release passes");
        check(f.output == L"[", "unmapped shifted bracket adds no replacement");
    }
    for (unsigned shift : {LeftShift, RightShift}) {
        Fixture f;
        check(!f.key(shift), "Shift passes through");
        for (unsigned key : {Key1, Comma, Period}) {
            check(f.key(key), "target down is replaced");
            check(f.key(key, false), "matching target up is suppressed");
        }
        check(f.output == L"!<>", "both Shift keys produce exact ASCII symbols");
    }
    {
        Fixture f;
        check(!f.key(Key1), "plain digit passes through");
        check(!f.key(Key1, false), "plain digit key-up passes through");
        f.key(LeftShift);
        check(!f.key('A'), "unrelated shifted keys pass through");
        check(f.output.empty(), "no output for unrelated keys");
    }
    for (unsigned modifier : {LeftControl, RightControl, LeftAlt,
                             RightAlt, LeftWindows, RightWindows}) {
        Fixture f;
        f.key(LeftShift);
        f.key(modifier);
        for (unsigned key : {Key1, Comma, Period}) {
            check(!f.key(key), "shortcut is preserved");
            check(!f.key(key, false), "shortcut key-up is preserved");
        }
        check(f.output.empty(), "shortcut emits no replacement");
    }
    {
        Fixture f;
        f.key(LeftShift);
        f.key(RightShift);
        f.key(LeftShift, false);
        check(f.key(Key1), "remaining right Shift stays active");
        f.key(Key1, false);
        f.key(RightShift, false);
        check(!f.key(Key1), "both Shift releases restore plain keys");
    }
    {
        Fixture f;
        f.key(LeftShift);
        check(f.key(Key1), "initial down replaced");
        check(f.key(Key1), "auto-repeat replaced");
        f.enabled = false;
        check(f.key(Key1), "pause consumes held repeat without output");
        check(f.key(Key1, false), "pause consumes matching release");
        check(!f.key(Comma), "new keys pass while paused");
        check(!f.key(Comma, false), "paused key-up passes");
        check(f.output == L"!!", "pause adds no characters");
    }
    {
        Fixture f;
        check(!f.key(Key1), "initial plain key passes");
        f.key(LeftShift);
        check(!f.key(Key1), "adding Shift cannot hijack an existing hold");
        check(!f.key(Key1, false), "previously forwarded down retains its up");
        check(f.key(Key1), "next independent press is replaced");
    }
    {
        Fixture f;
        f.key(LeftShift);
        f.key(Comma);
        f.key(LeftShift, false);
        check(f.key(Comma), "releasing Shift switches held comma to plain mapping");
        check(f.key(Comma, false), "replaced key still consumes release");
        check(f.output == L"<,", "repeat after Shift release emits an ASCII comma");
    }
    {
        Fixture f;
        f.key(LeftShift);
        f.canSend = false;
        check(!f.key(Key1), "failed injection passes original down");
        f.canSend = true;
        check(!f.key(Key1), "failed sequence remains passed until release");
        check(!f.key(Key1, false), "failed injection passes original up");
        check(f.key(Key1), "later press can recover from injection failure");
    }
    {
        Fixture f;
        f.key(LeftShift);
        f.key(Period);
        f.canSend = false;
        check(!f.key(Period), "failed repeat starts a forwarded down sequence");
        check(!f.key(Period, false), "failed repeat retains paired release");
    }
    {
        Fixture f;
        f.key(LeftShift);
        check(!f.key(Key1, true, true), "synthetic target is ignored");
        check(!f.key(0xE7, true, true), "Unicode packet is ignored");
        check(f.key(Key1), "physical key remains replaceable");
        check(!f.key(Key1, false, true), "synthetic key-up does not end physical hold");
        check(f.key(Key1, false), "physical key-up still suppressed");
        f.key(LeftControl, true, true);
        check(!f.key(Comma), "synthetic modifier protects a physical shortcut");
    }
    {
        Fixture f;
        f.mapper.setModifier(RightShift, true);
        check(f.key(Comma), "modifier held before startup is recognized");
        check(!f.key(999), "out-of-range key ignored");
        check(!f.key(Period, false), "unmatched key-up passes through");
    }
    {
        MappingTable custom = builtinMappingTable();
        custom.plain[Comma] = L'\uFF0C';
        custom.shifted[Comma] = L'\u300A';
        Fixture f;
        f.key(Comma);
        f.mapper.selectTable(custom);
        check(f.key(Comma), "held repeat remains replaced after table switch");
        f.key(LeftShift);
        f.key(Comma);
        check(f.key(Comma, false), "switched hold keeps its paired release");
        f.key(Comma);
        f.key(Comma, false);
        check(f.output == L",,<\u300A", "held key uses old table with live Shift; fresh key uses new table");
    }
    {
        const MappingTable empty;
        Fixture f;
        f.key(Comma);
        f.mapper.selectTable(empty);
        f.key(Comma, false, true);
        check(f.key(Comma), "synthetic release cannot unpin held mapping");
        check(f.key(Comma, false), "old mapped release suppressed after switch to empty");
        check(!f.key(Comma), "fresh press uses empty table");
        check(!f.key(Comma, false), "fresh empty-table release passes");
        f.mapper.selectTable(builtinMappingTable());
        check(f.key(Comma), "selection can return to builtin");
        check(f.output == L",,,", "switch to empty does not change existing hold");
    }
    {
        const MappingTable empty;
        Fixture f(empty);
        check(!f.key(Comma), "initial empty-table down passes");
        f.mapper.selectTable(builtinMappingTable());
        check(!f.key(Comma), "switch cannot hijack passed hold");
        check(!f.key(Comma, false), "passed hold keeps release after switch");
        check(f.key(Comma), "next independent press uses selected table");
    }
    {
        MappingTable custom = builtinMappingTable();
        custom.plain[Comma] = L'\uFF0C';
        Fixture f;
        f.key(Comma);
        f.enabled = false;
        f.mapper.selectTable(custom);
        check(f.key(Comma), "pause suppresses held repeat across selection");
        check(f.output == L",", "paused selection produces no output");
        check(f.key(Comma, false), "paused selection keeps old paired release");
        check(!f.key(Comma), "new paused key passes despite new table");
        f.enabled = true;
        check(!f.key(Comma), "resume cannot hijack a key pressed while paused");
        f.key(Comma, false);
        f.key(Comma);
        check(f.output == L",\uFF0C", "resume uses newly selected table for fresh keys");
    }
    {
        MappingTable custom = builtinMappingTable();
        custom.plain[Comma] = L'\uFF0C';
        Fixture f;
        f.key(Comma);
        f.key(LeftControl);
        f.key(LeftAlt);
        f.mapper.selectTable(custom);
        check(f.key(Comma), "switch hotkey modifiers suppress held output");
        check(f.output == L",", "modifier-protected repeat emits nothing");
        f.key(LeftControl, false);
        f.key(LeftAlt, false);
        f.key(Comma);
        f.canSend = false;
        check(!f.key(Comma), "failed old-table repeat passes physical down");
        f.canSend = true;
        check(!f.key(Comma), "failed hold cannot be reclaimed by selected table");
        check(!f.key(Comma, false), "failed hold retains passed release");
        f.key(Comma);
        check(f.output == L",,\uFF0C", "new press recovers using selected table");
    }
    std::cout << "PASS: " << checks << " key-sequence checks\n";
}
