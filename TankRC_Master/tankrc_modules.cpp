// Arduino IDE/arduino-cli only compile sources at the sketch root.
// Pull in every module implementation here so they build as one TU.
// PlatformIO sees the actual .cpp files, so we disable this include fan-out there.
#ifndef PLATFORMIO
#ifndef TANKRC_BUILD_MASTER
#ifndef TANKRC_BUILD_SLAVE
#define TANKRC_BUILD_MASTER 1
#endif
#endif
#if defined(TANKRC_BUILD_MASTER) && defined(TANKRC_BUILD_SLAVE)
#error "Define only one of TANKRC_BUILD_MASTER or TANKRC_BUILD_SLAVE"
#endif
#if TANKRC_BUILD_MASTER
#include "core/system_init.cpp"
#include "comms/bluetooth_console.cpp"
#include "comms/radio_link.cpp"
#include "comms/slave_link.cpp"
#include "config/runtime_config.cpp"
#include "control/drive_controller.cpp"
#include "drivers/pca9685.cpp"
#include "drivers/rc_receiver.cpp"
#include "features/lighting.cpp"
#include "features/sound_fx.cpp"
#include "logging/session_logger.cpp"
#include "network/control_server.cpp"
#include "network/remote_console.cpp"
#include "network/wifi_manager.cpp"
#include "storage/config_store.cpp"
#include "time/ntp_clock.cpp"
#include "ui/console.cpp"
#elif TANKRC_BUILD_SLAVE
#include "core/system_init.cpp"
#include "comms/slave_endpoint.cpp"
#include "config/runtime_config.cpp"
#include "control/drive_controller.cpp"
#include "control/pid.cpp"
#include "drivers/battery_monitor.cpp"
#include "drivers/motor_driver.cpp"
#else
#error "Define TANKRC_BUILD_MASTER or TANKRC_BUILD_SLAVE"
#endif
#endif
