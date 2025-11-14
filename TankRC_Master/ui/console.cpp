#include <Arduino.h>
#include <cstdint>
#include <iterator>

#include "comms/radio_link.h"
#include "control/drive_controller.h"
#include "drivers/rc_receiver.h"
#include "features/lighting.h"
#include "features/sound_fx.h"
#include "ui/console.h"

namespace TankRC::UI {
namespace {
Context ctx_{};
ApplyConfigCallback applyCallback_ = nullptr;
String inputBuffer_;
bool promptShown_ = false;
bool wizardActive_ = false;
bool wizardAbortRequested_ = false;

String readLineBlocking() {
    String line;
    while (true) {
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
        delay(10);
    }
}

int promptInt(const String& label, int current) {
    Serial.print(label);
    Serial.print(" [");
    Serial.print(current);
    Serial.print("] : ");
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
    Serial.print(label);
    Serial.print(" [");
    Serial.print(current ? "Y" : "N");
    Serial.print("] : ");

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
        Serial.print("Please type y/n: ");
    }
}

String promptString(const String& label, const String& current, size_t maxLen) {
    Serial.print(label);
    Serial.print(" [");
    Serial.print(current);
    Serial.print("] : ");
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

void showHelp() {
    Serial.println();
    Serial.println(F("=== TankRC Serial Console ==="));
    Serial.println(F("Commands:"));
    Serial.println(F("  help (h, ?)     - Show this list"));
    Serial.println(F("  show (s)        - Dump current configuration"));
    Serial.println(F("  wizard pins (wp)- Interactive pin assignment wizard"));
    Serial.println(F("  wizard features (wf) - Enable/disable feature modules"));
    Serial.println(F("  wizard test (wt)- Launch interactive test suite"));
    Serial.println(F("  pin <token> [value] - Show/update a single pin (type 'pin help' for tokens)"));
    Serial.println(F("  wizard wifi (ww) - Configure Wi-Fi credentials / AP settings"));
    Serial.println(F("  save (sv)       - Persist current settings to flash"));
    Serial.println(F("  load (ld)       - Reload last saved settings"));
    Serial.println(F("  defaults (df)   - Restore factory defaults"));
    Serial.println(F("  reset (rs)      - Clear saved settings from flash"));
    Serial.println();
}

void showConfig() {
    if (!ctx_.config) {
        Serial.println(F("No config available."));
        return;
    }

    const auto& pins = ctx_.config->pins;
    const auto& features = ctx_.config->features;
    Serial.println(F("--- Pin Assignments ---"));
    Serial.printf("Left Motor A (PWM,IN1,IN2): %d, %d, %d\n", pins.leftDriver.motorA.pwm, pins.leftDriver.motorA.in1, pins.leftDriver.motorA.in2);
    Serial.printf("Left Motor B (PWM,IN1,IN2): %d, %d, %d\n", pins.leftDriver.motorB.pwm, pins.leftDriver.motorB.in1, pins.leftDriver.motorB.in2);
    Serial.printf("Left Driver STBY: %d\n", pins.leftDriver.standby);
    Serial.printf("Right Motor A (PWM,IN1,IN2): %d, %d, %d\n", pins.rightDriver.motorA.pwm, pins.rightDriver.motorA.in1, pins.rightDriver.motorA.in2);
    Serial.printf("Right Motor B (PWM,IN1,IN2): %d, %d, %d\n", pins.rightDriver.motorB.pwm, pins.rightDriver.motorB.in1, pins.rightDriver.motorB.in2);
    Serial.printf("Right Driver STBY: %d\n", pins.rightDriver.standby);
    Serial.printf("Light bar pin: %d\n", pins.lightBar);
    Serial.printf("Speaker pin: %d\n", pins.speaker);
    Serial.printf("Battery sense pin: %d\n", pins.batterySense);

    Serial.println(F("--- RC Receiver Pins ---"));
    for (std::size_t i = 0; i < Drivers::RcReceiver::kChannelCount; ++i) {
        Serial.printf("CH%u pin: %d\n", static_cast<unsigned>(i + 1), ctx_.config->rc.channelPins[i]);
    }

    Serial.println(F("--- Lighting ---"));
    Serial.printf("PCA9685 addr: 0x%02X, freq: %u Hz\n", ctx_.config->lighting.pcaAddress, ctx_.config->lighting.pwmFrequency);
    auto printRgb = [](const char* name, const Config::RgbChannel& rgb) {
        Serial.printf("%s -> R:%d G:%d B:%d\n", name, rgb.r, rgb.g, rgb.b);
    };
    printRgb("Front Left", ctx_.config->lighting.channels.frontLeft);
    printRgb("Front Right", ctx_.config->lighting.channels.frontRight);
    printRgb("Rear Left", ctx_.config->lighting.channels.rearLeft);
    printRgb("Rear Right", ctx_.config->lighting.channels.rearRight);
    Serial.printf("Blink wifi:%s rc:%s bt:%s period:%ums\n",
                  ctx_.config->lighting.blink.wifi ? "on" : "off",
                  ctx_.config->lighting.blink.rc ? "on" : "off",
                  ctx_.config->lighting.blink.bt ? "on" : "off",
                  ctx_.config->lighting.blink.periodMs);

    Serial.println(F("--- Feature Flags ---"));
    Serial.printf("Lighting enabled: %s\n", features.lightingEnabled ? "yes" : "no");
    Serial.printf("Sound enabled: %s\n", features.soundEnabled ? "yes" : "no");
    Serial.printf("Sensors enabled: %s\n", features.sensorsEnabled ? "yes" : "no");
    Serial.println();
}

void configureChannel(const char* label, Config::ChannelPins& pins) {
    Serial.println(label);
    pins.pwm = promptInt("  PWM", pins.pwm);
    if (wizardAbortRequested_) return;
    pins.in1 = promptInt("  IN1", pins.in1);
    if (wizardAbortRequested_) return;
    pins.in2 = promptInt("  IN2", pins.in2);
}

void configureRgbChannel(const char* label, Config::RgbChannel& rgb) {
    Serial.println(label);
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

    Serial.println(F("RC receiver pins:"));
    for (std::size_t i = 0; i < Drivers::RcReceiver::kChannelCount; ++i) {
        rc.channelPins[i] = promptInt(labels[i], rc.channelPins[i]);
        if (wizardAbortRequested_) {
            break;
        }
    }
}

void configureLighting(Config::LightingConfig& lighting) {
    Serial.println(F("PCA9685 lighting setup:"));
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
    lighting.blink.bt = promptBool("Blink when Bluetooth disconnected", lighting.blink.bt);
    if (wizardAbortRequested_) return;
    lighting.blink.periodMs = static_cast<std::uint16_t>(promptInt("Blink period (ms)", lighting.blink.periodMs));
}

void runWifiWizard() {
    if (!ctx_.config) {
        Serial.println(F("Config not initialized."));
        return;
    }

    wizardActive_ = true;
    inputBuffer_.clear();

    Config::WifiConfig wifi = ctx_.config->wifi;
    auto toString = [](const char* data) { return (data && data[0]) ? String(data) : String(); };

    Serial.println(F("Wi-Fi configuration (leave blank to keep current value, or type 'q' to exit)."));
    const String staSsid = promptString("Station SSID", toString(wifi.ssid), sizeof(wifi.ssid));
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
        Serial.println(F("Wi-Fi wizard cancelled."));
        wizardActive_ = false;
        return;
    }
    const String staPass = promptString("Station Password", wifi.password[0] ? "[hidden]" : "", sizeof(wifi.password));
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
        Serial.println(F("Wi-Fi wizard cancelled."));
        wizardActive_ = false;
        return;
    }
    const String apSsid = promptString("Access Point SSID", toString(wifi.apSsid), sizeof(wifi.apSsid));
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
        Serial.println(F("Wi-Fi wizard cancelled."));
        wizardActive_ = false;
        return;
    }
    const String apPass = promptString("Access Point Password", wifi.apPassword[0] ? "[hidden]" : "", sizeof(wifi.apPassword));
    if (wizardAbortRequested_) {
        wizardAbortRequested_ = false;
        Serial.println(F("Wi-Fi wizard cancelled."));
        wizardActive_ = false;
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
        Serial.println(F("Wi-Fi settings updated. Device may restart networking."));
    } else {
        Serial.println(F("Wi-Fi changes discarded."));
    }

    wizardActive_ = false;
}

