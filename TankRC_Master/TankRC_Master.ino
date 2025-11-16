#ifndef TANKRC_BUILD_MASTER
#define TANKRC_BUILD_MASTER 1
#endif

#include "config/features.h"

#ifndef TANKRC_ENABLE_NETWORK
#define TANKRC_ENABLE_NETWORK FEATURE_WIFI
#endif

#ifndef TANKRC_USE_DRIVE_PROXY
#define TANKRC_USE_DRIVE_PROXY 1
#endif

#include <algorithm>

#include "TankRC.h"

#if TANKRC_ENABLE_NETWORK
#include "network/remote_console.h"
#endif

using namespace TankRC;

static Control::DriveController driveController;
#if FEATURE_SOUND
static Features::SoundFx sound;
#endif
static Comms::RadioLink radio;
static Config::RuntimeConfig runtimeConfig = Config::makeDefaultConfig();
static Storage::ConfigStore configStore;
#if TANKRC_ENABLE_NETWORK
static Network::WifiManager wifiManager;
static Network::ControlServer controlServer;
static Network::RemoteConsole remoteConsole;
static bool wifiInitialized = false;
static bool networkActive = false;
static Time::NtpClock ntpClock;
static Logging::SessionLogger sessionLogger;
static unsigned long lastLogMs = 0UL;
#else
static unsigned long lastLogMs = 0UL;
#endif

struct Task {
    void (*fn)();
    std::uint32_t intervalMs;
    std::uint32_t lastRunMs;
};

void taskReadInputs();
void taskControl();
void taskOutputs();
void taskHousekeeping();

Task tasks[] = {
    {taskReadInputs, 5, 0},
    {taskControl, 5, 0},
    {taskOutputs, 5, 0},
    {taskHousekeeping, 100, 0},
};

static Comms::CommandPacket currentPacket{};
static Comms::SlaveProtocol::LightingCommand pendingLighting{};
static bool outputsEnabled = false;
static Comms::RcStatusMode lastMode = Comms::RcStatusMode::Active;
static bool lastRcLinked = true;
static bool batteryLow = false;
static float latestBattery = 0.0F;
static bool rcHealthy = true;
static bool batteryHealthy = true;
static bool wifiHealthy = true;

void updateHealthState() {
    using namespace Health;
    if (!batteryHealthy) {
        setStatus(HealthCode::LowBattery, "Battery low");
    } else if (!rcHealthy) {
        setStatus(HealthCode::RcSignalLost, "RC link lost");
    } else if (!wifiHealthy) {
        setStatus(HealthCode::WifiDisconnected, "Wi-Fi disconnected");
    } else {
        setStatus(HealthCode::Ok, "All systems nominal");
    }
}

void handleEventLog(const Events::Event& event) {
    Serial.print(F("[EVT] "));
    switch (event.type) {
        case Events::EventType::RcSignalLost:
            Serial.println(F("RC signal lost"));
            break;
        case Events::EventType::RcSignalRestored:
            Serial.println(F("RC signal restored"));
            break;
        case Events::EventType::DriveModeChanged:
            Serial.printf("Drive mode -> %ld\n", static_cast<long>(event.i1));
            break;
        case Events::EventType::LowBattery:
            Serial.printf("Battery low: %.2f V\n", event.f1);
            break;
        case Events::EventType::BatteryRecovered:
            Serial.printf("Battery recovered: %.2f V\n", event.f1);
            break;
        case Events::EventType::ObstacleAhead:
            Serial.printf("Obstacle detected (%.2f)\n", event.f1);
            break;
        default:
            break;
    }
}

void applyRuntimeConfig();

