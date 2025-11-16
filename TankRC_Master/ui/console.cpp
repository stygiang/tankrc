#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdarg>
#include <iterator>
#include <vector>

#include "comms/radio_link.h"
#include "control/drive_controller.h"
#include "drivers/rc_receiver.h"
#include "features/sound_fx.h"
#include "ui/console.h"

namespace TankRC::UI {
namespace {
class ConsoleWriter : public Print {
  public:
    size_t write(uint8_t b) override {
        Serial.write(b);
        for (auto* tap : taps_) {
            if (tap) {
                tap->write(b);
            }
        }
        return 1;
    }

    void addTap(Print* tap) {
        if (!tap) {
            return;
        }
        for (auto* existing : taps_) {
            if (existing == tap) {
                return;
            }
        }
        for (auto& slot : taps_) {
            if (!slot) {
                slot = tap;
                return;
            }
        }
    }

    void removeTap(Print* tap) {
        if (!tap) {
            return;
        }
        for (auto& slot : taps_) {
            if (slot == tap) {
                slot = nullptr;
            }
        }
    }

    void printPrompt() {
        print(F("> "));
    }

    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        print(buffer);
    }

  private:
    std::array<Print*, 4> taps_{};
};

static ConsoleWriter console;

Context ctx_{};
ApplyConfigCallback applyCallback_ = nullptr;
String inputBuffer_;
bool promptShown_ = false;
bool wizardActive_ = false;
bool wizardAbortRequested_ = false;
ConsoleSource activeSource_ = ConsoleSource::Serial;
ConsoleSource wizardSource_ = ConsoleSource::Serial;
bool wizardInputPending_ = false;
String wizardInputBuffer_;
static Config::RuntimeConfig baselineConfig_{};
static bool baselineInitialized_ = false;
bool lastCharWasCr_ = false;
bool lastCharWasLf_ = false;

void processLine(const String& line, ConsoleSource source);
void beginWizardSession();
void finishWizardSession();
void runHelpMenu();
static void snapshotBaseline() {
    Serial.println(F("[TRACE] snapshotBaseline"));
    if (ctx_.config) {
        baselineConfig_ = *ctx_.config;
        baselineInitialized_ = true;
    }
}

void beginWizardSession() {
    Serial.println(F("[TRACE] beginWizardSession"));
    wizardActive_ = true;
    wizardSource_ = activeSource_;
    wizardInputPending_ = false;
    wizardInputBuffer_.clear();
    wizardAbortRequested_ = false;
    inputBuffer_.clear();
}

void finishWizardSession() {
    Serial.println(F("[TRACE] finishWizardSession"));
    wizardActive_ = false;
    wizardInputPending_ = false;
    wizardAbortRequested_ = false;
}

void processLine(const String& line, ConsoleSource source);

String readLineBlocking() {
    const ConsoleSource source = wizardActive_ ? wizardSource_ : ConsoleSource::Serial;
    String line;
    while (true) {
        Serial.println(F("[TRACE] readLineBlocking loop"));
        if (source == ConsoleSource::Serial) {
            while (Serial.available()) {
                char c = static_cast<char>(Serial.read());
                if (c == '\r') {
                    continue;
                }
                if (c == '\n') {
                    return line;
                }
                line += c;
            }
        } else {
            if (wizardInputPending_) {
                wizardInputPending_ = false;
                String ready = wizardInputBuffer_;
                wizardInputBuffer_.clear();
                return ready;
            }
        }
        delay(10);
    }
}

int promptInt(const String& label, int current) {
    Serial.println(F("[TRACE] promptInt"));
    console.print(label);
    console.print(" [");
    console.print(current);
    console.print("] : ");
    String line = readLineBlocking();
    line.trim();
    if (line.isEmpty()) {
        return current;
    }
    String lower = line;
    lower.toLowerCase();
    if (lower == "q" || lower == "quit" || lower == "exit") {
        wizardAbortRequested_ = true;
        return current;
    }
    return line.toInt();
}

bool promptBool(const String& label, bool current) {
    Serial.println(F("[TRACE] promptBool"));
    console.print(label);
    console.print(" [");
    console.print(current ? "Y" : "N");
    console.print("] : ");

    while (true) {
        String line = readLineBlocking();
        line.trim();
        line.toLowerCase();
        if (line.isEmpty()) {
            return current;
        }
        if (line == "q" || line == "quit" || line == "exit") {
            wizardAbortRequested_ = true;
            return current;
        }
        if (line == "y" || line == "yes" || line == "1" || line == "true") {
            return true;
        }
        if (line == "n" || line == "no" || line == "0" || line == "false") {
            return false;
        }
        console.print("Please type y/n: ");
    }
}

String promptString(const String& label, const String& current, size_t maxLen) {
    Serial.println(F("[TRACE] promptString"));
    console.print(label);
    console.print(" [");
    console.print(current);
    console.print("] : ");
    String line = readLineBlocking();
    line.trim();
    if (line.isEmpty()) {
        return current;
    }
    String lower = line;
    lower.toLowerCase();
    if (lower == "q" || lower == "quit" || lower == "exit") {
        wizardAbortRequested_ = true;
        return current;
    }
    if (maxLen > 0 && line.length() >= static_cast<int>(maxLen)) {
        line = line.substring(0, static_cast<int>(maxLen) - 1);
    }
    return line;
}

String describeOwnedPin(const Config::OwnedPin& pin) {
    if (pin.owner == Config::PinOwner::IoExpander) {
        return String("IO[") + pin.expanderPin + "]";
    }
    if (pin.gpio < 0) {
        return String(F("DISABLED"));
    }
    return String("GPIO ") + pin.gpio;
}

bool ownedPinsEqual(const Config::OwnedPin& a, const Config::OwnedPin& b) {
    return a.owner == b.owner && a.gpio == b.gpio && a.expanderPin == b.expanderPin;
}

bool parseBoolToken(const String& text, bool& out) {
    String lower = text;
    lower.trim();
    lower.toLowerCase();
    if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "off" || lower == "no") {
        out = false;
        return true;
    }
    return false;
}