void handlePinCommand(const String& args) {
    if (!ctx_.config) {
        Serial.println(F("Config not initialized."));
        return;
    }
    String trimmed = args;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        Serial.println(F("Usage: pin <token> [value]. Type 'pin help' for the token list."));
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
        Serial.println(F("Tokens:"));
        Serial.println(F("  lma_pwm,lma_in1,lma_in2"));
        Serial.println(F("  lmb_pwm,lmb_in1,lmb_in2"));
        Serial.println(F("  rma_pwm,rma_in1,rma_in2"));
        Serial.println(F("  rmb_pwm,rmb_in1,rmb_in2"));
        Serial.println(F("  left_stby,right_stby,lightbar,speaker,battery"));
        Serial.println(F("  rc1,rc2,rc3,rc4,rc5,rc6"));
        return;
    }

    auto& pins = ctx_.config->pins;
    struct Binding {
        const char* name;
        int* ptr;
    };
    Binding bindings[] = {
        {"lma_pwm", &pins.leftDriver.motorA.pwm},
        {"lma_in1", &pins.leftDriver.motorA.in1},
        {"lma_in2", &pins.leftDriver.motorA.in2},
        {"lmb_pwm", &pins.leftDriver.motorB.pwm},
        {"lmb_in1", &pins.leftDriver.motorB.in1},
        {"lmb_in2", &pins.leftDriver.motorB.in2},
        {"rma_pwm", &pins.rightDriver.motorA.pwm},
        {"rma_in1", &pins.rightDriver.motorA.in1},
        {"rma_in2", &pins.rightDriver.motorA.in2},
        {"rmb_pwm", &pins.rightDriver.motorB.pwm},
        {"rmb_in1", &pins.rightDriver.motorB.in1},
        {"rmb_in2", &pins.rightDriver.motorB.in2},
        {"left_stby", &pins.leftDriver.standby},
        {"right_stby", &pins.rightDriver.standby},
        {"lightbar", &pins.lightBar},
        {"speaker", &pins.speaker},
        {"battery", &pins.batterySense},
    };

    for (const auto& binding : bindings) {
        if (token == binding.name) {
            if (valueStr.isEmpty()) {
                Serial.printf("%s = %d\n", binding.name, *binding.ptr);
            } else {
                const int value = valueStr.toInt();
                *binding.ptr = value;
                Serial.printf("%s set to %d\n", binding.name, value);
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
                Serial.printf("rc%d = %d\n", index, slot);
            } else {
                slot = valueStr.toInt();
                Serial.printf("rc%d set to %d\n", index, slot);
                if (applyCallback_) {
                    applyCallback_();
                }
            }
            return;
        }
    }

    Serial.println(F("Unknown token. Type 'pin help' for the token list."));
}

