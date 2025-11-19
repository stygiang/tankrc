#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Shared hardware metadata describing each physical board and its pins.
// These enums intentionally match the notation used across the hardware docs
// and UI so data can be serialized without additional mapping.
namespace TankRC::Hardware {
enum HardwareClass : std::uint8_t {
    HW_CLASS_UNKNOWN = 0,
    HW_CLASS_MCU,
    HW_CLASS_SENSOR,
    HW_CLASS_EXPANDER,
    HW_CLASS_PERIPHERAL,
};

enum BusType : std::uint8_t {
    BUS_UNKNOWN = 0,
    BUS_LOCAL,
    BUS_I2C,
    BUS_SPI,
    BUS_UART,
    BUS_CAN,
};

enum PinRole : std::uint8_t {
    PIN_ROLE_UNUSED = 0,
    PIN_ROLE_GPIO,
    PIN_ROLE_UART_TX,
    PIN_ROLE_UART_RX,
    PIN_ROLE_I2C_SDA,
    PIN_ROLE_I2C_SCL,
    PIN_ROLE_SPI_MOSI,
    PIN_ROLE_SPI_MISO,
    PIN_ROLE_SPI_SCK,
    PIN_ROLE_SPI_CS,
    PIN_ROLE_ADC,
    PIN_ROLE_DAC,
    PIN_ROLE_PWM,
    PIN_ROLE_POWER,
    PIN_ROLE_GROUND,
};

struct PinRecord {
    int id = -1;
    int phys_num = -1;
    PinRole role = PIN_ROLE_UNUSED;
    bool active_low = false;
    const char* label = nullptr;
};

constexpr std::size_t kMaxPinsPerHardware = 32;

struct Hardware {
    const char* name = nullptr;
    HardwareClass hclass = HW_CLASS_UNKNOWN;
    BusType bus = BUS_UNKNOWN;
    std::uint8_t bus_addr = 0;
    std::size_t pin_count = 0;
    std::array<PinRecord, kMaxPinsPerHardware> pins{};
};
}  // namespace TankRC::Hardware
