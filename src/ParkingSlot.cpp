#include "ParkingSlot.h"

ParkingSlot::ParkingSlot(int id) : id_(id), state_(SlotState::UNKNOWN) {}

int ParkingSlot::getId() const { return id_; }

SlotState ParkingSlot::getState() const { return state_; }

void ParkingSlot::setState(SlotState state) { state_ = state; }
