#include <string>

#include "SlotState.h"
#include "TestFramework.h"

TEST(slotstate_toString) {
    CHECK_EQ(toString(SlotState::FREE), "FREE");
    CHECK_EQ(toString(SlotState::OCCUPIED), "OCCUPIED");
    CHECK_EQ(toString(SlotState::UNKNOWN), "UNKNOWN");
}

TEST(slotstate_parseValidIgnoresCase) {
    SlotState s = SlotState::UNKNOWN;
    CHECK(parseState("FREE", s));      CHECK(s == SlotState::FREE);
    CHECK(parseState("occupied", s));  CHECK(s == SlotState::OCCUPIED);
    CHECK(parseState("Unknown", s));   CHECK(s == SlotState::UNKNOWN);
    CHECK(parseState("fReE", s));      CHECK(s == SlotState::FREE);
}

TEST(slotstate_parseInvalidLeavesOutputUnchanged) {
    SlotState s = SlotState::OCCUPIED;
    CHECK(!parseState("", s));
    CHECK(!parseState("BANANA", s));
    CHECK(!parseState(" FREE", s));    // spaces are not trimmed
    CHECK(!parseState("FREE ", s));
    CHECK(!parseState("FRE", s));
    CHECK(s == SlotState::OCCUPIED);   // never modified on failure
}