void runPinWizard() {
    if (!ctx_.config) {
        Serial.println(F("Config not initialized."));
        return;
    }

    wizardActive_ = true;
    inputBuffer_.clear();

    Config::RuntimeConfig temp = *ctx_.config;
    wizardAbortRequested_ = false;
    Serial.println(F("Pin assignment wizard. Press Enter to keep the current value, or type 'q' to exit early."));

    bool aborted = false;

    do {
        configureChannel("Left Driver Motor A", temp.pins.leftDriver.motorA);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureChannel("Left Driver Motor B", temp.pins.leftDriver.motorB);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.leftDriver.standby = promptInt("Left driver STBY", temp.pins.leftDriver.standby);
        if (wizardAbortRequested_) { aborted = true; break; }

        configureChannel("Right Driver Motor A", temp.pins.rightDriver.motorA);
        if (wizardAbortRequested_) { aborted = true; break; }
        configureChannel("Right Driver Motor B", temp.pins.rightDriver.motorB);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.rightDriver.standby = promptInt("Right driver STBY", temp.pins.rightDriver.standby);
        if (wizardAbortRequested_) { aborted = true; break; }

        temp.pins.lightBar = promptInt("Light bar pin", temp.pins.lightBar);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.speaker = promptInt("Speaker pin", temp.pins.speaker);
        if (wizardAbortRequested_) { aborted = true; break; }
        temp.pins.batterySense = promptInt("Battery sense pin", temp.pins.batterySense);
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
        Serial.println(F("Pin wizard exited early. Changes entered so far can still be applied."));
    }

    const bool apply = promptBool(aborted ? "Apply the changes made so far?" : "Apply these changes?", true);
    if (apply) {
        *ctx_.config = temp;
        if (applyCallback_) {
            applyCallback_();
        }
        Serial.println(F("Pins updated. Run 'save' to persist to flash."));
    } else {
        Serial.println(F("Pin changes discarded."));
    }

    wizardActive_ = false;
}

