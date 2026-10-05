#include <stdexcept>
#include <vector>

#include "ParkingSlotManager.h"
#include "SensorReader.h"
#include "SimulatedSensor.h"
#include "TestFramework.h"
#include "TestSupport.h"

TEST(sensor_manualControlsWithNoiseOff) {
    SimulatedSensor sim(4, 1);
    sim.setNoiseEnabled(false);
    CHECK(!sim.isNoiseEnabled());

    std::vector<SensorReading> r = sim.readAll();
    CHECK_EQ(static_cast<int>(r.size()), 4);
    for (std::size_t i = 0; i < r.size(); ++i) {
        CHECK_EQ(r[i].slotId, static_cast<int>(i) + 1);
        CHECK(r[i].state == SlotState::UNKNOWN);
    }

    CHECK(sim.vehicleArrives(2));
    CHECK(sim.readAll()[1].state == SlotState::OCCUPIED);
    CHECK(sim.vehicleLeaves(2));
    CHECK(sim.readAll()[1].state == SlotState::FREE);
    CHECK(sim.injectFault(2));
    CHECK(sim.readAll()[1].state == SlotState::UNKNOWN);

    sim.setNoiseEnabled(true);
    CHECK(sim.isNoiseEnabled());
}

TEST(sensor_invalidSlotIdsReturnFalse) {
    SimulatedSensor sim(4, 1);
    CHECK(!sim.vehicleArrives(0));
    CHECK(!sim.vehicleArrives(5));
    CHECK(!sim.vehicleLeaves(-1));
    CHECK(!sim.injectFault(1000));
}

TEST(sensor_zeroSlotsThrows) {
    bool thrown = false;
    try {
        SimulatedSensor sim(0, 1);
        (void)sim;
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    CHECK(thrown);
}

TEST(sensor_sameSeedGivesSameSequence) {
    SimulatedSensor a(6, 42);
    SimulatedSensor b(6, 42);
    bool identical = true;
    for (int poll = 0; poll < 30; ++poll) {
        const std::vector<SensorReading> ra = a.readAll();
        const std::vector<SensorReading> rb = b.readAll();
        for (std::size_t i = 0; i < ra.size(); ++i) {
            if (ra[i].slotId != rb[i].slotId || ra[i].state != rb[i].state) identical = false;
        }
    }
    CHECK(identical);
}

TEST(sensor_noiseProducesReadings) {
    SimulatedSensor sim(6, 7);  // noise is on by default
    bool sawFree = false, sawOccupied = false;
    for (int poll = 0; poll < 40; ++poll) {
        for (const SensorReading& r : sim.readAll()) {
            if (r.state == SlotState::FREE) sawFree = true;
            if (r.state == SlotState::OCCUPIED) sawOccupied = true;
        }
    }
    CHECK(sawFree);
    CHECK(sawOccupied);
}

TEST(sensor_readingsFlowThroughInterfaceIntoManager) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);

    SimulatedSensor sim(6, 99);
    sim.setNoiseEnabled(false);
    sim.vehicleLeaves(1);
    sim.vehicleLeaves(2);
    sim.vehicleArrives(3);  // slots 4..6 remain UNKNOWN

    SensorReader& reader = sim;  // the manager side only sees the interface
    for (const SensorReading& r : reader.readAll()) {
        m.updateSlot(r.slotId, r.state, "sensor");
    }

    const Snapshot s = m.getSnapshot();
    CHECK_EQ(s.counts.available, 2);
    CHECK_EQ(s.counts.occupied, 1);
    CHECK_EQ(s.counts.unknown, 3);   // UNKNOWN excluded from available
    CHECK_EQ(static_cast<int>(l.events().size()), 3);
}
