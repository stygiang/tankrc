#include "core/system_init.h"
#include "hal/hal.h"

namespace TankRC::Core {
void setupHardware() {
    Hal::initializePlatform();
}

void serviceWatchdog() {
    Hal::toggleStatusLed();
}
}  // namespace TankRC::Core
