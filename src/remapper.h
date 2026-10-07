#pragma once

#include "mapping_table.h"

#include <array>

namespace symbols {

struct KeyEvent {
    unsigned key;
    bool down;
    bool injected;
};

class Remapper {
public:
    // The referenced table must outlive this remapper and remain unchanged
    // while it processes events. Temporary tables would leave a dangling view.
    explicit Remapper(const MappingTable& mappings = builtinMappingTable())
        : mappings_(&mappings) {}
    Remapper(MappingTable&&) = delete;
    Remapper(const MappingTable&&) = delete;

    // Existing holds retain their table until release. All selected tables
    // must remain alive and immutable for the lifetime of those holds.
    void selectTable(const MappingTable& mappings) { mappings_ = &mappings; }
    void selectTable(MappingTable&&) = delete;
    void selectTable(const MappingTable&&) = delete;

    void setModifier(unsigned key, bool down) {
        if (isModifier(key)) modifiers_[key] = down;
    }

    // The sender returns true if a character key-down was inserted. Returning
    // false passes the physical event through, so failed injection cannot eat it.
    template <class Sender>
    bool process(const KeyEvent& event, bool enabled, Sender&& send) {
        if (event.key >= states_.size()) return false;
        if (isModifier(event.key)) {
            setModifier(event.key, event.down);
            return false;
        }
        // Ignore synthetic symbols, including Unicode packets from ourselves.
        // Synthetic modifiers above still affect shortcut handling.
        if (event.injected) return false;

        State& state = states_[event.key];
        if (!event.down) {
            const bool suppress = state == State::Replaced;
            state = State::Idle;
            heldTables_[event.key] = nullptr;
            return suppress;
        }

        // A key-down already delivered to the application must retain its
        // matching key-up, even if Shift is pressed halfway through a hold.
        if (state == State::Passed) return false;

        if (state == State::Idle) heldTables_[event.key] = mappings_;
        const wchar_t replacement = enabled ? target(event.key, *heldTables_[event.key]) : 0;
        if (replacement != 0) {
            if (send(replacement)) {
                state = State::Replaced;
                return true;
            }
            state = State::Passed;
            return false;
        }

        // If a held key stops matching (pause or a modifier change), do not
        // leak unmatched repeats or key-up events to the application.
        if (state == State::Replaced) return true;
        state = State::Passed;
        return false;
    }

private:
    enum class State { Idle, Passed, Replaced };
    const MappingTable* mappings_;
    std::array<const MappingTable*, KeyCount> heldTables_{};
    std::array<State, KeyCount> states_{};
    std::array<bool, KeyCount> modifiers_{};

    static bool isModifier(unsigned key) {
        return (key >= LeftShift && key <= RightAlt) ||
               key == LeftWindows || key == RightWindows;
    }

    wchar_t target(unsigned key, const MappingTable& table) const {
        if (modifiers_[LeftControl] || modifiers_[RightControl] ||
            modifiers_[LeftAlt] || modifiers_[RightAlt] ||
            modifiers_[LeftWindows] || modifiers_[RightWindows]) return 0;

        const bool shift = modifiers_[LeftShift] || modifiers_[RightShift];
        return shift ? table.shifted[key] : table.plain[key];
    }
};

} // namespace symbols
