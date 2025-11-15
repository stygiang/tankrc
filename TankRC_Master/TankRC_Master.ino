#ifndef TANKRC_BUILD_MASTER
#define TANKRC_BUILD_MASTER 1
#endif

#ifndef TANKRC_ENABLE_NETWORK
#define TANKRC_ENABLE_NETWORK 0
#endif

#ifndef TANKRC_USE_DRIVE_PROXY
#define TANKRC_USE_DRIVE_PROXY 1
#endif

#include "TankRC.h"

#if TANKRC_ENABLE_NETWORK
#include "network/remote_console.h"
#endif

using namespace TankRC;

static Control::DriveController driveController;
static Features::SoundFx sound;
static Comms::RadioLink radio;
static Config::RuntimeConfig runtimeConfig = Config::makeDefaultConfig();
static Storage::ConfigStore configStore;
#if TANKRC_ENABLE_NETWORK
static Network::WifiManager wifiManager;
static Network::ControlServer controlServer;
static Network::RemoteConsole remoteConsole;
static bool wifiInitialized = false;
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
    applyRuntimeConfig();
    Serial.println(F("[BOOT] Runtime config applied"));

    UI::Context uiContext{
        .config = &runtimeConfig,
        .store = &configStore,
        .drive = &driveController,
        .sound = &sound,
    };
    UI::begin(uiContext, applyRuntimeConfig);
    Serial.println(F("[BOOT] Serial UI ready"));
#if TANKRC_USE_DRIVE_PROXY
    Serial.println(F("[BOOT] Drive controller proxy enabled (UART link to slave)."));
#endif

#if TANKRC_ENABLE_NETWORK
    controlServer.begin(&wifiManager, &runtimeConfig, &configStore, applyRuntimeConfig, &sessionLogger);
    Serial.println(F("[BOOT] Control server online"));
    remoteConsole.begin();
    Serial.println(F("[BOOT] Remote console online (telnet 2323)"));
#else
    Serial.println(F("[BOOT] Network stack disabled (TANKRC_ENABLE_NETWORK=0)"));
#endif
}

static Comms::CommandPacket currentPacket{};
static Comms::SlaveProtocol::LightingCommand pendingLighting{};
static bool outputsEnabled = false;

void taskReadInputs() {
    currentPacket = radio.poll();
#if TANKRC_ENABLE_NETWORK
    currentPacket.wifiConnected = wifiManager.isConnected() && !wifiManager.isApMode();
    const auto overrides = controlServer.getOverrides();
#else
    currentPacket.wifiConnected = false;
    Network::Overrides overrides{};
#endif
    if (overrides.hazardOverride) {
        currentPacket.hazard = overrides.hazardEnabled;
    }
    if (overrides.lightsOverride) {
        currentPacket.lightingState = overrides.lightsEnabled;
    }
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
    pendingLighting = {};
    pendingLighting.ultrasonicLeft = currentPacket.auxChannel5;
    pendingLighting.ultrasonicRight = currentPacket.auxChannel6;
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
    const bool lightingInstalled = runtimeConfig.features.lightingEnabled;
    const bool hazardActive = lightingInstalled && currentPacket.hazard;
    const bool lightEnable = lightingInstalled && outputsEnabled && currentPacket.lightingState;
    const bool lightingEffective = lightEnable || hazardActive;
    if (lightingEffective) {
        pendingLighting.flags |= Comms::SlaveProtocol::LightingEnabled;
    }
    driveController.setLightingCommand(pendingLighting);
    driveController.update();
    sound.update(outputsEnabled && currentPacket.soundState);
}

void taskOutputs() {
#if TANKRC_ENABLE_NETWORK
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

    if (sessionLogger.enabled()) {
        const unsigned long nowMs = Hal::millis32();
        if (lastLogMs == 0 || nowMs - lastLogMs >= 200) {
            lastLogMs = nowMs;
            Logging::LogEntry entry{};
            entry.epoch = ntpClock.now();
            entry.steering = currentPacket.drive.turn;
            entry.throttle = currentPacket.drive.throttle;
            entry.hazard = currentPacket.hazard;
            entry.mode = currentPacket.status;
            entry.battery = driveController.readBatteryVoltage();
            sessionLogger.log(entry);
        }
    }
#endif
}

void taskHousekeeping() {
#if TANKRC_ENABLE_NETWORK
    wifiManager.loop();
    ntpClock.update(wifiManager.isConnected());
    controlServer.loop();
    remoteConsole.loop();
#endif
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
    if (!wifiInitialized) {
        wifiManager.begin(runtimeConfig);
        wifiInitialized = true;
    } else {
        wifiManager.applyConfig(runtimeConfig);
    }
#endif
    Hal::applyConfig(runtimeConfig);
    driveController.begin(runtimeConfig);

    sound.begin(runtimeConfig.pins.speaker);
    sound.setFeatureEnabled(runtimeConfig.features.soundEnabled);
    sound.update(false);
    radio.begin(runtimeConfig);
#if TANKRC_ENABLE_NETWORK
    controlServer.notifyConfigApplied();
    ntpClock.configure(runtimeConfig);
    sessionLogger.configure(runtimeConfig.logging);
#endif
    lastLogMs = 0;
}