bool parseOwnedPinValue(const String& value, Config::OwnedPin& pin) {
    String trimmed = value;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return false;
    }
    String lower = trimmed;
    lower.toLowerCase();
    if (lower.startsWith("io")) {
        int sep = lower.indexOf(':');
        String number = sep >= 0 ? lower.substring(sep + 1) : lower.substring(2);
        number.trim();
        const int idx = number.toInt();
        pin.owner = Config::PinOwner::IoExpander;
        pin.expanderPin = static_cast<std::uint8_t>(std::max(0, idx));
        pin.gpio = -1;
        return true;
    }
    if (lower == "none" || lower == "off") {
        pin.owner = Config::PinOwner::Slave;
        pin.gpio = -1;
        pin.expanderPin = 0;
        return true;
    }
    pin.owner = Config::PinOwner::Slave;
    pin.gpio = trimmed.toInt();
    pin.expanderPin = 0;
    return true;
}

struct HelpEntry {
    const __FlashStringHelper* command;
    const __FlashStringHelper* description;
};

template <std::size_t N>
void printHelpSection(const __FlashStringHelper* title, const HelpEntry (&entries)[N]) {
    console.println();
    console.print(F("-- "));
    console.print(title);
    console.println(F(" --"));
    for (const auto& entry : entries) {
        console.print(F("  * "));
        console.print(entry.command);
        console.print(F(" : "));
        console.println(entry.description);
    }
}

static const HelpEntry kHelpQuickCommands[] = {
    {F("show / s"), F("Print current runtime configuration")},
    {F("pin <token> [value]"), F("Inspect or change a single pin assignment")},
    {F("load / ld"), F("Reload the last saved settings")},
    {F("save / sv"), F("Persist the current configuration to flash")},
};

static const HelpEntry kHelpWizards[] = {
    {F("wizard pins / wp"), F("Guided pin assignment setup")},
    {F("wizard features / wf"), F("Toggle feature modules on/off")},
    {F("wizard wifi / ww"), F("Configure Wi-Fi credentials and AP mode")},
    {F("wizard test / wt"), F("Run drivetrain, sound, and battery tests")},
};

static const HelpEntry kHelpMaintenance[] = {
    {F("defaults / df"), F("Restore factory defaults in RAM")},
    {F("reset / rs"), F("Erase saved settings from flash storage")},
};

static const HelpEntry kHelpShortcuts[] = {
    {F("help"), F("Open the interactive help hub")},
    {F("? or h"), F("Show the quick reference")},
    {F("pin help"), F("List pin-token names for the 'pin' command")},
};
 
void showHelp() {
    console.println();
    console.println(F("=== TankRC Serial Console ==="));
    console.println(F("Commands:"));
    console.println(F("  help (h, ?)     - Show this list"));
    console.println(F("  show (s)        - Dump current configuration"));
    console.println(F("  wizard pins (wp)- Interactive pin assignment wizard"));
    console.println(F("  wizard features (wf) - Enable/disable feature modules"));
    console.println(F("  wizard test (wt)- Launch interactive test suite"));
    console.println(F("  pin <token> [value] - Show/update a single pin (type 'pin help' for tokens)"));
    console.println(F("  wizard wifi (ww) - Configure Wi-Fi credentials / AP settings"));
    console.println(F("  save (sv)       - Persist current settings to flash"));
    console.println(F("  load (ld)       - Reload last saved settings"));
    console.println(F("  defaults (df)   - Restore factory defaults"));
    console.println(F("  reset (rs)      - Clear saved settings from flash"));
    console.println();
}

void runHelpMenu() {
    if (wizardActive_) {
        console.println(F("Another interactive session is already running."));
        return;
    }

    beginWizardSession();

    bool exitRequested = false;
    while (!exitRequested && !wizardAbortRequested_) {
        console.println();
        console.println(F("=============================================="));
        console.println(F("       TankRC Interactive Help Hub"));
        console.println(F("=============================================="));
        console.println(F(" 1) Quick commands"));
        console.println(F(" 2) Setup wizards"));
        console.println(F(" 3) Maintenance & recovery"));
        console.println(F(" 4) Full quick reference"));
        console.println(F(" 0) Exit help"));

        const int choice = promptInt("Choose an option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                printHelpSection(F("Quick Commands"), kHelpQuickCommands);
                break;
            case 2:
                printHelpSection(F("Setup Wizards"), kHelpWizards);
                break;
            case 3:
                printHelpSection(F("Maintenance"), kHelpMaintenance);
                printHelpSection(F("Help & Tips"), kHelpShortcuts);
                break;
            case 4:
                showHelp();
                break;
            case 0:
                exitRequested = true;
                console.println(F("Closing help hub."));
                break;
            default:
                console.println(F("Unknown selection. Please choose 0-4."));
                break;
        }
    }

    if (wizardAbortRequested_) {
        console.println(F("Help hub dismissed."));
    }

    finishWizardSession();
}

