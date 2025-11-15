// Arduino IDE/arduino-cli only compile sources at the sketch root.
// Pull in the modules the slave firmware needs from the shared master tree.
#ifndef PLATFORMIO
#include "core/system_init.cpp"
#include "comms/slave_endpoint.cpp"
#include "config/runtime_config.cpp"
#include "control/drive_controller.cpp"
#include "control/pid.cpp"
#include "drivers/battery_monitor.cpp"
#include "drivers/motor_driver.cpp"
#include "drivers/pca9685.cpp"
#include "features/lighting.cpp"
#endif
