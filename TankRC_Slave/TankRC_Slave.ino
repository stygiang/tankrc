#ifndef TANKRC_BUILD_SLAVE
#define TANKRC_BUILD_SLAVE 1
#endif

#ifndef TANKRC_ENABLE_NETWORK
#define TANKRC_ENABLE_NETWORK 0
#endif

#ifndef TANKRC_USE_DRIVE_PROXY
#define TANKRC_USE_DRIVE_PROXY 0
#endif

#include "TankRC.h"
#include "comms/slave_endpoint.h"
#include "hal/hal.h"

using namespace TankRC;

static Control::DriveController driveController;
static Config::RuntimeConfig runtimeConfig = Config::makeDefaultConfig();
static Comms::SlaveEndpoint slaveEndpoint;

struct Task {
    void (*fn)();
    std::uint32_t intervalMs;
    std::uint32_t lastRunMs;
};

void taskServiceLink();
void taskControlLoop();

Task tasks[] = {
    {taskServiceLink, 5, 0},
    {taskControlLoop, 5, 0},
};

void logEvent(const Events::Event& event) {
    Serial.print(F("[EVT] "));
    switch (event.type) {
        case Events::EventType::LowBattery:
            Serial.printf("Battery low: %.2f V\n", event.f1);
            break;
        case Events::EventType::BatteryRecovered:
            Serial.printf("Battery recovered: %.2f V\n", event.f1);
            break;
        default:
            Serial.println(F("Event received"));
            break;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("[BOOT] TankRC slave starting..."));

    Core::setupHardware();

    Hal::begin(runtimeConfig);
    Events::subscribe(logEvent);
    slaveEndpoint.begin(&runtimeConfig, &driveController);
    Serial.println(F("[BOOT] Slave ready. Waiting for master commands."));
}

void taskServiceLink() {
    slaveEndpoint.loop();
}

void taskControlLoop() {
    Events::process();
}

void loop() {
    const std::uint32_t now = Hal::millis32();
    for (auto& task : tasks) {
        if (now - task.lastRunMs >= task.intervalMs) {
            task.lastRunMs = now;
            task.fn();
        }
    }
    Core::serviceWatchdog();
    Hal::delayMs(1);
}
