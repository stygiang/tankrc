// Arduino IDE/arduino-cli only compile sources at the sketch root.
// Pull in every module implementation here so they build as one TU.
// PlatformIO sees the actual .cpp files, so we disable this include fan-out there.
#ifndef PLATFORMIO
#include "core/system_init.cpp"
#include "comms/radio_link.cpp"
#include "config/runtime_config.cpp"
#include "control/drive_controller.cpp"
#include "control/pid.cpp"
#include "drivers/motor_driver.cpp"
#include "drivers/battery_monitor.cpp"
#include "drivers/pca9685.cpp"
#include "drivers/rc_receiver.cpp"
#include "logging/session_logger.cpp"
#include "network/control_server.cpp"
#include "network/remote_console.cpp"
#include "network/wifi_manager.cpp"
#include "time/ntp_clock.cpp"
#include "features/lighting.cpp"
#include "features/sound_fx.cpp"
#include "storage/config_store.cpp"
#include "ui/console.cpp"
#endif
