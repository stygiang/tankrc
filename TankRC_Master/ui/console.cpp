#include <Arduino.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>

#include "comms/radio_link.h"
#include "control/drive_controller.h"
#include "features/sound_fx.h"
#include "config/runtime_config.h"
#include "storage/config_store.h"
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

void processLine(const String& line, ConsoleSource source);
void beginWizardSession();
void finishWizardSession();

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
        console.print(F("Please type y/n: "));
    }
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
        if (wizardAbortRequested_) {
            break;
        }
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

void showHelp() {
    console.println();
    console.println(F("=== TankRC Console Shortcuts ==="));
    console.println(F("menu    : Open the feature/test dashboard"));
    console.println(F("features: Toggle lights, sound, Wifi, sensors"));
    console.println(F("tests   : Run motor/sound/battery diagnostics"));
    console.println(F("save    : Persist current settings"));
    console.println(F("load    : Reload saved settings"));
    console.println(F("defaults: Restore factory defaults"));
    console.println(F("reset   : Clear saved flash storage"));
}

void runMainMenu() {
    beginWizardSession();
    bool exit = false;
    while (!exit && !wizardAbortRequested_) {
        console.println();
        console.println(F("===== TankRC Console ====="));
        console.println(F("1) Feature toggles"));
        console.println(F("2) Diagnostics & tests"));
        console.println(F("0) Exit"));
        const int choice = promptInt("Select option", 0);
        if (wizardAbortRequested_) {
            break;
        }
        switch (choice) {
            case 1:
                finishWizardSession();
                runFeatureWizard();
                beginWizardSession();
                break;
            case 2:
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

void handleCommand(String line) {
    line.trim();
    if (line.isEmpty()) {
        runMainMenu();
        return;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower == "help" || lower == "h" || lower == "?") {
        showHelp();
        return;
    }
    if (lower == "menu" || lower == "main") {
        runMainMenu();
        return;
    }
    if (lower == "features" || lower == "wf") {
        runFeatureWizard();
        return;
    }
    if (lower == "tests" || lower == "wt") {
        runTestWizard();
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

    console.println(F("Unknown command. Type 'help' for shortcuts."));
}

void processLine(const String& line, ConsoleSource source) {
    if (wizardActive_ && source != wizardSource_) {
        console.println(F("Wizard already running on another console. Exit it before sending new commands."));
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
