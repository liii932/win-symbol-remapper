#pragma once

#include <array>
#include <cstddef>

namespace symbols {

// Win32 virtual-key values; keeping the policy independent of Windows makes
// key sequences and failed input delivery testable without a global hook.
constexpr std::size_t KeyCount = 256;
constexpr unsigned Key1 = 0x31; // 1 !
constexpr unsigned Key4 = 0x34; // 4 $
constexpr unsigned Key6 = 0x36; // 6 ^
constexpr unsigned Key9 = 0x39; // 9 (
constexpr unsigned Key0 = 0x30; // 0 )
constexpr unsigned Minus = 0xBD; // - _
constexpr unsigned Comma = 0xBC; // , <
constexpr unsigned Period = 0xBE; // . >
constexpr unsigned LeftBracket = 0xDB; // [ {
constexpr unsigned RightBracket = 0xDD; // ] }
constexpr unsigned Backslash = 0xDC; // \ |
constexpr unsigned Semicolon = 0xBA; // ; :
constexpr unsigned Quote = 0xDE; // ' "
constexpr unsigned Slash = 0xBF; // / ?
constexpr unsigned LeftShift = 0xA0;
constexpr unsigned RightShift = 0xA1;
constexpr unsigned LeftControl = 0xA2;
constexpr unsigned RightControl = 0xA3;
constexpr unsigned LeftAlt = 0xA4;
constexpr unsigned RightAlt = 0xA5;
constexpr unsigned LeftWindows = 0x5B;
constexpr unsigned RightWindows = 0x5C;

// Zero means pass through. Modifier keys are always handled by the remapper,
// and Ctrl/Alt/Win combinations bypass these tables.
struct MappingTable {
    std::array<wchar_t, KeyCount> plain{};
    std::array<wchar_t, KeyCount> shifted{};
};

// Build once when first requested, before processing keyboard events. Callers
// can copy this table to prepare overrides without changing the built-in rules.
inline const MappingTable& builtinMappingTable() {
    static const MappingTable table = [] {
        MappingTable result;
        result.plain[Comma] = L',';
        result.plain[Period] = L'.';
        result.plain[LeftBracket] = L'[';
        result.plain[RightBracket] = L']';
        result.plain[Backslash] = L'\\';
        result.plain[Semicolon] = L';';
        result.plain[Quote] = L'\'';

        result.shifted[Key1] = L'!';
        result.shifted[Key4] = L'$';
        result.shifted[Key6] = L'^';
        result.shifted[Key9] = L'(';
        result.shifted[Key0] = L')';
        result.shifted[Minus] = L'_';
        result.shifted[Comma] = L'<';
        result.shifted[Period] = L'>';
        result.shifted[Backslash] = L'|';
        result.shifted[Semicolon] = L':';
        result.shifted[Quote] = L'"';
        result.shifted[Slash] = L'?';
        return result;
    }();
    return table;
}

} // namespace symbols