void showConfig() {
    Serial.println(F("[TRACE] showConfig"));
    if (!ctx_.config) {
        console.println(F("No config available."));
        return;
    }

    const auto& pins = ctx_.config->pins;
    const auto& features = ctx_.config->features;
    console.println(F("--- Pin Assignments ---"));
    auto printChannel = [&](const char* label, const Config::ChannelPins& ch) {
        const String in1 = describeOwnedPin(ch.in1);
        const String in2 = describeOwnedPin(ch.in2);
        console.printf("%s (PWM,IN1,IN2): %d, %s, %s\n", label, ch.pwm, in1.c_str(), in2.c_str());
    };
    const String leftStby = describeOwnedPin(pins.leftDriver.standby);
    const String rightStby = describeOwnedPin(pins.rightDriver.standby);
    printChannel("Left Motor A", pins.leftDriver.motorA);
    printChannel("Left Motor B", pins.leftDriver.motorB);
    console.printf("Left Driver STBY: %s\n", leftStby.c_str());
    printChannel("Right Motor A", pins.rightDriver.motorA);
    printChannel("Right Motor B", pins.rightDriver.motorB);
    console.printf("Right Driver STBY: %s\n", rightStby.c_str());
    console.printf("Light bar pin: %d\n", pins.lightBar);
    console.printf("Speaker pin: %d\n", pins.speaker);
    console.printf("Battery sense pin: %d\n", pins.batterySense);
    console.printf("Slave link TX/RX: %d / %d\n", pins.slaveTx, pins.slaveRx);
    console.println(F("--- IO Expander ---"));
    console.printf("Enabled: %s\n", pins.ioExpander.enabled ? "yes" : "no");
    console.printf("Address: 0x%02X\n", pins.ioExpander.address);
    console.printf("Use PCA9548A: %s (addr 0x%02X, channel %u)\n",
                  pins.ioExpander.useMux ? "yes" : "no",
                  pins.ioExpander.muxAddress,
                  static_cast<unsigned>(pins.ioExpander.muxChannel));

    console.println(F("--- RC Receiver Pins ---"));
    for (std::size_t i = 0; i < Drivers::RcReceiver::kChannelCount; ++i) {
        console.printf("CH%u pin: %d\n", static_cast<unsigned>(i + 1), ctx_.config->rc.channelPins[i]);
    }

    console.println(F("--- Lighting ---"));
    console.printf("PCA9685 addr: 0x%02X, freq: %u Hz\n", ctx_.config->lighting.pcaAddress, ctx_.config->lighting.pwmFrequency);
    auto printRgb = [](const char* name, const Config::RgbChannel& rgb) {
        console.printf("%s -> R:%d G:%d B:%d\n", name, rgb.r, rgb.g, rgb.b);
    };
    printRgb("Front Left", ctx_.config->lighting.channels.frontLeft);
    printRgb("Front Right", ctx_.config->lighting.channels.frontRight);
    printRgb("Rear Left", ctx_.config->lighting.channels.rearLeft);
    printRgb("Rear Right", ctx_.config->lighting.channels.rearRight);
    console.printf("Blink wifi:%s rc:%s period:%ums\n",
                  ctx_.config->lighting.blink.wifi ? "on" : "off",
                  ctx_.config->lighting.blink.rc ? "on" : "off",
                  ctx_.config->lighting.blink.periodMs);

    console.println(F("--- Feature Flags ---"));
    console.printf("Lights enabled: %s\n", features.lightsEnabled ? "yes" : "no");
    console.printf("Sound enabled: %s\n", features.soundEnabled ? "yes" : "no");
    console.printf("Sensors enabled: %s\n", features.sensorsEnabled ? "yes" : "no");
    console.printf("Wi-Fi enabled: %s\n", features.wifiEnabled ? "yes" : "no");
    console.printf("Ultrasonic enabled: %s\n", features.ultrasonicEnabled ? "yes" : "no");
    console.printf("Tip-over enabled: %s\n", features.tipOverEnabled ? "yes" : "no");
    const auto& health = Health::getStatus();
    console.printf("Health: %s (%s)\n", Health::toString(health.code), health.message);
    console.println();
}

struct PinTokenInfo {
    enum class Kind { IntValue, OwnedPin };
    String token;
    const char* description;
    Kind kind = Kind::IntValue;
    int* value = nullptr;
    Config::OwnedPin* owned = nullptr;
    int baseline = -1;
    Config::OwnedPin ownedBaseline{};
};

static std::vector<PinTokenInfo> collectPinTokens() {
    std::vector<PinTokenInfo> tokens;
    if (!ctx_.config) {
        return tokens;
    }

    auto& pins = ctx_.config->pins;
    auto& rc = ctx_.config->rc;
    const auto* basePins = baselineInitialized_ ? &baselineConfig_.pins : nullptr;
    const auto* baseRc = baselineInitialized_ ? &baselineConfig_.rc : nullptr;

    auto baselineValue = [&](int current, const int* basePtr) {
        return (baselineInitialized_ && basePtr) ? *basePtr : current;
    };

    auto baselinePin = [&](const Config::OwnedPin& current, const Config::OwnedPin* basePtr) {
        return (baselineInitialized_ && basePtr) ? *basePtr : current;
    };

    auto addInt = [&](const char* token, const char* desc, int& ref, const int* basePtr) {
        PinTokenInfo info;
        info.token = token;
        info.description = desc;
        info.kind = PinTokenInfo::Kind::IntValue;
        info.value = &ref;
        info.baseline = baselineValue(ref, basePtr);
        tokens.push_back(info);
    };

    auto addOwned = [&](const char* token, const char* desc, Config::OwnedPin& ref, const Config::OwnedPin* basePtr) {
        PinTokenInfo info;
        info.token = token;
        info.description = desc;
        info.kind = PinTokenInfo::Kind::OwnedPin;
        info.owned = &ref;
        info.ownedBaseline = baselinePin(ref, basePtr);
        tokens.push_back(info);
    };

    const auto& bp = baselineConfig_.pins;
    addInt("lma_pwm", "Left motor A PWM", pins.leftDriver.motorA.pwm, basePins ? &bp.leftDriver.motorA.pwm : nullptr);
    addOwned("lma_in1", "Left motor A IN1", pins.leftDriver.motorA.in1, basePins ? &bp.leftDriver.motorA.in1 : nullptr);
    addOwned("lma_in2", "Left motor A IN2", pins.leftDriver.motorA.in2, basePins ? &bp.leftDriver.motorA.in2 : nullptr);
    addInt("lmb_pwm", "Left motor B PWM", pins.leftDriver.motorB.pwm, basePins ? &bp.leftDriver.motorB.pwm : nullptr);
    addOwned("lmb_in1", "Left motor B IN1", pins.leftDriver.motorB.in1, basePins ? &bp.leftDriver.motorB.in1 : nullptr);
    addOwned("lmb_in2", "Left motor B IN2", pins.leftDriver.motorB.in2, basePins ? &bp.leftDriver.motorB.in2 : nullptr);
    addOwned("left_stby", "Left driver STBY", pins.leftDriver.standby, basePins ? &bp.leftDriver.standby : nullptr);

    addInt("rma_pwm", "Right motor A PWM", pins.rightDriver.motorA.pwm, basePins ? &bp.rightDriver.motorA.pwm : nullptr);
    addOwned("rma_in1", "Right motor A IN1", pins.rightDriver.motorA.in1, basePins ? &bp.rightDriver.motorA.in1 : nullptr);
    addOwned("rma_in2", "Right motor A IN2", pins.rightDriver.motorA.in2, basePins ? &bp.rightDriver.motorA.in2 : nullptr);
    addInt("rmb_pwm", "Right motor B PWM", pins.rightDriver.motorB.pwm, basePins ? &bp.rightDriver.motorB.pwm : nullptr);
    addOwned("rmb_in1", "Right motor B IN1", pins.rightDriver.motorB.in1, basePins ? &bp.rightDriver.motorB.in1 : nullptr);
    addOwned("rmb_in2", "Right motor B IN2", pins.rightDriver.motorB.in2, basePins ? &bp.rightDriver.motorB.in2 : nullptr);
    addOwned("right_stby", "Right driver STBY", pins.rightDriver.standby, basePins ? &bp.rightDriver.standby : nullptr);

    addInt("lightbar", "Light bar pin", pins.lightBar, basePins ? &bp.lightBar : nullptr);
    addInt("speaker", "Speaker pin", pins.speaker, basePins ? &bp.speaker : nullptr);
    addInt("battery", "Battery sense pin", pins.batterySense, basePins ? &bp.batterySense : nullptr);
    addInt("slave_tx", "Slave link TX pin", pins.slaveTx, basePins ? &bp.slaveTx : nullptr);
    addInt("slave_rx", "Slave link RX pin", pins.slaveRx, basePins ? &bp.slaveRx : nullptr);

    for (std::size_t i = 0; i < std::size(rc.channelPins); ++i) {
        String name = "rc" + String(i + 1);
        PinTokenInfo info;
        info.token = name;
        info.description = "RC channel pin";
        info.value = &rc.channelPins[i];
        info.baseline = baselineValue(rc.channelPins[i], baseRc ? &baseRc->channelPins[i] : nullptr);
        tokens.push_back(info);
    }

    return tokens;
}