void runFeatureWizard() {
    if (!ctx_.config) {
        Serial.println(F("Config not initialized."));
        return;
    }

    wizardActive_ = true;
    inputBuffer_.clear();

    Config::FeatureConfig features = ctx_.config->features;
    Serial.println(F("Feature configuration. Press Enter to keep the current setting."));

    features.lightingEnabled = promptBool("Lighting enabled", features.lightingEnabled);
    features.soundEnabled = promptBool("Sound enabled", features.soundEnabled);
    features.sensorsEnabled = promptBool("Sensors enabled", features.sensorsEnabled);

    const bool apply = promptBool("Apply these changes?", true);
    if (apply) {
        ctx_.config->features = features;
        if (applyCallback_) {
            applyCallback_();
        }
        Serial.println(F("Feature settings updated. Run 'save' to persist."));
    } else {
        Serial.println(F("Feature changes discarded."));
    }

    wizardActive_ = false;
}

void performDrivePulse(float throttle, float turn, unsigned long durationMs, const char* label) {
    if (!ctx_.drive) {
        Serial.println(F("Drive controller unavailable."));
        return;
    }

    Serial.println(label);
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
    Serial.println(F("Motor test starting. Tracks will spin forward/back and pivot."));
    performDrivePulse(0.5F, 0.0F, 1500, "Forward");
    performDrivePulse(-0.5F, 0.0F, 1500, "Reverse");
    performDrivePulse(0.0F, 0.6F, 1200, "Pivot right");
    performDrivePulse(0.0F, -0.6F, 1200, "Pivot left");
    Serial.println(F("Motor test complete."));
}

void runLightingTest() {
    if (!ctx_.lighting) {
        Serial.println(F("Lighting controller unavailable."));
        return;
    }
    Serial.println(F("Blinking light bar (6 cycles)."));
    Features::LightingInput input{};
    input.wifiConnected = true;
    input.rcConnected = true;
    input.btConnected = true;
    input.ultrasonicLeft = 0.4F;
    input.ultrasonicRight = 0.9F;
    for (int i = 0; i < 6; ++i) {
        input.steering = 0.8F;
        ctx_.lighting->update(input);
        delay(200);
        input.steering = -0.8F;
        ctx_.lighting->update(input);
        delay(200);
    }
    Serial.println(F("Lighting test complete."));
}

