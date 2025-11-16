#include <Arduino.h>
#include <array>
#include <cstdint>
#include <cstdarg>
#include <cctype>
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

void processLine(const String& line, ConsoleSource source);
void beginWizardSession();
void finishWizardSession();
static void snapshotBaseline() {
    if (ctx_.config) {
        baselineConfig_ = *ctx_.config;
        baselineInitialized_ = true;
    }
}

void beginWizardSession() {
    wizardActive_ = true;
    wizardSource_ = activeSource_;
    wizardInputPending_ = false;
    wizardInputBuffer_.clear();
    wizardAbortRequested_ = false;
    inputBuffer_.clear();
}

void finishWizardSession() {
    wizardActive_ = false;
    wizardInputPending_ = false;
    wizardAbortRequested_ = false;
}

void processLine(const String& line, ConsoleSource source);

String readLineBlocking() {
    const ConsoleSource source = wizardActive_ ? wizardSource_ : ConsoleSource::Serial;
    String line;
    while (true) {
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

bool parseIntStrict(const String& text, int& value) {
    if (text.isEmpty()) {
        return false;
    }
    int start = 0;
    if (text[start] == '-' || text[start] == '+') {
        ++start;
    }
    if (start >= text.length()) {
        return false;
    }
    for (int i = start; i < text.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    value = text.toInt();
    return true;
}

String formatPinValue(int pin) {
    if (pin == -1) {
        return String(F("none"));
    }
    const int idx = Config::pcfIndexFromPin(pin);
    if (idx >= 0) {
        return String(F("pcf")) + String(idx);
    }
    return String(pin);
}

bool parsePinValue(const String& text, int& outPin) {
    if (text.isEmpty()) {
        return false;
    }
    String lower = text;
    lower.trim();
    lower.toLowerCase();
    if (lower == "none" || lower == "off") {
        outPin = -1;
        return true;
    }
    if (lower.startsWith("pcf")) {
        String suffix = lower.substring(3);
        suffix.trim();
        int idx = 0;
        if (!parseIntStrict(suffix, idx)) {
            return false;
        }
        if (idx < 0 || idx >= 16) {
            return false;
        }
        outPin = Config::pinFromPcfIndex(idx);
        return true;
    }
    int value = 0;
    if (!parseIntStrict(lower, value)) {
        return false;
    }
    outPin = value;
    return true;
}

int promptPinValue(const String& label, int current) {
    while (true) {
        console.print(label);
        console.print(" [");
        console.print(formatPinValue(current));
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
        int parsed = current;
        if (parsePinValue(line, parsed)) {
            return parsed;
        }
        console.println(F("Invalid pin. Use a GPIO number or pcf# (e.g. pcf3)."));
    }
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
    {F("menu"), F("Open the interactive dashboard")},
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
    {F("menu"), F("Launch the primary dashboard menu")},
    {F("pin help"), F("List pin-token names for the 'pin' command")},
};

void showHelp() {
    console.println();
    console.println(F("+=============================================+"));
    console.println(F("| TankRC Serial Console - Quick Reference     |"));
    console.println(F("+=============================================+"));
    console.println(F("menu       : Open the interactive dashboard"));
    console.println(F("show / s   : Print current runtime configuration"));
    console.println(F("pin ...    : Inspect or change a single pin"));
    console.println(F("wf / wp / ww / wt : Feature, Pin, Wi-Fi, and Test wizards"));
    console.println(F("save / load: Persist or reload settings"));
    console.println(F("defaults / reset: Restore factory defaults or clear flash"));
    console.println(F("help / h   : Quick help, 'pin help' for pin tokens"));
    console.println(F("---------------------------------------------"));
    console.println(F("Try 'menu' for the compact dashboard."));
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
    if (!ctx_.config) {
        console.println(F("No config available."));
        return;
    }

    const auto& pins = ctx_.config->pins;
    const auto& features = ctx_.config->features;
    console.println(F("--- Pin Assignments ---"));
    console.printf("Left Motor A (PWM,IN1,IN2): %d, %s, %s\n",
                  pins.leftDriver.motorA.pwm,
                  formatPinValue(pins.leftDriver.motorA.in1).c_str(),
                  formatPinValue(pins.leftDriver.motorA.in2).c_str());
    console.printf("Left Motor B (PWM,IN1,IN2): %d, %s, %s\n",
                  pins.leftDriver.motorB.pwm,
                  formatPinValue(pins.leftDriver.motorB.in1).c_str(),
                  formatPinValue(pins.leftDriver.motorB.in2).c_str());
    console.printf("Left Driver STBY: %s\n", formatPinValue(pins.leftDriver.standby).c_str());
    console.printf("Right Motor A (PWM,IN1,IN2): %d, %s, %s\n",
                  pins.rightDriver.motorA.pwm,
                  formatPinValue(pins.rightDriver.motorA.in1).c_str(),
                  formatPinValue(pins.rightDriver.motorA.in2).c_str());
    console.printf("Right Motor B (PWM,IN1,IN2): %d, %s, %s\n",
                  pins.rightDriver.motorB.pwm,
                  formatPinValue(pins.rightDriver.motorB.in1).c_str(),
                  formatPinValue(pins.rightDriver.motorB.in2).c_str());
    console.printf("Right Driver STBY: %s\n", formatPinValue(pins.rightDriver.standby).c_str());
    console.printf("Light bar pin: %s\n", formatPinValue(pins.lightBar).c_str());
    console.printf("Speaker pin: %s\n", formatPinValue(pins.speaker).c_str());
    console.printf("Battery sense pin: %s\n", formatPinValue(pins.batterySense).c_str());
    console.printf("Slave link TX/RX: %d / %d\n", pins.slaveTx, pins.slaveRx);
    console.printf("PCF8575 address: %d\n", pins.pcfAddress);

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
    String token;
    const char* description;
    int* value;
    int baseline;
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

    auto add = [&](const char* token, const char* desc, int& ref, const int* basePtr) {
        PinTokenInfo info;
        info.token = token;
        info.description = desc;
        info.value = &ref;
        info.baseline = baselineValue(ref, basePtr);
        tokens.push_back(info);
    };

    const auto& bp = baselineConfig_.pins;
    add("lma_pwm", "Left motor A PWM", pins.leftDriver.motorA.pwm, basePins ? &bp.leftDriver.motorA.pwm : nullptr);
    add("lma_in1", "Left motor A IN1", pins.leftDriver.motorA.in1, basePins ? &bp.leftDriver.motorA.in1 : nullptr);
    add("lma_in2", "Left motor A IN2", pins.leftDriver.motorA.in2, basePins ? &bp.leftDriver.motorA.in2 : nullptr);
    add("lmb_pwm", "Left motor B PWM", pins.leftDriver.motorB.pwm, basePins ? &bp.leftDriver.motorB.pwm : nullptr);
    add("lmb_in1", "Left motor B IN1", pins.leftDriver.motorB.in1, basePins ? &bp.leftDriver.motorB.in1 : nullptr);
    add("lmb_in2", "Left motor B IN2", pins.leftDriver.motorB.in2, basePins ? &bp.leftDriver.motorB.in2 : nullptr);
    add("left_stby", "Left driver STBY", pins.leftDriver.standby, basePins ? &bp.leftDriver.standby : nullptr);

    add("rma_pwm", "Right motor A PWM", pins.rightDriver.motorA.pwm, basePins ? &bp.rightDriver.motorA.pwm : nullptr);
    add("rma_in1", "Right motor A IN1", pins.rightDriver.motorA.in1, basePins ? &bp.rightDriver.motorA.in1 : nullptr);
    add("rma_in2", "Right motor A IN2", pins.rightDriver.motorA.in2, basePins ? &bp.rightDriver.motorA.in2 : nullptr);
    add("rmb_pwm", "Right motor B PWM", pins.rightDriver.motorB.pwm, basePins ? &bp.rightDriver.motorB.pwm : nullptr);
    add("rmb_in1", "Right motor B IN1", pins.rightDriver.motorB.in1, basePins ? &bp.rightDriver.motorB.in1 : nullptr);
    add("rmb_in2", "Right motor B IN2", pins.rightDriver.motorB.in2, basePins ? &bp.rightDriver.motorB.in2 : nullptr);
    add("right_stby", "Right driver STBY", pins.rightDriver.standby, basePins ? &bp.rightDriver.standby : nullptr);

    add("lightbar", "Light bar pin", pins.lightBar, basePins ? &bp.lightBar : nullptr);
    add("speaker", "Speaker pin", pins.speaker, basePins ? &bp.speaker : nullptr);
    add("battery", "Battery sense pin", pins.batterySense, basePins ? &bp.batterySense : nullptr);
    add("slave_tx", "Slave link TX pin", pins.slaveTx, basePins ? &bp.slaveTx : nullptr);
    add("slave_rx", "Slave link RX pin", pins.slaveRx, basePins ? &bp.slaveRx : nullptr);
    add("pcf_addr", "PCF8575 I2C address", pins.pcfAddress, basePins ? &bp.pcfAddress : nullptr);

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
        const String value = formatPinValue(*t.value);
        console.printf("%-10s = %-8s (%s)\n", t.token.c_str(), value.c_str(), t.description);
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
        if (*t.value != t.baseline) {
            if (!any) {
                console.println(F("--- Pin diffs since last save ---"));
                any = true;
            }
            const String fromVal = formatPinValue(t.baseline);
            const String toVal = formatPinValue(*t.value);
            console.printf("%-10s: %s -> %s\n", t.token.c_str(), fromVal.c_str(), toVal.c_str());
        }
    }
    if (!any) {
        console.println(F("No pin changes since last save."));
    }
}

void configureChannel(const char* label, Config::ChannelPins& pins) {
    console.println(label);
    pins.pwm = promptInt("  PWM", pins.pwm);
    if (wizardAbortRequested_) return;
    pins.in1 = promptPinValue("  IN1", pins.in1);
    if (wizardAbortRequested_) return;
    pins.in2 = promptPinValue("  IN2", pins.in2);
}

void editDriverPins(const char* label, Config::DriverPins& pins) {
    String base = label;
    configureChannel((base + " Motor A").c_str(), pins.motorA);
    if (wizardAbortRequested_) return;
    configureChannel((base + " Motor B").c_str(), pins.motorB);
    if (wizardAbortRequested_) return;
    pins.standby = promptPinValue(base + " STBY", pins.standby);
}

void editPeripheralPins(Config::PinAssignments& pins) {
    pins.lightBar = promptPinValue("Light bar pin", pins.lightBar);
    if (wizardAbortRequested_) return;
    pins.speaker = promptPinValue("Speaker pin", pins.speaker);
    if (wizardAbortRequested_) return;
    pins.batterySense = promptPinValue("Battery sense pin", pins.batterySense);
}

void editSlaveLinkPins(Config::PinAssignments& pins) {
    pins.slaveTx = promptInt("Slave link TX pin", pins.slaveTx);
    if (wizardAbortRequested_) return;
    pins.slaveRx = promptInt("Slave link RX pin", pins.slaveRx);
}

void editPcfAddress(Config::PinAssignments& pins) {
    pins.pcfAddress = promptInt("PCF8575 I2C address (decimal)", pins.pcfAddress);
}

void editPcaAddress(Config::LightingConfig& lighting) {
    int addr = promptInt("PCA9685 I2C address (decimal, 64 = 0x40)", lighting.pcaAddress);
    if (addr >= 0 && addr <= 127) {
        lighting.pcaAddress = static_cast<std::uint8_t>(addr);
    }
}

void showPinSummary(const Config::RuntimeConfig& config) {
    const auto& pins = config.pins;
    console.println(F("--- Pin Summary ---"));
    auto printChannel = [&](const char* name, const Config::ChannelPins& ch) {
        String in1 = formatPinValue(ch.in1);
        String in2 = formatPinValue(ch.in2);
        console.printf("%s PWM:%d IN1:%s IN2:%s\n", name, ch.pwm, in1.c_str(), in2.c_str());
    };
    printChannel("Left A", pins.leftDriver.motorA);
    printChannel("Left B", pins.leftDriver.motorB);
    printChannel("Right A", pins.rightDriver.motorA);
    printChannel("Right B", pins.rightDriver.motorB);
    String leftStby = formatPinValue(pins.leftDriver.standby);
    String rightStby = formatPinValue(pins.rightDriver.standby);
    console.printf("Left STBY:%s | Right STBY:%s\n", leftStby.c_str(), rightStby.c_str());
    String light = formatPinValue(pins.lightBar);
    String speaker = formatPinValue(pins.speaker);
    String battery = formatPinValue(pins.batterySense);
    console.printf("Light:%s Speaker:%s Battery:%s\n", light.c_str(), speaker.c_str(), battery.c_str());
    console.printf("Slave TX/RX: %d/%d | PCF addr: %d\n", pins.slaveTx, pins.slaveRx, pins.pcfAddress);
}

enum class WizardValueType {
    Int,
    Uint8,
    Uint16,
};

struct PinWizardBinding {
    String token;
    String label;
    WizardValueType valueType;
    void* valuePtr;
    int baselineValue;
    bool allowPcf;
};

int clampToRange(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int readBindingValue(const PinWizardBinding& binding) {
    switch (binding.valueType) {
        case WizardValueType::Int:
            return *static_cast<int*>(binding.valuePtr);
        case WizardValueType::Uint8:
            return static_cast<int>(*static_cast<std::uint8_t*>(binding.valuePtr));
        case WizardValueType::Uint16:
            return static_cast<int>(*static_cast<std::uint16_t*>(binding.valuePtr));
    }
    return 0;
}

void writeBindingValue(const PinWizardBinding& binding, int value) {
    switch (binding.valueType) {
        case WizardValueType::Int:
            *static_cast<int*>(binding.valuePtr) = value;
            break;
        case WizardValueType::Uint8: {
            int clamped = clampToRange(value, 0, 255);
            *static_cast<std::uint8_t*>(binding.valuePtr) = static_cast<std::uint8_t>(clamped);
            break;
        }
        case WizardValueType::Uint16: {
            int clamped = clampToRange(value, 0, 65535);
            *static_cast<std::uint16_t*>(binding.valuePtr) = static_cast<std::uint16_t>(clamped);
            break;
        }
    }
}

std::vector<PinWizardBinding> buildPinWizardBindings(Config::RuntimeConfig& working, const Config::RuntimeConfig& baseline) {
    std::vector<PinWizardBinding> bindings;
    bindings.reserve(48);
    auto addInt = [&](const String& token, const String& label, int& current, int baselineValue, bool allowPcf) {
        PinWizardBinding binding;
        binding.token = token;
        binding.label = label;
        binding.valueType = WizardValueType::Int;
        binding.valuePtr = &current;
        binding.baselineValue = baselineValue;
        binding.allowPcf = allowPcf;
        bindings.push_back(binding);
    };
    auto addUint8 = [&](const String& token, const String& label, std::uint8_t& current, std::uint8_t baselineValue) {
        PinWizardBinding binding;
        binding.token = token;
        binding.label = label;
        binding.valueType = WizardValueType::Uint8;
        binding.valuePtr = &current;
        binding.baselineValue = static_cast<int>(baselineValue);
        binding.allowPcf = false;
        bindings.push_back(binding);
    };
    auto addUint16 = [&](const String& token, const String& label, std::uint16_t& current, std::uint16_t baselineValue) {
        PinWizardBinding binding;
        binding.token = token;
        binding.label = label;
        binding.valueType = WizardValueType::Uint16;
        binding.valuePtr = &current;
        binding.baselineValue = static_cast<int>(baselineValue);
        binding.allowPcf = false;
        bindings.push_back(binding);
    };

    auto& pins = working.pins;
    const auto& basePins = baseline.pins;
    addInt("lma_pwm", "Left motor A PWM", pins.leftDriver.motorA.pwm, basePins.leftDriver.motorA.pwm, false);
    addInt("lma_in1", "Left motor A IN1", pins.leftDriver.motorA.in1, basePins.leftDriver.motorA.in1, true);
    addInt("lma_in2", "Left motor A IN2", pins.leftDriver.motorA.in2, basePins.leftDriver.motorA.in2, true);
    addInt("lmb_pwm", "Left motor B PWM", pins.leftDriver.motorB.pwm, basePins.leftDriver.motorB.pwm, false);
    addInt("lmb_in1", "Left motor B IN1", pins.leftDriver.motorB.in1, basePins.leftDriver.motorB.in1, true);
    addInt("lmb_in2", "Left motor B IN2", pins.leftDriver.motorB.in2, basePins.leftDriver.motorB.in2, true);
    addInt("left_stby", "Left driver STBY", pins.leftDriver.standby, basePins.leftDriver.standby, true);

    addInt("rma_pwm", "Right motor A PWM", pins.rightDriver.motorA.pwm, basePins.rightDriver.motorA.pwm, false);
    addInt("rma_in1", "Right motor A IN1", pins.rightDriver.motorA.in1, basePins.rightDriver.motorA.in1, true);
    addInt("rma_in2", "Right motor A IN2", pins.rightDriver.motorA.in2, basePins.rightDriver.motorA.in2, true);
    addInt("rmb_pwm", "Right motor B PWM", pins.rightDriver.motorB.pwm, basePins.rightDriver.motorB.pwm, false);
    addInt("rmb_in1", "Right motor B IN1", pins.rightDriver.motorB.in1, basePins.rightDriver.motorB.in1, true);
    addInt("rmb_in2", "Right motor B IN2", pins.rightDriver.motorB.in2, basePins.rightDriver.motorB.in2, true);
    addInt("right_stby", "Right driver STBY", pins.rightDriver.standby, basePins.rightDriver.standby, true);

    addInt("lightbar", "Light bar", pins.lightBar, basePins.lightBar, true);
    addInt("speaker", "Speaker", pins.speaker, basePins.speaker, true);
    addInt("battery", "Battery sense", pins.batterySense, basePins.batterySense, true);
    addInt("slave_tx", "Slave link TX", pins.slaveTx, basePins.slaveTx, false);
    addInt("slave_rx", "Slave link RX", pins.slaveRx, basePins.slaveRx, false);
    addInt("pcf_addr", "PCF8575 address", pins.pcfAddress, basePins.pcfAddress, false);

    auto& lighting = working.lighting;
    const auto& baseLighting = baseline.lighting;
    addUint8("pca_addr", "PCA9685 address", lighting.pcaAddress, baseLighting.pcaAddress);
    addUint16("pca_freq", "PCA9685 PWM frequency", lighting.pwmFrequency, baseLighting.pwmFrequency);
    addInt("fl_r", "Front left red", lighting.channels.frontLeft.r, baseLighting.channels.frontLeft.r, false);
    addInt("fl_g", "Front left green", lighting.channels.frontLeft.g, baseLighting.channels.frontLeft.g, false);
    addInt("fl_b", "Front left blue", lighting.channels.frontLeft.b, baseLighting.channels.frontLeft.b, false);
    addInt("fr_r", "Front right red", lighting.channels.frontRight.r, baseLighting.channels.frontRight.r, false);
    addInt("fr_g", "Front right green", lighting.channels.frontRight.g, baseLighting.channels.frontRight.g, false);
    addInt("fr_b", "Front right blue", lighting.channels.frontRight.b, baseLighting.channels.frontRight.b, false);
    addInt("rl_r", "Rear left red", lighting.channels.rearLeft.r, baseLighting.channels.rearLeft.r, false);
    addInt("rl_g", "Rear left green", lighting.channels.rearLeft.g, baseLighting.channels.rearLeft.g, false);
    addInt("rl_b", "Rear left blue", lighting.channels.rearLeft.b, baseLighting.channels.rearLeft.b, false);
    addInt("rr_r", "Rear right red", lighting.channels.rearRight.r, baseLighting.channels.rearRight.r, false);
    addInt("rr_g", "Rear right green", lighting.channels.rearRight.g, baseLighting.channels.rearRight.g, false);
    addInt("rr_b", "Rear right blue", lighting.channels.rearRight.b, baseLighting.channels.rearRight.b, false);

    auto& rc = working.rc;
    const auto& baseRc = baseline.rc;
    for (std::size_t i = 0; i < std::size(rc.channelPins); ++i) {
        String token = "rc" + String(static_cast<unsigned>(i + 1));
        String label = "RC channel " + String(static_cast<unsigned>(i + 1));
        addInt(token, label, rc.channelPins[i], baseRc.channelPins[i], false);
    }

    return bindings;
}

const PinWizardBinding* findPinWizardBinding(const std::vector<PinWizardBinding>& bindings, const String& token) {
    for (const auto& binding : bindings) {
        if (binding.token == token) {
            return &binding;
        }
    }
    return nullptr;
}

std::size_t countPendingPinChanges(const std::vector<PinWizardBinding>& bindings) {
    std::size_t count = 0;
    for (const auto& binding : bindings) {
        if (readBindingValue(binding) != binding.baselineValue) {
            ++count;
        }
    }
    return count;
}

bool tryHandleQuickPinEdit(const String& input, std::vector<PinWizardBinding>& bindings) {
    String trimmed = input;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return false;
    }

    int eq = trimmed.indexOf('=');
    int space = trimmed.indexOf(' ');
    int split = -1;
    if (eq >= 0 && (space < 0 || eq < space)) {
        split = eq;
    } else if (space >= 0) {
        split = space;
    }
    if (split <= 0) {
        return false;
    }

    String token = trimmed.substring(0, split);
    String value = trimmed.substring(split + 1);
    token.trim();
    value.trim();
    if (token.isEmpty() || value.isEmpty()) {
        return false;
    }
    token.toLowerCase();

    const PinWizardBinding* binding = findPinWizardBinding(bindings, token);
    if (!binding) {
        console.printf("Unknown pin token '%s'. Type '?' for the command list.\n", token.c_str());
        return true;
    }

    int parsed = 0;
    bool ok = binding->allowPcf ? parsePinValue(value, parsed) : parseIntStrict(value, parsed);
    if (!ok) {
        console.println(F("Invalid value. Use GPIO numbers or pcf# (e.g. pcf3)."));
        return true;
    }

    if (readBindingValue(*binding) == parsed) {
        console.println(F("Value unchanged."));
        return true;
    }

    writeBindingValue(*binding, parsed);
    const int stored = readBindingValue(*binding);
    const String display = binding->allowPcf ? formatPinValue(stored) : String(stored);
    console.printf("%s set to %s\n", binding->label.c_str(), display.c_str());
    return true;
}

bool tryShowPinBindingValue(const String& input, const std::vector<PinWizardBinding>& bindings) {
    String token = input;
    token.trim();
    if (token.isEmpty()) {
        return false;
    }
    if (token.indexOf(' ') >= 0 || token.indexOf('=') >= 0) {
        return false;
    }
    token.toLowerCase();
    const PinWizardBinding* binding = findPinWizardBinding(bindings, token);
    if (!binding) {
        return false;
    }
    const int value = readBindingValue(*binding);
    const String display = binding->allowPcf ? formatPinValue(value) : String(value);
    console.printf("%s [%s] = %s\n", binding->label.c_str(), binding->token.c_str(), display.c_str());
    return true;
}

void printSessionPinDiff(const std::vector<PinWizardBinding>& bindings) {
    bool any = false;
    for (const auto& binding : bindings) {
        if (readBindingValue(binding) == binding.baselineValue) {
            continue;
        }
        if (!any) {
            console.println(F("--- Pending pin changes ---"));
            any = true;
        }
        const String before = binding.allowPcf ? formatPinValue(binding.baselineValue) : String(binding.baselineValue);
        const int value = readBindingValue(binding);
        const String after = binding.allowPcf ? formatPinValue(value) : String(value);
        console.printf("%-18s: %s -> %s\n", binding.label.c_str(), before.c_str(), after.c_str());
    }
    if (!any) {
        console.println(F("No pending pin changes."));
    }
}

bool isLightingTokenName(const String& token) {
    static const char* const kTokens[] = {
        "pca_addr", "pca_freq", "fl_r", "fl_g", "fl_b", "fr_r", "fr_g", "fr_b",
        "rl_r",     "rl_g",     "rl_b", "rr_r", "rr_g", "rr_b",
    };
    for (const char* name : kTokens) {
        if (token == name) {
            return true;
        }
    }
    return false;
}

void renderDriverChannelRow(const char* name,
                            const Config::ChannelPins& channel,
                            const char* pwmToken,
                            const char* in1Token,
                            const char* in2Token);
void renderPinField(const char* label, const char* token, int value, bool allowPcf);

void renderIoPortExpanderSummary(const Config::RuntimeConfig& config) {
    console.println();
    console.println(F(" IO Port Expander (PCA9685 lighting board)"));
    console.printf("  Address: 0x%02X [pca_addr]   PWM freq: %u Hz [pca_freq]\n",
                   config.lighting.pcaAddress,
                   config.lighting.pwmFrequency);
    auto printRgb = [](const char* name,
                       const Config::RgbChannel& rgb,
                       const char* rToken,
                       const char* gToken,
                       const char* bToken) {
        console.printf("  %-10s R:%-3d [%s]  G:%-3d [%s]  B:%-3d [%s]\n",
                       name,
                       rgb.r,
                       rToken,
                       rgb.g,
                       gToken,
                       rgb.b,
                       bToken);
    };
    printRgb("Front L", config.lighting.channels.frontLeft, "fl_r", "fl_g", "fl_b");
    printRgb("Front R", config.lighting.channels.frontRight, "fr_r", "fr_g", "fr_b");
    printRgb("Rear L", config.lighting.channels.rearLeft, "rl_r", "rl_g", "rl_b");
    printRgb("Rear R", config.lighting.channels.rearRight, "rr_r", "rr_g", "rr_b");

    std::array<String, 16> channelUsage;
    for (auto& entry : channelUsage) {
        entry = F("unused");
    }
    auto tagChannel = [&](int channel, const char* label, const char* token) {
        if (channel < 0 || channel >= static_cast<int>(channelUsage.size())) {
            return;
        }
        channelUsage[channel] = String(label) + " [" + token + "]";
    };
    tagChannel(config.lighting.channels.frontLeft.r, "Front L R", "fl_r");
    tagChannel(config.lighting.channels.frontLeft.g, "Front L G", "fl_g");
    tagChannel(config.lighting.channels.frontLeft.b, "Front L B", "fl_b");
    tagChannel(config.lighting.channels.frontRight.r, "Front R R", "fr_r");
    tagChannel(config.lighting.channels.frontRight.g, "Front R G", "fr_g");
    tagChannel(config.lighting.channels.frontRight.b, "Front R B", "fr_b");
    tagChannel(config.lighting.channels.rearLeft.r, "Rear L R", "rl_r");
    tagChannel(config.lighting.channels.rearLeft.g, "Rear L G", "rl_g");
    tagChannel(config.lighting.channels.rearLeft.b, "Rear L B", "rl_b");
    tagChannel(config.lighting.channels.rearRight.r, "Rear R R", "rr_r");
    tagChannel(config.lighting.channels.rearRight.g, "Rear R G", "rr_g");
    tagChannel(config.lighting.channels.rearRight.b, "Rear R B", "rr_b");

    console.println(F("  Channel map:"));
    for (int row = 0; row < 16; row += 4) {
        console.print(F("   "));
        for (int col = 0; col < 4; ++col) {
            int channel = row + col;
            console.printf("CH%02d: %-20s", channel, channelUsage[channel].c_str());
            if (col < 3) {
                console.print(F("  "));
            }
        }
        console.println();
    }
}

void renderPcfExpanderSummary(const Config::RuntimeConfig& config, const std::vector<PinWizardBinding>& bindings) {
    console.println();
    console.println(F(" PCF8575 IO Expander"));
    console.printf("  Address: 0x%02X [pcf_addr]\n", config.pins.pcfAddress);
    std::array<String, 16> usage;
    for (auto& entry : usage) {
        entry = F("unused");
    }
    auto appendUsage = [&](int idx, const PinWizardBinding& binding) {
        if (idx < 0 || idx >= static_cast<int>(usage.size())) {
            return;
        }
        String entry = binding.label + " [" + binding.token + "]";
        if (usage[idx] == F("unused")) {
            usage[idx] = entry;
        } else {
            usage[idx] += F(", ");
            usage[idx] += entry;
        }
    };
    for (const auto& binding : bindings) {
        if (!binding.allowPcf) {
            continue;
        }
        const int idx = Config::pcfIndexFromPin(readBindingValue(binding));
        if (idx >= 0) {
            appendUsage(idx, binding);
        }
    }
    console.println(F("  Channel map:"));
    for (int row = 0; row < 16; row += 4) {
        console.print(F("   "));
        for (int col = 0; col < 4; ++col) {
            const int idx = row + col;
            console.printf("PCF%02d: %-32s", idx, usage[idx].c_str());
            if (col < 3) {
                console.print(F("  "));
            }
        }
        console.println();
    }
}

bool isDriverTokenName(const String& token) {
    static const char* const tokens[] = {
        "lma_pwm", "lma_in1", "lma_in2", "lmb_pwm", "lmb_in1", "lmb_in2", "left_stby",
        "rma_pwm", "rma_in1", "rma_in2", "rmb_pwm", "rmb_in1", "rmb_in2", "right_stby",
    };
    for (const char* name : tokens) {
        if (token == name) {
            return true;
        }
    }
    return false;
}

void renderDriverExpanderSummary(const Config::RuntimeConfig& config) {
    console.println();
    console.println(F(" Drive modules (left/right)"));
    renderDriverChannelRow("Left Motor A", config.pins.leftDriver.motorA, "lma_pwm", "lma_in1", "lma_in2");
    renderDriverChannelRow("Left Motor B", config.pins.leftDriver.motorB, "lmb_pwm", "lmb_in1", "lmb_in2");
    renderPinField("Left STBY", "left_stby", config.pins.leftDriver.standby, true);
    console.println();
    renderDriverChannelRow("Right Motor A", config.pins.rightDriver.motorA, "rma_pwm", "rma_in1", "rma_in2");
    renderDriverChannelRow("Right Motor B", config.pins.rightDriver.motorB, "rmb_pwm", "rmb_in1", "rmb_in2");
    renderPinField("Right STBY", "right_stby", config.pins.rightDriver.standby, true);
}

void renderDriverChannelRow(const char* name,
                            const Config::ChannelPins& channel,
                            const char* pwmToken,
                            const char* in1Token,
                            const char* in2Token) {
    String in1 = formatPinValue(channel.in1);
    String in2 = formatPinValue(channel.in2);
    console.printf("  %-12s PWM:%-4d [%s]  IN1:%-7s [%s]  IN2:%-7s [%s]\n",
                   name,
                   channel.pwm,
                   pwmToken,
                   in1.c_str(),
                   in1Token,
                   in2.c_str(),
                   in2Token);
}

void renderPinField(const char* label, const char* token, int value, bool allowPcf) {
    String display = allowPcf ? formatPinValue(value) : String(value);
    console.printf("  %-18s %-8s", label, display.c_str());
    if (token && token[0]) {
        console.printf(" [%s]", token);
    }
    console.println();
}

void renderPinWizardDashboard(const Config::RuntimeConfig& config, const std::vector<PinWizardBinding>& bindings) {
    const auto& pins = config.pins;
    console.println();
    console.println(F("==============================================================================="));
    console.println(F("                          Pin Assignment Dashboard"));
    console.println(F("==============================================================================="));
    console.println(F(" Drivers"));
    renderDriverChannelRow("Left Motor A", pins.leftDriver.motorA, "lma_pwm", "lma_in1", "lma_in2");
    renderDriverChannelRow("Left Motor B", pins.leftDriver.motorB, "lmb_pwm", "lmb_in1", "lmb_in2");
    renderPinField("Left STBY", "left_stby", pins.leftDriver.standby, true);
    console.println();
    renderDriverChannelRow("Right Motor A", pins.rightDriver.motorA, "rma_pwm", "rma_in1", "rma_in2");
    renderDriverChannelRow("Right Motor B", pins.rightDriver.motorB, "rmb_pwm", "rmb_in1", "rmb_in2");
    renderPinField("Right STBY", "right_stby", pins.rightDriver.standby, true);

    console.println();
    console.println(F(" Peripherals & Links"));
    renderPinField("Light bar", "lightbar", pins.lightBar, true);
    renderPinField("Speaker", "speaker", pins.speaker, true);
    renderPinField("Battery sense", "battery", pins.batterySense, true);
    console.printf("  Slave TX: %-4d [slave_tx]   Slave RX: %-4d [slave_rx]\n", pins.slaveTx, pins.slaveRx);
    console.printf("  PCF8575 addr: %d [pcf_addr]\n", pins.pcfAddress);

    console.println();
    console.println(F(" RC Receiver (PWM inputs)"));
    for (std::size_t i = 0; i < std::size(config.rc.channelPins); ++i) {
        console.printf("  CH%u: %-4d [rc%u]\n",
                       static_cast<unsigned>(i + 1),
                       config.rc.channelPins[i],
                       static_cast<unsigned>(i + 1));
    }

    renderIoPortExpanderSummary(config);
    renderPcfExpanderSummary(config, bindings);

    const std::size_t pending = countPendingPinChanges(bindings);
    console.println();
    console.printf("Pending changes: %u (type 'diff' to list details)\n", static_cast<unsigned>(pending));
    console.println(F("Quick edit: <token>=<value> or <token> <value>  (e.g. lma_pwm=32, rc1 15)"));
    console.println(F("Number keys open grouped editors:"));
    console.println(F(" 1) Drive modules (left/right)   3) Lights/Speaker/Battery"));
    console.println(F(" 4) Slave link       5) RC receiver pins   6) PWM IO port expander"));
    console.println(F(" 8) PCF8575 IO expander  9) Finish wizard"));
    console.println(F(" Type 0/exit to cancel without applying. '?' for help."));
    console.println(F(" Tip: fl_*/fr_*/rl_*/rr_* tokens edit color channels without opening option 6 (PWM IO port expander)."));
    console.println(F(" Hint: Option 6 accepts 'addr' to change PCA9685 address; option 8 shows PCF8575 usage."));
}

void configureLighting(Config::LightingConfig& lighting);
void printIoPortTokenHelp() {
    console.println(F("IO port tokens:"));
    console.println(F("  pca_addr, pca_freq"));
    console.println(F("  fl_r, fl_g, fl_b  (front left RGB)"));
    console.println(F("  fr_r, fr_g, fr_b  (front right RGB)"));
    console.println(F("  rl_r, rl_g, rl_b  (rear left RGB)"));
    console.println(F("  rr_r, rr_g, rr_b  (rear right RGB)"));
    console.println(F("Use '<token>=<value>' for one-shot edits or type a token for guided input."));
    console.println(F("Type 'addr' to change the PCA9685 address or 'advanced' to run the legacy lighting wizard (blink settings)."));
}

void editIoPortExpander(Config::RuntimeConfig& config, std::vector<PinWizardBinding>& bindings) {
    console.println(F("PWM IO port expander editor. Type '?' for token help, 'done' to return."));
    bool exitRequested = false;
    while (!exitRequested && !wizardAbortRequested_) {
        renderIoPortExpanderSummary(config);
        console.print(F("IO command (? for help, done to exit): "));
        String line = readLineBlocking();
        if (wizardAbortRequested_) {
            break;
        }
        line.trim();
        if (line.isEmpty()) {
            continue;
        }
        String lower = line;
        lower.toLowerCase();

        if (lower == "done" || lower == "exit" || lower == "back" || lower == "finish") {
            exitRequested = true;
            break;
        }
        if (lower == "diff") {
            printSessionPinDiff(bindings);
            continue;
        }
        if (lower == "?" || lower == "help" || lower == "tokens") {
            printIoPortTokenHelp();
            continue;
        }
        if (lower == "addr" || lower == "address" || lower == "pca addr") {
            editPcaAddress(config.lighting);
            continue;
        }
        if (lower == "advanced" || lower == "legacy" || lower == "full") {
            configureLighting(config.lighting);
            continue;
        }

        int eq = line.indexOf('=');
        int space = line.indexOf(' ');
        int split = -1;
        if (eq >= 0 && (space < 0 || eq < space)) {
            split = eq;
        } else if (space >= 0) {
            split = space;
        }

        if (split > 0) {
            String token = line.substring(0, split);
            token.trim();
            token.toLowerCase();
            String valueStr = line.substring(split + 1);
            valueStr.trim();
            if (token.isEmpty() || valueStr.isEmpty()) {
                console.println(F("Use <token>=<value> format or just type the token name."));
                continue;
            }
            if (!isLightingTokenName(token)) {
                console.println(F("Token not part of the IO expander. Try pca_addr or fl_*/fr_*/rl_*/rr_*."));
                continue;
            }
            const PinWizardBinding* binding = findPinWizardBinding(bindings, token);
            if (!binding) {
                console.println(F("Token unavailable in this session."));
                continue;
            }
            int parsed = 0;
            bool ok = binding->allowPcf ? parsePinValue(valueStr, parsed) : parseIntStrict(valueStr, parsed);
            if (!ok) {
                console.println(F("Invalid value. Use decimal channel numbers (0-15) or -1 to disable."));
                continue;
            }
            if (readBindingValue(*binding) == parsed) {
                console.println(F("Value unchanged."));
                continue;
            }
            writeBindingValue(*binding, parsed);
            console.printf("%s updated to %d\n", binding->label.c_str(), parsed);
            continue;
        }

        if (!isLightingTokenName(lower)) {
            console.println(F("Unknown token. Type '?' to see valid IO port tokens."));
            continue;
        }

        const PinWizardBinding* binding = findPinWizardBinding(bindings, lower);
        if (!binding) {
            console.println(F("Token unavailable in this session."));
            continue;
        }
        const int current = readBindingValue(*binding);
        const int updated = promptInt(binding->label, current);
        if (wizardAbortRequested_) {
            break;
        }
        if (updated == current) {
            console.println(F("Value unchanged."));
            continue;
        }
        writeBindingValue(*binding, updated);
        console.println(F("Value updated."));
    }

    if (!wizardAbortRequested_) {
        console.println(F("Leaving PWM IO port expander editor."));
    }
}

void printDriverTokenHelp() {
    console.println(F("Driver tokens:"));
    console.println(F("  lma_pwm,lma_in1,lma_in2,lmb_pwm,lmb_in1,lmb_in2,left_stby"));
    console.println(F("  rma_pwm,rma_in1,rma_in2,rmb_pwm,rmb_in1,rmb_in2,right_stby"));
    console.println(F("Use '<token>=<value>' (pcf#/GPIO) or type the token to inspect it."));
    console.println(F("Type 'left' or 'right' to open the guided forms for that driver."));
}

void editDriverExpander(Config::RuntimeConfig& config, std::vector<PinWizardBinding>& bindings) {
    console.println(F("Drive module editor. Type '?' for token help, 'done' to return."));
    bool exitRequested = false;
    while (!exitRequested && !wizardAbortRequested_) {
        renderDriverExpanderSummary(config);
        console.print(F("Drive command (? for help, done to exit): "));
        String line = readLineBlocking();
        if (wizardAbortRequested_) {
            break;
        }
        line.trim();
        if (line.isEmpty()) {
            continue;
        }
        String lower = line;
        lower.toLowerCase();

        if (lower == "done" || lower == "exit" || lower == "back" || lower == "finish") {
            exitRequested = true;
            break;
        }
        if (lower == "diff") {
            printSessionPinDiff(bindings);
            continue;
        }
        if (lower == "?" || lower == "help" || lower == "tokens") {
            printDriverTokenHelp();
            continue;
        }
        if (lower == "left" || lower == "left driver") {
            editDriverPins("Left Driver", config.pins.leftDriver);
            continue;
        }
        if (lower == "right" || lower == "right driver") {
            editDriverPins("Right Driver", config.pins.rightDriver);
            continue;
        }

        int eq = line.indexOf('=');
        int space = line.indexOf(' ');
        int split = -1;
        if (eq >= 0 && (space < 0 || eq < space)) {
            split = eq;
        } else if (space >= 0) {
            split = space;
        }

        if (split > 0) {
            String token = line.substring(0, split);
            token.trim();
            token.toLowerCase();
            if (!isDriverTokenName(token)) {
                console.println(F("Token not part of the drive modules (try lma_*/rma_* etc)."));
                continue;
            }
            if (tryHandleQuickPinEdit(line, bindings)) {
                continue;
            }
        } else if (isDriverTokenName(lower)) {
            if (tryShowPinBindingValue(lower, bindings)) {
                continue;
            }
        } else {
            console.println(F("Unknown command. Try '<token>=value', 'left', 'right', or '?'."));
            continue;
        }

    }

    if (!wizardAbortRequested_) {
        console.println(F("Leaving drive module editor."));
    }
}

void printPcfTokenHelp(const std::vector<PinWizardBinding>& bindings) {
    console.println(F("PCF8575-capable tokens:"));
    for (const auto& binding : bindings) {
        if (!binding.allowPcf) {
            continue;
        }
        console.printf("  %-12s -> %s\n", binding.token.c_str(), binding.label.c_str());
    }
    console.println(F("Use '<token>=pcf#' or '<token> none' to reassign a pin."));
    console.println(F("Type 'pcf#' (e.g. pcf3) to inspect a specific channel."));
    console.println(F("Type 'addr' to change the expander I2C address."));
}

void printPcfChannelDetails(int index, const std::vector<PinWizardBinding>& bindings) {
    console.printf("PCF%02d assignments:\n", index);
    bool any = false;
    for (const auto& binding : bindings) {
        if (!binding.allowPcf) {
            continue;
        }
        if (Config::pcfIndexFromPin(readBindingValue(binding)) == index) {
            console.printf("  %-18s [%s]\n", binding.label.c_str(), binding.token.c_str());
            any = true;
        }
    }
    if (!any) {
        console.println(F("  (unused)"));
    }
}

bool tryHandlePcfTokenEdit(const String& input, std::vector<PinWizardBinding>& bindings) {
    String trimmed = input;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return false;
    }
    int eq = trimmed.indexOf('=');
    int space = trimmed.indexOf(' ');
    int split = -1;
    if (eq >= 0 && (space < 0 || eq < space)) {
        split = eq;
    } else if (space >= 0) {
        split = space;
    }
    if (split <= 0) {
        return false;
    }
    String token = trimmed.substring(0, split);
    String value = trimmed.substring(split + 1);
    token.trim();
    value.trim();
    if (token.isEmpty() || value.isEmpty()) {
        console.println(F("Use <token>=pcf# format (e.g. lma_in1=pcf3)."));
        return true;
    }
    token.toLowerCase();

    const PinWizardBinding* binding = findPinWizardBinding(bindings, token);
    if (!binding) {
        console.printf("Unknown token '%s'. Type '?' for the token list.\n", token.c_str());
        return true;
    }
    if (!binding->allowPcf) {
        console.println(F("That token cannot be assigned to the PCF8575 expander."));
        return true;
    }
    int parsed = readBindingValue(*binding);
    if (!parsePinValue(value, parsed)) {
        console.println(F("Invalid value. Use GPIO numbers, 'pcf#', or 'none'."));
        return true;
    }
    if (readBindingValue(*binding) == parsed) {
        console.println(F("Value unchanged."));
        return true;
    }
    writeBindingValue(*binding, parsed);
    console.printf("%s [%s] = %s\n",
                   binding->label.c_str(),
                   binding->token.c_str(),
                   formatPinValue(parsed).c_str());
    return true;
}

void editPcfExpander(Config::RuntimeConfig& config, std::vector<PinWizardBinding>& bindings) {
    console.println(F("PCF8575 IO expander editor. Type '?' for token help, 'done' to return."));
    bool exitRequested = false;
    while (!exitRequested && !wizardAbortRequested_) {
        renderPcfExpanderSummary(config, bindings);
        console.print(F("PCF command (? for help, done to exit): "));
        String line = readLineBlocking();
        if (wizardAbortRequested_) {
            break;
        }
        line.trim();
        if (line.isEmpty()) {
            continue;
        }
        String lower = line;
        lower.toLowerCase();

        if (lower == "done" || lower == "exit" || lower == "back" || lower == "finish") {
            exitRequested = true;
            break;
        }
        if (lower == "diff") {
            printSessionPinDiff(bindings);
            continue;
        }
        if (lower == "?" || lower == "help" || lower == "tokens") {
            printPcfTokenHelp(bindings);
            continue;
        }
        if (lower == "addr" || lower == "address") {
            editPcfAddress(config.pins);
            continue;
        }
        if (lower.startsWith("pcf")) {
            String suffix = lower.substring(3);
            suffix.trim();
            if (suffix.isEmpty()) {
                console.println(F("Specify a channel number 0-15 (e.g. pcf4)."));
                continue;
            }
            int idx = suffix.toInt();
            if (idx >= 0 && idx < 16) {
                printPcfChannelDetails(idx, bindings);
            } else {
                console.println(F("Channel must be between 0 and 15."));
            }
            continue;
        }
        if (tryHandlePcfTokenEdit(line, bindings)) {
            continue;
        }
        if (tryShowPinBindingValue(line, bindings)) {
            continue;
        }
        console.println(F("Unknown command. Try '<token>=pcf#' or type '?' for help."));
    }

    if (!wizardAbortRequested_) {
        console.println(F("Leaving PCF8575 IO expander editor."));
    }
}

void printPinWizardHelp() {
    console.println(F("Pin wizard controls:"));
    console.println(F("  - Type <token>=<value> for quick edits (pcf# for expander pins)."));
    console.println(F("  - Type a token alone to see its current value."));
    console.println(F("  - Press menu numbers to open grouped editors (1-8)."));
    console.println(F("  - Option 1 opens the drive module editor for both left/right motors."));
    console.println(F("  - PCA9685 tokens: pca_addr, pca_freq, fl_*/fr_*/rl_*/rr_* for color channels."));
    console.println(F("  - Option 6 opens the PWM IO port expander; type 'addr' for PCA address or 'advanced' for blink settings."));
    console.println(F("  - Option 8 opens the PCF8575 IO expander; use 'addr' to change its I2C address."));
    console.println(F("  - 'diff' lists changes relative to when the wizard opened."));
    console.println(F("  - '9' or 'done' finishes and prompts to apply, '0'/'exit' discards changes."));
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

void runWifiWizard() {
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
        console.println(F("  pcf_addr (PCF8575 I2C address)"));
        console.println(F("  rc1,rc2,rc3,rc4,rc5,rc6"));
        console.println(F("  Use values like 'pcf3' or 'none' for expander pins"));
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
    struct Binding {
        const char* name;
        int* ptr;
        bool allowPcf;
    };
    Binding bindings[] = {
        {"lma_pwm", &pins.leftDriver.motorA.pwm, false},
        {"lma_in1", &pins.leftDriver.motorA.in1, true},
        {"lma_in2", &pins.leftDriver.motorA.in2, true},
        {"lmb_pwm", &pins.leftDriver.motorB.pwm, false},
        {"lmb_in1", &pins.leftDriver.motorB.in1, true},
        {"lmb_in2", &pins.leftDriver.motorB.in2, true},
        {"rma_pwm", &pins.rightDriver.motorA.pwm, false},
        {"rma_in1", &pins.rightDriver.motorA.in1, true},
        {"rma_in2", &pins.rightDriver.motorA.in2, true},
        {"rmb_pwm", &pins.rightDriver.motorB.pwm, false},
        {"rmb_in1", &pins.rightDriver.motorB.in1, true},
        {"rmb_in2", &pins.rightDriver.motorB.in2, true},
        {"left_stby", &pins.leftDriver.standby, true},
        {"right_stby", &pins.rightDriver.standby, true},
        {"lightbar", &pins.lightBar, true},
        {"speaker", &pins.speaker, true},
        {"battery", &pins.batterySense, true},
        {"slave_tx", &pins.slaveTx, false},
        {"slave_rx", &pins.slaveRx, false},
        {"pcf_addr", &pins.pcfAddress, false},
    };

    for (const auto& binding : bindings) {
        if (token == binding.name) {
            if (valueStr.isEmpty()) {
                const String value = binding.allowPcf ? formatPinValue(*binding.ptr) : String(*binding.ptr);
                console.printf("%s = %s\n", binding.name, value.c_str());
            } else {
                int parsed = 0;
                bool ok = binding.allowPcf ? parsePinValue(valueStr, parsed) : parseIntStrict(valueStr, parsed);
                if (!ok) {
                    console.println(F("Invalid pin value."));
                    return;
                }
                *binding.ptr = parsed;
                const String value = binding.allowPcf ? formatPinValue(parsed) : String(parsed);
                console.printf("%s set to %s\n", binding.name, value.c_str());
                if (applyCallback_) {
                    applyCallback_();
                }
            }
            return;
        }
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
    if (!ctx_.config) {
        console.println(F("Config not initialized."));
        return;
    }

    beginWizardSession();

    Config::RuntimeConfig baseline = *ctx_.config;
    Config::RuntimeConfig temp = baseline;
    auto bindings = buildPinWizardBindings(temp, baseline);

    console.println(F("Pin assignment wizard. Interactive dashboard + quick-edit tokens."));
    console.println(F("Type '?' for help, 'done' when finished, or '0' to cancel."));

    bool finishRequested = false;
    bool discardRequested = false;
    while (!finishRequested && !discardRequested && !wizardAbortRequested_) {
        renderPinWizardDashboard(temp, bindings);
        console.print(F("Command (? for help): "));
        String line = readLineBlocking();
        if (wizardAbortRequested_) {
            break;
        }
        line.trim();
        if (line.isEmpty()) {
            continue;
        }

        String lower = line;
        lower.toLowerCase();

        if (lower == "?" || lower == "help") {
            printPinWizardHelp();
            continue;
        }
        if (lower == "diff") {
            printSessionPinDiff(bindings);
            continue;
        }
        if (lower == "done" || lower == "finish" || lower == "apply" || lower == "9") {
            finishRequested = true;
            break;
        }
        if (lower == "0" || lower == "exit" || lower == "cancel" || lower == "quit" || lower == "q") {
            discardRequested = true;
            break;
        }

        bool sectionHandled = false;
        if (lower == "1" || lower == "2" || lower == "drive" || lower == "drives" ||
            lower == "left driver" || lower == "right driver" || lower == "left" || lower == "right") {
            editDriverExpander(temp, bindings);
            sectionHandled = true;
        } else if (lower == "3" || lower == "peripheral" || lower == "peripherals" || lower == "lights") {
            editPeripheralPins(temp.pins);
            sectionHandled = true;
        } else if (lower == "4" || lower == "slave" || lower == "link") {
            editSlaveLinkPins(temp.pins);
            sectionHandled = true;
        } else if (lower == "5" || lower == "rc" || lower == "receiver") {
            configureRcPins(temp.rc);
            sectionHandled = true;
        } else if (lower == "6" || lower == "lighting" || lower == "io" || lower == "expander" || lower == "io port") {
            editIoPortExpander(temp, bindings);
            sectionHandled = true;
        } else if (lower == "8" || lower == "pcf" || lower == "pcf io" || lower == "pcf expander") {
            editPcfExpander(temp, bindings);
            sectionHandled = true;
        }

        if (wizardAbortRequested_) {
            break;
        }
        if (sectionHandled) {
            continue;
        }

        if (tryHandleQuickPinEdit(line, bindings)) {
            continue;
        }
        if (tryShowPinBindingValue(line, bindings)) {
            continue;
        }

        console.println(F("Unknown command. Use '?' for help or <token>=<value> to edit a pin."));
    }

    bool aborted = wizardAbortRequested_;
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
    }

    if (aborted) {
        console.println(F("Pin wizard cancelled."));
        finishWizardSession();
        return;
    }
    if (discardRequested) {
        console.println(F("Pin changes discarded."));
        finishWizardSession();
        return;
    }
    if (!finishRequested) {
        console.println(F("Pin changes discarded."));
        finishWizardSession();
        return;
    }

    const bool apply = promptBool("Apply these changes?", true);
    if (wizardAbortRequested_) {
        console.println(F("Pin wizard cancelled."));
        wizardAbortRequested_ = false;
        finishWizardSession();
        return;
    }
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

bool saveConfigToStore() {
    if (ctx_.store && ctx_.config && ctx_.store->save(*ctx_.config)) {
        console.println(F("Settings saved."));
        snapshotBaseline();
        return true;
    }
    console.println(F("Failed to save settings."));
    return false;
}

bool loadConfigFromStore() {
    if (!ctx_.store || !ctx_.config) {
        console.println(F("Storage unavailable."));
        return false;
    }
    if (ctx_.store->load(*ctx_.config)) {
        console.println(F("Settings loaded."));
    } else {
        console.println(F("Loaded defaults (no saved data)."));
    }
    if (applyCallback_) {
        applyCallback_();
    }
    snapshotBaseline();
    return true;
}

bool restoreDefaultConfig() {
    if (!ctx_.config) {
        console.println(F("Config unavailable."));
        return false;
    }
    *ctx_.config = Config::makeDefaultConfig();
    if (applyCallback_) {
        applyCallback_();
    }
    console.println(F("Restored defaults. Run 'save' to persist."));
    return true;
}

bool resetStoredConfig() {
    if (!ctx_.store) {
        console.println(F("Storage unavailable."));
        return false;
    }
    ctx_.store->reset();
    console.println(F("Cleared saved settings."));
    return true;
}

void runStorageMenu() {
    beginWizardSession();
    bool exit = false;
    while (!exit && !wizardAbortRequested_) {
        console.println();
        console.println(F("=== Storage & Defaults ==="));
        console.println(F("1) Save current settings"));
        console.println(F("2) Load saved settings"));
        console.println(F("3) Restore factory defaults"));
        console.println(F("4) Clear saved data"));
        console.println(F("0) Back"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                saveConfigToStore();
                break;
            case 2:
                loadConfigFromStore();
                break;
            case 3:
                restoreDefaultConfig();
                break;
            case 4:
                resetStoredConfig();
                break;
            case 0:
                exit = true;
                break;
            default:
                console.println(F("Unknown selection."));
                break;
        }
    }
    finishWizardSession();
}

void runHardwareMenu() {
    beginWizardSession();
    bool exit = false;
    while (!exit && !wizardAbortRequested_) {
        console.println();
        console.println(F("=== Hardware & Pins ==="));
        console.println(F("1) View pin summary"));
        console.println(F("2) Edit pin mapping"));
        console.println(F("0) Back"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                if (ctx_.config) {
                    showPinSummary(*ctx_.config);
                } else {
                    console.println(F("Config unavailable."));
                }
                break;
            case 2:
                finishWizardSession();
                runPinWizard();
                beginWizardSession();
                break;
            case 0:
                exit = true;
                break;
            default:
                console.println(F("Unknown selection."));
                break;
        }
    }
    finishWizardSession();
}

void runConfigMenu() {
    beginWizardSession();
    bool exit = false;
    while (!exit && !wizardAbortRequested_) {
        console.println();
        console.println(F("=== Config & Network ==="));
        console.println(F("1) View configuration"));
        console.println(F("2) Feature toggles"));
        console.println(F("3) Wi-Fi / network"));
        console.println(F("0) Back"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                showConfig();
                break;
            case 2:
                finishWizardSession();
                runFeatureWizard();
                beginWizardSession();
                break;
            case 3:
                finishWizardSession();
                runWifiWizard();
                beginWizardSession();
                break;
            case 0:
                exit = true;
                break;
            default:
                console.println(F("Unknown selection."));
                break;
        }
    }
    finishWizardSession();
}

void runDiagMenu() {
    beginWizardSession();
    bool exit = false;
    while (!exit && !wizardAbortRequested_) {
        console.println();
        console.println(F("=== Diagnostics & Tests ==="));
        console.println(F("1) Run test wizard"));
        console.println(F("0) Back"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                finishWizardSession();
                runTestWizard();
                beginWizardSession();
                break;
            case 0:
                exit = true;
                break;
            default:
                console.println(F("Unknown selection."));
                break;
        }
    }
    finishWizardSession();
}

void runMainMenu() {
    beginWizardSession();
    bool done = false;
    while (!done && !wizardAbortRequested_) {
        console.println();
        console.println(F("===== TankRC Console ====="));
        console.println(F("1) Hardware & pins"));
        console.println(F("2) Config & network"));
        console.println(F("3) Diagnostics"));
        console.println(F("4) Storage & maintenance"));
        console.println(F("0) Exit"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                finishWizardSession();
                runHardwareMenu();
                beginWizardSession();
                break;
            case 2:
                finishWizardSession();
                runConfigMenu();
                beginWizardSession();
                break;
            case 3:
                finishWizardSession();
                runDiagMenu();
                beginWizardSession();
                break;
            case 4:
                finishWizardSession();
                runStorageMenu();
                beginWizardSession();
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
    line.trim();
    if (line.isEmpty()) {
        runMainMenu();
        return;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower == "help") {
        runHelpMenu();
        return;
    }
    if (lower == "menu" || lower == "main") {
        runMainMenu();
        return;
    }
    if (lower == "h" || lower == "?") {
        showHelp();
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
        saveConfigToStore();
        return;
    }
    if (lower == "load" || lower == "ld") {
        loadConfigFromStore();
        return;
    }
    if (lower == "defaults" || lower == "df") {
        restoreDefaultConfig();
        return;
    }
    if (lower == "reset" || lower == "rs") {
        resetStoredConfig();
        return;
    }

    console.print(F("Unknown command: "));
    console.println(line);
    console.println(F("Type 'help' to see available commands."));
}

void processLine(const String& line, ConsoleSource source) {
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
    promptShown_ = false;
    inputBuffer_.clear();
    snapshotBaseline();
}

void update() {
    if (!promptShown_) {
        console.println();
        console.println(F("[TankRC] Serial console ready. Type 'menu' for the dashboard or 'help' for shortcuts."));
        console.printPrompt();
        promptShown_ = true;
    }

    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            String line = inputBuffer_;
            inputBuffer_.clear();
            processLine(line, ConsoleSource::Serial);
        } else {
            inputBuffer_ += c;
        }
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