static void printPinList() {
    const auto tokens = collectPinTokens();
    if (tokens.empty()) {
        console.println(F("No pin data available."));
        return;
    }
    console.println(F("--- Pin Tokens ---"));
    for (const auto& t : tokens) {
        if (t.kind == PinTokenInfo::Kind::OwnedPin) {
            const String desc = describeOwnedPin(*t.owned);
            console.printf("%-10s = %-8s (%s)\n", t.token.c_str(), desc.c_str(), t.description);
        } else {
            console.printf("%-10s = %-4d (%s)\n", t.token.c_str(), *t.value, t.description);
        }
    }
    if (ctx_.config) {
        const auto& io = ctx_.config->pins.ioExpander;
        console.printf("io_enabled = %s\n", io.enabled ? "on" : "off");
        console.printf("io_addr    = %u\n", io.address);
        console.printf("io_mux_enabled = %s\n", io.useMux ? "on" : "off");
        console.printf("io_mux_addr    = %u\n", io.muxAddress);
        console.printf("io_mux_channel = %u\n", io.muxChannel);
    }
}

static void printPinDiff() {
    if (!baselineInitialized_) {
        console.println(F("No saved baseline yet. Save the config first."));
        return;
    }
    const auto tokens = collectPinTokens();
    bool any = false;
    for (const auto& t : tokens) {
        bool changed = false;
        if (t.kind == PinTokenInfo::Kind::OwnedPin) {
            if (!ownedPinsEqual(*t.owned, t.ownedBaseline)) {
                changed = true;
            }
        } else if (*t.value != t.baseline) {
            changed = true;
        }
        if (changed) {
            if (!any) {
                console.println(F("--- Pin diffs since last save ---"));
                any = true;
            }
            if (t.kind == PinTokenInfo::Kind::OwnedPin) {
                const String before = describeOwnedPin(t.ownedBaseline);
                const String after = describeOwnedPin(*t.owned);
                console.printf("%-10s: %s -> %s\n", t.token.c_str(), before.c_str(), after.c_str());
            } else {
                console.printf("%-10s: %d -> %d\n", t.token.c_str(), t.baseline, *t.value);
            }
        }
    }
    if (ctx_.config) {
        const auto& currentIo = ctx_.config->pins.ioExpander;
        const auto& baseIo = baselineConfig_.pins.ioExpander;
        auto emitIoDiff = [&](const char* name, auto before, auto after) {
            if (before != after) {
                if (!any) {
                    console.println(F("--- Pin diffs since last save ---"));
                    any = true;
                }
                console.printf("%-10s: %ld -> %ld\n", name, static_cast<long>(before), static_cast<long>(after));
            }
        };
        emitIoDiff("io_enabled", baseIo.enabled, currentIo.enabled);
        emitIoDiff("io_addr", baseIo.address, currentIo.address);
        emitIoDiff("io_mux_enabled", baseIo.useMux, currentIo.useMux);
        emitIoDiff("io_mux_addr", baseIo.muxAddress, currentIo.muxAddress);
        emitIoDiff("io_mux_channel", baseIo.muxChannel, currentIo.muxChannel);
    }
    if (!any) {
        console.println(F("No pin changes since last save."));
    }
}

bool configureOwnedPinField(const char* label, Config::OwnedPin& pin) {
    const bool currentExp = pin.owner == Config::PinOwner::IoExpander;
    const bool useExp = promptBool(String(label) + " via IO expander", currentExp);
    if (wizardAbortRequested_) {
        return false;
    }
    if (useExp) {
        pin.owner = Config::PinOwner::IoExpander;
        const int idx = promptInt(String(label) + " expander pin (0-15)", pin.expanderPin);
        if (wizardAbortRequested_) {
            return false;
        }
        pin.expanderPin = static_cast<std::uint8_t>(std::max(0, idx));
        pin.gpio = -1;
    } else {
        pin.owner = Config::PinOwner::Slave;
        pin.expanderPin = 0;
        pin.gpio = promptInt(String(label) + " GPIO", pin.gpio);
        if (wizardAbortRequested_) {
            return false;
        }
    }
    return true;
}

void configureChannel(const char* label, Config::ChannelPins& pins) {
    console.println(label);
    pins.pwm = promptInt("  PWM", pins.pwm);
    if (wizardAbortRequested_) return;
    if (!configureOwnedPinField("  IN1", pins.in1)) return;
    if (!configureOwnedPinField("  IN2", pins.in2)) return;
}

