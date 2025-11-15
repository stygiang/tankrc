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
#include "comms/radio_link.cpp"
#include "comms/slave_link.cpp"
#include "config/runtime_config.cpp"
#include "control/drive_controller.cpp"
#include "drivers/rc_receiver.cpp"
#include "features/sound_fx.cpp"
#include "hal/hal.cpp"
#include "health/health.cpp"
#include "../events/event_bus.cpp"
#if TANKRC_ENABLE_NETWORK
#include "logging/session_logger.cpp"
#include "network/control_server.cpp"
#include "network/remote_console.cpp"
#include "network/wifi_manager.cpp"
#include "time/ntp_clock.cpp"
#endif
#include "storage/config_store.cpp"
#include "ui/console.cpp"
#else
#error "Define TANKRC_BUILD_MASTER"
#endif
#endif