void setup() {
    Serial.begin(115200);
    Serial.println(F("[BOOT] TankRC (master) starting..."));

    Core::setupHardware();
    Serial.println(F("[BOOT] Core hardware ready"));

    configStore.begin();
    Serial.println(F("[BOOT] Config store started"));
    if (!configStore.load(runtimeConfig)) {
        Serial.println(F("[BOOT] Loaded defaults"));
        configStore.save(runtimeConfig);
    } else {
        Serial.println(F("[BOOT] Loaded existing config"));
    }

    Hal::begin(runtimeConfig);
    rcHealthy = batteryHealthy = wifiHealthy = true;
    Health::setStatus(Health::HealthCode::Ok, "Startup");
    Events::subscribe(handleEventLog);
    applyRuntimeConfig();
    Serial.println(F("[BOOT] Runtime config applied"));

    UI::Context uiContext{
        .config = &runtimeConfig,
        .store = &configStore,
        .drive = &driveController,
#if FEATURE_SOUND
        .sound = &sound,
#else
        .sound = nullptr,
#endif
    };
    UI::begin(uiContext, applyRuntimeConfig);
    Serial.println(F("[BOOT] Serial UI ready"));
#if TANKRC_USE_DRIVE_PROXY
    Serial.println(F("[BOOT] Drive controller proxy enabled (UART link to slave)."));
#endif

#if TANKRC_ENABLE_NETWORK
    if (runtimeConfig.features.wifiEnabled) {
        controlServer.begin(&wifiManager, &runtimeConfig, &configStore, applyRuntimeConfig, &sessionLogger);
        Serial.println(F("[BOOT] Control server online"));
        remoteConsole.begin();
        Serial.println(F("[BOOT] Remote console online (telnet 2323)"));
        networkActive = true;
    } else {
        networkActive = false;
        Serial.println(F("[BOOT] Network features disabled via runtime config."));
    }
#else
    Serial.println(F("[BOOT] Network stack disabled (TANKRC_ENABLE_NETWORK=0)"));
#endif
}

void taskReadInputs() {
    currentPacket = radio.poll();
    Network::Overrides overrides{};
#if TANKRC_ENABLE_NETWORK
    if (networkActive) {
        currentPacket.wifiConnected = wifiManager.isConnected() && !wifiManager.isApMode();
        overrides = controlServer.getOverrides();
    } else {
        currentPacket.wifiConnected = false;
    }
#else
    currentPacket.wifiConnected = false;
#endif
    if (overrides.hazardOverride) {
        currentPacket.hazard = overrides.hazardEnabled;
    }
    if (overrides.lightsOverride) {
        currentPacket.lightingState = overrides.lightsEnabled;
    }

    if (lastRcLinked && !currentPacket.rcLinked) {
        Events::publish({Events::EventType::RcSignalLost, Hal::millis32()});
    } else if (!lastRcLinked && currentPacket.rcLinked) {
        Events::publish({Events::EventType::RcSignalRestored, Hal::millis32()});
    }
    lastRcLinked = currentPacket.rcLinked;
    rcHealthy = currentPacket.rcLinked;

#if TANKRC_ENABLE_NETWORK
    wifiHealthy = !networkActive || currentPacket.wifiConnected;
#else
    wifiHealthy = true;
#endif

    updateHealthState();
}

void taskControl() {
    auto driveCommand = currentPacket.drive;
    if (currentPacket.status == Comms::RcStatusMode::Locked) {
        driveCommand.throttle = 0.0F;
        driveCommand.turn = 0.0F;
    } else if (currentPacket.status == Comms::RcStatusMode::Debug) {
        driveCommand.throttle *= 0.5F;
        driveCommand.turn *= 0.5F;
    }
    driveController.setCommand(driveCommand);
    if (currentPacket.status != lastMode) {
        Events::publish({Events::EventType::DriveModeChanged, Hal::millis32(), static_cast<std::int32_t>(currentPacket.status)});
        lastMode = currentPacket.status;
    }
    const float obstacleLevel = std::min(currentPacket.auxChannel5, currentPacket.auxChannel6);
    if (runtimeConfig.features.ultrasonicEnabled && obstacleLevel < 0.2F) {
        Events::publish({Events::EventType::ObstacleAhead, Hal::millis32(), 0, obstacleLevel});
    }
    pendingLighting = {};
    pendingLighting.ultrasonicLeft = runtimeConfig.features.ultrasonicEnabled ? currentPacket.auxChannel5 : 1.0F;
    pendingLighting.ultrasonicRight = runtimeConfig.features.ultrasonicEnabled ? currentPacket.auxChannel6 : 1.0F;
    pendingLighting.status = static_cast<std::uint8_t>(currentPacket.status);
    if (currentPacket.hazard) {
        pendingLighting.flags |= Comms::SlaveProtocol::LightingHazard;
    }
    if (currentPacket.rcLinked) {
        pendingLighting.flags |= Comms::SlaveProtocol::LightingRcLinked;
    }
    if (currentPacket.wifiConnected) {
        pendingLighting.flags |= Comms::SlaveProtocol::LightingWifiLinked;
    }
    outputsEnabled = currentPacket.status != Comms::RcStatusMode::Locked;
    const bool lightingInstalled = runtimeConfig.features.lightsEnabled;
    const bool hazardActive = lightingInstalled && currentPacket.hazard;
    const bool lightEnable = lightingInstalled && outputsEnabled && currentPacket.lightingState;
    const bool lightingEffective = lightEnable || hazardActive;
    if (lightingEffective) {
        pendingLighting.flags |= Comms::SlaveProtocol::LightingEnabled;
    }
    driveController.setLightingCommand(pendingLighting);
    driveController.update();
#if FEATURE_SOUND
    sound.update(outputsEnabled && currentPacket.soundState);
#endif
}