void configureRgbChannel(const char* label, Config::RgbChannel& rgb) {
    console.println(label);
    rgb.r = promptInt("  Red channel", rgb.r);
    rgb.g = promptInt("  Green channel", rgb.g);
    rgb.b = promptInt("  Blue channel", rgb.b);
}

void configureRcPins(Config::RcConfig& rc) {
    static const char* const labels[Drivers::RcReceiver::kChannelCount] = {
        "Channel 1 (steering)",
        "Channel 2 (throttle)",
        "Channel 3 (aux button)",
        "Channel 4 (mode switch)",
        "Channel 5 (ultrasonic A)",
        "Channel 6 (ultrasonic B)",
    };

    console.println(F("RC receiver pins:"));
    for (std::size_t i = 0; i < Drivers::RcReceiver::kChannelCount; ++i) {
        rc.channelPins[i] = promptInt(labels[i], rc.channelPins[i]);
        if (wizardAbortRequested_) {
            break;
        }
    }
}

void configureLighting(Config::LightingConfig& lighting) {
    console.println(F("PCA9685 lighting setup:"));
    int addr = promptInt("  I2C address (decimal, 64 = 0x40)", lighting.pcaAddress);
    if (addr >= 0 && addr <= 127) {
        lighting.pcaAddress = static_cast<std::uint8_t>(addr);
    }
    int freq = promptInt("  PWM frequency (Hz)", lighting.pwmFrequency);
    if (freq > 0) {
        lighting.pwmFrequency = static_cast<std::uint16_t>(freq);
    }
    configureRgbChannel("Front left RGB channels", lighting.channels.frontLeft);
    if (wizardAbortRequested_) return;
    configureRgbChannel("Front right RGB channels", lighting.channels.frontRight);
    if (wizardAbortRequested_) return;
    configureRgbChannel("Rear left RGB channels", lighting.channels.rearLeft);
    if (wizardAbortRequested_) return;
    configureRgbChannel("Rear right RGB channels", lighting.channels.rearRight);
    if (wizardAbortRequested_) return;
    lighting.blink.wifi = promptBool("Blink when WiFi disconnected", lighting.blink.wifi);
    if (wizardAbortRequested_) return;
    lighting.blink.rc = promptBool("Blink when RC link lost", lighting.blink.rc);
    if (wizardAbortRequested_) return;
    lighting.blink.periodMs = static_cast<std::uint16_t>(promptInt("Blink period (ms)", lighting.blink.periodMs));
}

void configureIoExpander(Config::IoExpanderConfig& io) {
    console.println(F("IO expander setup:"));
    io.enabled = promptBool("  Enabled", io.enabled);
    if (wizardAbortRequested_) return;
    int addr = promptInt("  Expander I2C address", io.address);
    if (wizardAbortRequested_) return;
    if (addr >= 0 && addr <= 127) {
        io.address = static_cast<std::uint8_t>(addr);
    }
    io.useMux = promptBool("  Use PCA9548A mux", io.useMux);
    if (wizardAbortRequested_) return;
    if (io.useMux) {
        int muxAddr = promptInt("  Mux address", io.muxAddress);
        if (wizardAbortRequested_) return;
        if (muxAddr >= 0 && muxAddr <= 127) {
            io.muxAddress = static_cast<std::uint8_t>(muxAddr);
        }
        int channel = promptInt("  Mux channel (0-7)", io.muxChannel);
        if (wizardAbortRequested_) return;
        if (channel >= 0 && channel <= 7) {
            io.muxChannel = static_cast<std::uint8_t>(channel);
        }
    }
}

void runWifiWizard() {
    Serial.println(F("[TRACE] runWifiWizard"));
    if (!ctx_.config) {
        console.println(F("Config not initialized."));
        return;
    }

    beginWizardSession();

    Config::WifiConfig wifi = ctx_.config->wifi;
    auto toString = [](const char* data) { return (data && data[0]) ? String(data) : String(); };

    console.println(F("Wi-Fi configuration (leave blank to keep current value, or type 'q' to exit)."));
    const String staSsid = promptString("Station SSID", toString(wifi.ssid), sizeof(wifi.ssid));
    if (wizardAbortRequested_) {
        console.println(F("Wi-Fi wizard cancelled."));
        finishWizardSession();
        return;
    }
    const String staPass = promptString("Station Password", wifi.password[0] ? "[hidden]" : "", sizeof(wifi.password));
    if (wizardAbortRequested_) {
        console.println(F("Wi-Fi wizard cancelled."));
        finishWizardSession();
        return;
    }
    const String apSsid = promptString("Access Point SSID", toString(wifi.apSsid), sizeof(wifi.apSsid));
    if (wizardAbortRequested_) {
        console.println(F("Wi-Fi wizard cancelled."));
        finishWizardSession();
        return;
    }
    const String apPass = promptString("Access Point Password", wifi.apPassword[0] ? "[hidden]" : "", sizeof(wifi.apPassword));
    if (wizardAbortRequested_) {
        console.println(F("Wi-Fi wizard cancelled."));
        finishWizardSession();
        return;
    }

    if (!staSsid.isEmpty()) {
        staSsid.toCharArray(wifi.ssid, sizeof(wifi.ssid));
    }
    if (!staPass.isEmpty() && staPass != "[hidden]") {
        staPass.toCharArray(wifi.password, sizeof(wifi.password));
    }
    if (!apSsid.isEmpty()) {
        apSsid.toCharArray(wifi.apSsid, sizeof(wifi.apSsid));
    }
    if (!apPass.isEmpty() && apPass != "[hidden]") {
        apPass.toCharArray(wifi.apPassword, sizeof(wifi.apPassword));
    }

    const bool apply = promptBool("Apply Wi-Fi changes?", true);
    if (apply) {
        ctx_.config->wifi = wifi;
        if (applyCallback_) {
            applyCallback_();
        }
        console.println(F("Wi-Fi settings updated. Device may restart networking."));
    } else {
        console.println(F("Wi-Fi changes discarded."));
    }

    finishWizardSession();
}

