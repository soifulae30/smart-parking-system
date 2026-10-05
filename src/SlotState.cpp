#include "SlotState.h"

#include <cctype>

std::string toString(SlotState state) {
    switch (state) {
        case SlotState::FREE:     return "FREE";
        case SlotState::OCCUPIED: return "OCCUPIED";
        case SlotState::UNKNOWN:  return "UNKNOWN";
    }
    return "UNKNOWN";  // unreachable, but keeps the compiler quiet
}

bool parseState(const std::string& text, SlotState& out) {
    std::string upper;
    for (char c : text) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper == "FREE")     { out = SlotState::FREE;     return true; }
    if (upper == "OCCUPIED") { out = SlotState::OCCUPIED; return true; }
    if (upper == "UNKNOWN")  { out = SlotState::UNKNOWN;  return true; }
    return false;
}