void runSoundTest() {
    if (!ctx_.sound) {
        Serial.println(F("Sound controller unavailable."));
        return;
    }
    Serial.println(F("Pulsing sound output."));
    for (int i = 0; i < 5; ++i) {
        ctx_.sound->update(true);
        delay(150);
        ctx_.sound->update(false);
        delay(150);
    }
    Serial.println(F("Sound test complete."));
}

void runBatteryTest() {
    if (!ctx_.drive) {
        Serial.println(F("Drive controller unavailable."));
        return;
    }
    const float voltage = ctx_.drive->readBatteryVoltage();
    Serial.print(F("Battery voltage: "));
    Serial.print(voltage, 2);
    Serial.println(F(" V"));
}

void runTestWizard() {
    wizardActive_ = true;
    inputBuffer_.clear();

    bool done = false;
    while (!done) {
        Serial.println();
        Serial.println(F("=== Test Wizard ==="));
        Serial.println(F("1) Tank drive sweep"));
        Serial.println(F("2) Lighting blink"));
        Serial.println(F("3) Sound pulse"));
        Serial.println(F("4) Battery voltage read"));
        Serial.println(F("0) Exit test wizard"));
        const int choice = promptInt("Select option", 0);
        switch (choice) {
            case 1:
                runMotorTest();
                break;
            case 2:
                runLightingTest();
                break;
            case 3:
                runSoundTest();
                break;
            case 4:
                runBatteryTest();
                break;
            case 0:
                done = true;
                break;
            default:
                Serial.println(F("Unknown selection."));
                break;
        }
    }

    wizardActive_ = false;
}

void handleCommand(String line) {
    line.trim();
    if (line.isEmpty()) {
        showHelp();
        return;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower == "help" || lower == "menu" || lower == "h" || lower == "?") {
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
            Serial.println(F("Usage: pin <token> [value]. Type 'pin help' for options."));
        } else {
            handlePinCommand(line.substring(space + 1));
        }
        return;
    }
    if (lower == "save" || lower == "sv") {
        if (ctx_.store && ctx_.config && ctx_.store->save(*ctx_.config)) {
            Serial.println(F("Settings saved."));
        } else {
            Serial.println(F("Failed to save settings."));
        }
        return;
    }
    if (lower == "load" || lower == "ld") {
        if (ctx_.store && ctx_.config) {
            if (ctx_.store->load(*ctx_.config)) {
                Serial.println(F("Settings loaded."));
            } else {
                Serial.println(F("Loaded defaults (no saved data)."));
            }
            if (applyCallback_) {
                applyCallback_();
            }
        } else {
            Serial.println(F("Storage unavailable."));
        }
        return;
    }
    if (lower == "defaults" || lower == "df") {
        if (ctx_.config) {
            *ctx_.config = Config::makeDefaultConfig();
            if (applyCallback_) {
                applyCallback_();
            }
            Serial.println(F("Restored defaults. Run 'save' to persist."));
        } else {
            Serial.println(F("Config unavailable."));
        }
        return;
    }
    if (lower == "reset" || lower == "rs") {
        if (ctx_.store) {
            ctx_.store->reset();
            Serial.println(F("Cleared saved settings."));
        }
        return;
    }

    Serial.print(F("Unknown command: "));
    Serial.println(line);
    Serial.println(F("Type 'help' to see available commands."));
}
}  // namespace

void begin(const Context& ctx, ApplyConfigCallback applyCallback) {
    ctx_ = ctx;
    applyCallback_ = applyCallback;
    promptShown_ = false;
    inputBuffer_.clear();
}

void update() {
    if (!promptShown_) {
        Serial.println();
        Serial.println(F("[TankRC] Serial console ready. Type 'help' for commands."));
        Serial.print(F("> "));
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
            handleCommand(line);
            Serial.print(F("> "));
        } else {
            inputBuffer_ += c;
        }
    }
}

bool isWizardActive() {
    return wizardActive_;
}
}  // namespace TankRC::UI