void taskOutputs() {
#if TANKRC_ENABLE_NETWORK
    if (networkActive) {
        Network::ControlState state{};
        state.steering = currentPacket.drive.turn;
        state.throttle = currentPacket.drive.throttle;
        state.hazard = currentPacket.hazard;
        state.lighting = (pendingLighting.flags & Comms::SlaveProtocol::LightingEnabled) != 0;
        state.mode = currentPacket.status;
        state.rcLinked = currentPacket.rcLinked;
        state.wifiLinked = currentPacket.wifiConnected;
        state.ultrasonicLeft = currentPacket.auxChannel5;
        state.ultrasonicRight = currentPacket.auxChannel6;
        state.serverTime = ntpClock.now();
        controlServer.updateState(state);
    }
#endif

    latestBattery = driveController.readBatteryVoltage();
    if (!batteryLow && latestBattery < 11.0F) {
        batteryLow = true;
        Events::publish({Events::EventType::LowBattery, Hal::millis32(), 0, latestBattery});
    } else if (batteryLow && latestBattery > 11.5F) {
        batteryLow = false;
        Events::publish({Events::EventType::BatteryRecovered, Hal::millis32(), 0, latestBattery});
    }
    batteryHealthy = !batteryLow;
    updateHealthState();

#if TANKRC_ENABLE_NETWORK
    if (networkActive && sessionLogger.enabled()) {
        const unsigned long nowMs = Hal::millis32();
        if (lastLogMs == 0 || nowMs - lastLogMs >= 200) {
            lastLogMs = nowMs;
            Logging::LogEntry entry{};
            entry.epoch = ntpClock.now();
            entry.steering = currentPacket.drive.turn;
            entry.throttle = currentPacket.drive.throttle;
            entry.hazard = currentPacket.hazard;
            entry.mode = currentPacket.status;
            entry.battery = latestBattery;
            sessionLogger.log(entry);
        }
    }
#endif
}

void taskHousekeeping() {
#if TANKRC_ENABLE_NETWORK
    if (networkActive) {
        wifiManager.loop();
        ntpClock.update(wifiManager.isConnected());
        controlServer.loop();
        remoteConsole.loop();
    }
#endif
    Events::process();
    UI::update();
}

void loop() {
    if (UI::isWizardActive()) {
        Core::serviceWatchdog();
        Hal::delayMs(1);
        return;
    }
    const std::uint32_t now = Hal::millis32();
    for (auto& task : tasks) {
        if (now - task.lastRunMs >= task.intervalMs) {
            task.lastRunMs = now;
            task.fn();
        }
    }
    Core::serviceWatchdog();
    Hal::delayMs(1);
}

void applyRuntimeConfig() {
#if TANKRC_ENABLE_NETWORK
    const bool enableWifi = runtimeConfig.features.wifiEnabled && FEATURE_WIFI;
    if (enableWifi) {
        if (!wifiInitialized) {
            wifiManager.begin(runtimeConfig);
            wifiInitialized = true;
        } else {
            wifiManager.applyConfig(runtimeConfig);
        }
    }
    networkActive = enableWifi;
    wifiHealthy = !networkActive;
#endif
    Hal::applyConfig(runtimeConfig);
    driveController.begin(runtimeConfig);

#if FEATURE_SOUND
    sound.begin(runtimeConfig.pins.speaker);
    sound.setFeatureEnabled(runtimeConfig.features.soundEnabled);
    sound.update(false);
#endif
    radio.begin(runtimeConfig);
#if TANKRC_ENABLE_NETWORK
    controlServer.notifyConfigApplied();
    ntpClock.configure(runtimeConfig);
    sessionLogger.configure(runtimeConfig.logging);
#endif
    lastLogMs = 0;
    updateHealthState();
}