void handlePinCommand(const String& args) {
    if (!ctx_.config) {
        console.println(F("Config not initialized."));
        return;
    }
    String trimmed = args;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        console.println(F("Usage: pin <token> [value]. Type 'pin help' for the token list."));
        return;
    }
    String token;
    String valueStr;
    int space = trimmed.indexOf(' ');
    if (space < 0) {
        token = trimmed;
    } else {
        token = trimmed.substring(0, space);
        valueStr = trimmed.substring(space + 1);
        valueStr.trim();
    }
    token.toLowerCase();
    if (token == "help") {
        console.println(F("Tokens:"));
        console.println(F("  lma_pwm,lma_in1,lma_in2"));
        console.println(F("  lmb_pwm,lmb_in1,lmb_in2"));
        console.println(F("  rma_pwm,rma_in1,rma_in2"));
        console.println(F("  rmb_pwm,rmb_in1,rmb_in2"));
        console.println(F("  left_stby,right_stby,lightbar,speaker,battery,slave_tx,slave_rx"));
        console.println(F("  rc1,rc2,rc3,rc4,rc5,rc6"));
        console.println(F("Value hints: use numbers for GPIO or io:<n> to map to the expander."));
        console.println(F("IO expander tokens: io_enabled, io_addr, io_mux_enabled, io_mux_addr, io_mux_channel"));
        console.println(F("  list  (show all pins)"));
        console.println(F("  diff  (show pins changed since last save)"));
        return;
    }
    if (token == "list") {
        printPinList();
        return;
    }
    if (token == "diff") {
        printPinDiff();
        return;
    }

    auto& pins = ctx_.config->pins;
    struct IntBinding {
        const char* name;
        int* ptr;
    };
    struct OwnedBinding {
        const char* name;
        Config::OwnedPin* ptr;
    };
    OwnedBinding ownedBindings[] = {
        {"lma_in1", &pins.leftDriver.motorA.in1},
        {"lma_in2", &pins.leftDriver.motorA.in2},
        {"lmb_in1", &pins.leftDriver.motorB.in1},
        {"lmb_in2", &pins.leftDriver.motorB.in2},
        {"rma_in1", &pins.rightDriver.motorA.in1},
        {"rma_in2", &pins.rightDriver.motorA.in2},
        {"rmb_in1", &pins.rightDriver.motorB.in1},
        {"rmb_in2", &pins.rightDriver.motorB.in2},
        {"left_stby", &pins.leftDriver.standby},
        {"right_stby", &pins.rightDriver.standby},
    };

    for (const auto& binding : ownedBindings) {
        if (token == binding.name) {
            if (valueStr.isEmpty()) {
                console.printf("%s = %s\n", binding.name, describeOwnedPin(*binding.ptr).c_str());
            } else if (parseOwnedPinValue(valueStr, *binding.ptr)) {
                console.printf("%s set to %s\n", binding.name, describeOwnedPin(*binding.ptr).c_str());
                if (applyCallback_) {
                    applyCallback_();
                }
            } else {
                console.println(F("Invalid value. Use a GPIO number or io:<pin>."));
            }
            return;
        }
    }

    IntBinding bindings[] = {
        {"lma_pwm", &pins.leftDriver.motorA.pwm},
        {"lmb_pwm", &pins.leftDriver.motorB.pwm},
        {"rma_pwm", &pins.rightDriver.motorA.pwm},
        {"rmb_pwm", &pins.rightDriver.motorB.pwm},
        {"lightbar", &pins.lightBar},
        {"speaker", &pins.speaker},
        {"battery", &pins.batterySense},
        {"slave_tx", &pins.slaveTx},
        {"slave_rx", &pins.slaveRx},
    };

    for (const auto& binding : bindings) {
        if (token == binding.name) {
            if (valueStr.isEmpty()) {
                console.printf("%s = %d\n", binding.name, *binding.ptr);
            } else {
                const int value = valueStr.toInt();
                *binding.ptr = value;
                console.printf("%s set to %d\n", binding.name, value);
                if (applyCallback_) {
                    applyCallback_();
                }
            }
            return;
        }
    }

    if (token == "io_enabled") {
        if (valueStr.isEmpty()) {
            console.printf("io_enabled = %s\n", pins.ioExpander.enabled ? "on" : "off");
        } else {
            bool val = pins.ioExpander.enabled;
            if (parseBoolToken(valueStr, val)) {
                pins.ioExpander.enabled = val;
                console.printf("io_enabled set to %s\n", val ? "on" : "off");
                if (applyCallback_) {
                    applyCallback_();
                }
            } else {
                console.println(F("Use on/off or 1/0."));
            }
        }
        return;
    }
    if (token == "io_mux_enabled") {
        if (valueStr.isEmpty()) {
            console.printf("io_mux_enabled = %s\n", pins.ioExpander.useMux ? "on" : "off");
        } else {
            bool val = pins.ioExpander.useMux;
            if (parseBoolToken(valueStr, val)) {
                pins.ioExpander.useMux = val;
                console.printf("io_mux_enabled set to %s\n", val ? "on" : "off");
                if (applyCallback_) applyCallback_();
            } else {
                console.println(F("Use on/off or 1/0."));
            }
        }
        return;
    }
    if (token == "io_addr") {
        if (valueStr.isEmpty()) {
            console.printf("io_addr = %u\n", pins.ioExpander.address);
        } else {
            int addr = valueStr.toInt();
            if (addr >= 0 && addr <= 127) {
                pins.ioExpander.address = static_cast<std::uint8_t>(addr);
                console.printf("io_addr set to %u\n", pins.ioExpander.address);
                if (applyCallback_) applyCallback_();
            } else {
                console.println(F("Address must be 0-127."));
            }
        }
        return;
    }
    if (token == "io_mux_addr") {
        if (valueStr.isEmpty()) {
            console.printf("io_mux_addr = %u\n", pins.ioExpander.muxAddress);
        } else {
            int addr = valueStr.toInt();
            if (addr >= 0 && addr <= 127) {
                pins.ioExpander.muxAddress = static_cast<std::uint8_t>(addr);
                console.printf("io_mux_addr set to %u\n", pins.ioExpander.muxAddress);
                if (applyCallback_) applyCallback_();
            } else {
                console.println(F("Mux address must be 0-127."));
            }
        }
        return;
    }
    if (token == "io_mux_channel") {
        if (valueStr.isEmpty()) {
            console.printf("io_mux_channel = %u\n", pins.ioExpander.muxChannel);
        } else {
            int chan = valueStr.toInt();
            if (chan >= 0 && chan <= 7) {
                pins.ioExpander.muxChannel = static_cast<std::uint8_t>(chan);
                console.printf("io_mux_channel set to %u\n", pins.ioExpander.muxChannel);
                if (applyCallback_) applyCallback_();
            } else {
                console.println(F("Mux channel must be 0-7."));
            }
        }
        return;
    }

    if (token.startsWith("rc")) {
        int index = token.substring(2).toInt();
        if (index >= 1 && index <= static_cast<int>(std::size(ctx_.config->rc.channelPins))) {
            int& slot = ctx_.config->rc.channelPins[index - 1];
            if (valueStr.isEmpty()) {
                console.printf("rc%d = %d\n", index, slot);
            } else {
                slot = valueStr.toInt();
                console.printf("rc%d set to %d\n", index, slot);
                if (applyCallback_) {
                    applyCallback_();
                }
            }
            return;
        }
    }

    console.println(F("Unknown token. Type 'pin help' for the token list."));
}

