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
    console.println(F("+=============================================+"));
    console.println(F("| TankRC Serial Console - Quick Reference     |"));
    console.println(F("+=============================================+"));
    printHelpSection(F("Quick Commands"), kHelpQuickCommands);
    printHelpSection(F("Setup Wizards"), kHelpWizards);
    printHelpSection(F("Maintenance"), kHelpMaintenance);
    printHelpSection(F("Help & Tips"), kHelpShortcuts);
    console.println();
    console.println(F("Type 'help' to launch the interactive help hub."));
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

    Config::RuntimeConfig temp = *ctx_.config;
    console.println(F("Pin assignment wizard. Edit individual sections or exit when finished."));

    bool exitRequested = false;
    while (!exitRequested && !wizardAbortRequested_) {
        showPinSummary(temp);
        console.println(F("-----------------------------"));
        console.println(F("1) Edit left driver"));
        console.println(F("2) Edit right driver"));
        console.println(F("3) Edit light/speaker/battery pins"));
        console.println(F("4) Edit slave link TX/RX"));
        console.println(F("5) Edit RC receiver pins"));
        console.println(F("6) Edit lighting config"));
        console.println(F("7) Edit PCF8575 address"));
        console.println(F("8) Finish wizard"));
        console.println(F("0) Exit without finish"));
        const int choice = promptInt("Select option", 8);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                editDriverPins("Left Driver", temp.pins.leftDriver);
                break;
            case 2:
                editDriverPins("Right Driver", temp.pins.rightDriver);
                break;
            case 3:
                editPeripheralPins(temp.pins);
                break;
            case 4:
                editSlaveLinkPins(temp.pins);
                break;
            case 5:
                configureRcPins(temp.rc);
                break;
            case 6:
                configureLighting(temp.lighting);
                break;
            case 7:
                editPcfAddress(temp.pins);
                break;
            case 8:
                exitRequested = true;
                break;
            case 0:
                exitRequested = true;
                break;
            default:
                console.println(F("Invalid selection."));
                break;
        }
    }

    bool aborted = wizardAbortRequested_;
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
    }

    bool apply = false;
    if (!aborted) {
        apply = promptBool("Apply these changes?", true);
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

void handleCommand(String line) {
    line.trim();
    if (line.isEmpty()) {
        showHelp();
        return;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower == "help" || lower == "menu") {
        runHelpMenu();
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
        console.println(F("[TankRC] Serial console ready. Type 'help' for the interactive hub."));
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
