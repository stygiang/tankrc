#ifndef TANKRC_BUILD_SLAVE
#define TANKRC_BUILD_SLAVE 1
#endif

#ifndef TANKRC_ENABLE_NETWORK
#define TANKRC_ENABLE_NETWORK 0
#endif

#ifndef TANKRC_ENABLE_BLUETOOTH
#define TANKRC_ENABLE_BLUETOOTH 0
#endif

#ifndef TANKRC_USE_DRIVE_PROXY
#define TANKRC_USE_DRIVE_PROXY 0
#endif

#include "TankRC.h"
#include "comms/slave_endpoint.h"

using namespace TankRC;

static Control::DriveController driveController;
static Config::RuntimeConfig runtimeConfig = Config::makeDefaultConfig();
static Comms::SlaveEndpoint slaveEndpoint;

void setup() {
    Serial.begin(115200);
    Serial.println(F("[BOOT] TankRC slave starting..."));

    Core::setupHardware();

    slaveEndpoint.begin(&runtimeConfig, &driveController);
    Serial.println(F("[BOOT] Slave ready. Waiting for master commands."));
}

void loop() {
    slaveEndpoint.loop();
    Core::serviceWatchdog();
    delay(1);
}