void runPinWizard() {
    Serial.println(F("[TRACE] runPinWizard"));
    if (!ctx_.config) {
        console.println(F("Config not initialized."));
        return;
    }

    beginWizardSession();

    Config::RuntimeConfig temp = *ctx_.config;
    console.println(F("Pin assignment wizard. Press Enter to keep the current value, or type 'q' to exit early."));

    bool aborted = false;

    do {
        configureChannel("Left Driver Motor A", temp.pins.leftDriver.motorA);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureChannel("Left Driver Motor B", temp.pins.leftDriver.motorB);
        if (wizardAbortRequested_) { aborted = true; break; }
        if (!configureOwnedPinField("Left driver STBY", temp.pins.leftDriver.standby)) { aborted = true; break; }

        configureChannel("Right Driver Motor A", temp.pins.rightDriver.motorA);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureChannel("Right Driver Motor B", temp.pins.rightDriver.motorB);
        if (wizardAbortRequested_) { aborted = true; break; }
        if (!configureOwnedPinField("Right driver STBY", temp.pins.rightDriver.standby)) { aborted = true; break; }

        temp.pins.lightBar = promptInt("Light bar pin", temp.pins.lightBar);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.speaker = promptInt("Speaker pin", temp.pins.speaker);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.batterySense = promptInt("Battery sense pin", temp.pins.batterySense);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.slaveTx = promptInt("Slave link TX pin", temp.pins.slaveTx);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.slaveRx = promptInt("Slave link RX pin", temp.pins.slaveRx);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureIoExpander(temp.pins.ioExpander);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureRcPins(temp.rc);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureLighting(temp.lighting);
        if (wizardAbortRequested_) { aborted = true; break; }
    } while (false);

    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
    }

    if (aborted) {
        console.println(F("Pin wizard exited early. Changes entered so far can still be applied."));
    }

    const bool apply = promptBool(aborted ? "Apply the changes made so far?" : "Apply these changes?", true);
    if (apply) {
        *ctx_.config = temp;
        if (applyCallback_) {
            applyCallback_();
        }
        console.println(F("Pins updated. Run 'save' to persist to flash."));
    } else {
        console.println(F("Pin changes discarded."));
    }

    finishWizardSession();
}

void runFeatureWizard() {
    Serial.println(F("[TRACE] runFeatureWizard"));
    if (!ctx_.config) {
        console.println(F("Config not initialized."));
        return;
    }

    beginWizardSession();

    Config::FeatureConfig features = ctx_.config->features;
    console.println(F("Feature configuration. Press Enter to keep the current setting."));

    features.lightsEnabled = promptBool("Lights enabled", features.lightsEnabled);
    features.soundEnabled = promptBool("Sound enabled", features.soundEnabled);
    features.sensorsEnabled = promptBool("Sensors enabled", features.sensorsEnabled);
    features.wifiEnabled = promptBool("Wi-Fi enabled", features.wifiEnabled);
    features.ultrasonicEnabled = promptBool("Ultrasonic sensors enabled", features.ultrasonicEnabled);
    features.tipOverEnabled = promptBool("Tip-over protection enabled", features.tipOverEnabled);

    const bool apply = promptBool("Apply these changes?", true);
    if (apply) {
        ctx_.config->features = features;
        if (applyCallback_) {
            applyCallback_();
        }
        console.println(F("Feature settings updated. Run 'save' to persist."));
    } else {
        console.println(F("Feature changes discarded."));
    }

    finishWizardSession();
}

void performDrivePulse(float throttle, float turn, unsigned long durationMs, const char* label) {
    if (!ctx_.drive) {
        console.println(F("Drive controller unavailable."));
        return;
    }

    console.println(label);
    Comms::DriveCommand cmd;
    cmd.throttle = throttle;
    cmd.turn = turn;
    const unsigned long end = millis() + durationMs;
    while (millis() < end) {
        ctx_.drive->setCommand(cmd);
        ctx_.drive->update();
        delay(25);
    }
    cmd.throttle = 0.0F;
    cmd.turn = 0.0F;
    ctx_.drive->setCommand(cmd);
    ctx_.drive->update();
}

void runMotorTest() {
    console.println(F("Motor test starting. Tracks will spin forward/back and pivot."));
    performDrivePulse(0.5F, 0.0F, 1500, "Forward");
    performDrivePulse(-0.5F, 0.0F, 1500, "Reverse");
    performDrivePulse(0.0F, 0.6F, 1200, "Pivot right");
    performDrivePulse(0.0F, -0.6F, 1200, "Pivot left");
    console.println(F("Motor test complete."));
}

void runSoundTest() {
    if (!ctx_.sound) {
        console.println(F("Sound controller unavailable."));
        return;
    }
    console.println(F("Pulsing sound output."));
    for (int i = 0; i < 5; ++i) {
        ctx_.sound->update(true);
        delay(150);
        ctx_.sound->update(false);
        delay(150);
    }
    console.println(F("Sound test complete."));
}

