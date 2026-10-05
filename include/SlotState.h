#ifndef SLOT_STATE_H
#define SLOT_STATE_H

#include <string>

// The three possible states of a parking slot (FR-02).
enum class SlotState {
    FREE,      // sensor reports no vehicle
    OCCUPIED,  // sensor reports a vehicle
    UNKNOWN    // no valid reading yet, or sensor fault
};

// Converts a state to text: "FREE", "OCCUPIED" or "UNKNOWN".
std::string toString(SlotState state);

// Converts text to a state, ignoring upper/lower case.
// Returns true and fills 'out' on success; returns false and leaves
// 'out' unchanged for anything that is not a valid state name.
bool parseState(const std::string& text, SlotState& out);

#endif // SLOT_STATE_H