void runBatteryTest() {
    if (!ctx_.drive) {
        console.println(F("Drive controller unavailable."));
        return;
    }
    const float voltage = ctx_.drive->readBatteryVoltage();
    console.print(F("Battery voltage: "));
    console.print(voltage, 2);
    console.println(F(" V"));
}

void runTestWizard() {
    beginWizardSession();

    bool done = false;
    while (!done) {
        console.println();
        console.println(F("=== Test Wizard ==="));
        console.println(F("1) Tank drive sweep"));
        console.println(F("2) Sound pulse"));
        console.println(F("3) Battery voltage read"));
        console.println(F("0) Exit test wizard"));
        const int choice = promptInt("Select option", 0);
        switch (choice) {
            case 1:
                runMotorTest();
                break;
            case 2:
                runSoundTest();
                break;
            case 3:
                runBatteryTest();
                break;
            case 0:
                done = true;
                break;
            default:
                console.println(F("Unknown selection."));
                break;
        }
    }

    finishWizardSession();
}

void handleCommand(String line) {
    Serial.print(F("[TRACE] handleCommand: "));
    Serial.println(line);
    line.trim();
    if (line.isEmpty()) {
        showHelp();
        return;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower == "h" || lower == "?" || lower == "quick" || lower == "qr") {
        showHelp();
        return;
    }
    if (lower == "help" || lower == "menu") {
        runHelpMenu();
        return;
    }
    if (lower == "show" || lower == "s") {
        showConfig();
        return;
    }
    if (lower == "wizard pins" || lower == "wp" || lower == "pins") {
        runPinWizard();
        return;
    }
    if (lower == "wizard features" || lower == "wf" || lower == "features") {
        runFeatureWizard();
        return;
    }
    if (lower == "wizard test" || lower == "wt" || lower == "test") {
        runTestWizard();
        return;
    }
    if (lower == "wizard wifi" || lower == "ww" || lower == "wifi") {
        runWifiWizard();
        return;
    }
    if (lower.startsWith("pin")) {
        int space = line.indexOf(' ');
        if (space < 0) {
            console.println(F("Usage: pin <token> [value]. Type 'pin help' for options."));
        } else {
            handlePinCommand(line.substring(space + 1));
        }
        return;
    }
    if (lower == "save" || lower == "sv") {
        if (ctx_.store && ctx_.config && ctx_.store->save(*ctx_.config)) {
            console.println(F("Settings saved."));
            snapshotBaseline();
        } else {
            console.println(F("Failed to save settings."));
        }
        return;
    }
    if (lower == "load" || lower == "ld") {
        if (ctx_.store && ctx_.config) {
            if (ctx_.store->load(*ctx_.config)) {
                console.println(F("Settings loaded."));
            } else {
                console.println(F("Loaded defaults (no saved data)."));
            }
            if (applyCallback_) {
                applyCallback_();
            }
            snapshotBaseline();
        } else {
            console.println(F("Storage unavailable."));
        }
        return;
    }
    if (lower == "defaults" || lower == "df") {
        if (ctx_.config) {
            *ctx_.config = Config::makeDefaultConfig();
            if (applyCallback_) {
                applyCallback_();
            }
            console.println(F("Restored defaults. Run 'save' to persist."));
        } else {
            console.println(F("Config unavailable."));
        }
        return;
    }
    if (lower == "reset" || lower == "rs") {
        if (ctx_.store) {
            ctx_.store->reset();
            console.println(F("Cleared saved settings."));
        }
        return;
    }

    console.print(F("Unknown command: "));
    console.println(line);
    console.println(F("Type 'help' to see available commands."));
}

void processLine(const String& line, ConsoleSource source) {
    Serial.print(F("[TRACE] processLine: "));
    Serial.println(line);
    if (wizardActive_ && source != wizardSource_) {
        console.println(F("Wizard already running on another console. Please wait or exit it before entering new commands."));
        console.printPrompt();
        return;
    }

    String trimmed = line;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        console.printPrompt();
        return;
    }
    activeSource_ = source;
    handleCommand(trimmed);
    console.printPrompt();
}
}  // namespace

void begin(const Context& ctx, ApplyConfigCallback applyCallback) {
    ctx_ = ctx;
    applyCallback_ = applyCallback;
    promptShown_ = true;
    inputBuffer_.clear();
    snapshotBaseline();
    Serial.println(F("[TRACE] UI::begin"));
}

void update() {
    Serial.println(F("[TRACE] UI::update"));
    if (!promptShown_) {
        console.println();
        console.println(F("[TankRC] Serial console ready. Type 'help' for commands."));
        console.printPrompt();
        promptShown_ = true;
    }

    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            if (!lastCharWasCr_) {
                lastCharWasCr_ = true;
                lastCharWasLf_ = false;
                String line = inputBuffer_;
                inputBuffer_.clear();
                processLine(line, ConsoleSource::Serial);
            }
            continue;
        }
        if (c == '\n') {
            if (!lastCharWasLf_) {
                lastCharWasLf_ = true;
                lastCharWasCr_ = false;
                String line = inputBuffer_;
                inputBuffer_.clear();
                processLine(line, ConsoleSource::Serial);
            }
            continue;
        }
        lastCharWasCr_ = false;
        lastCharWasLf_ = false;
        inputBuffer_ += c;
    }
}

bool isWizardActive() {
    return wizardActive_;
}

void addConsoleTap(Print* tap) {
    console.addTap(tap);
}

void removeConsoleTap(Print* tap) {
    console.removeTap(tap);
}

#if TANKRC_ENABLE_NETWORK
void setRemoteConsoleTap(Print* tap) {
    static Print* currentTap = nullptr;
    if (currentTap == tap) {
        return;
    }
    if (currentTap) {
        console.removeTap(currentTap);
    }
    currentTap = tap;
    if (currentTap) {
        console.addTap(currentTap);
    }
}
#endif

void injectRemoteLine(const String& line, ConsoleSource source) {
    if (wizardActive_) {
        if (source == wizardSource_) {
            wizardInputBuffer_ = line;
            wizardInputPending_ = true;
        } else {
            console.println(F("Wizard active on another console. Hold tight or exit it before running more commands."));
            console.printPrompt();
        }
        return;
    }
    processLine(line, source);
}
}  // namespace TankRC::UI
