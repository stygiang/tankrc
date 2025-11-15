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

void loop() {
#if TANKRC_ENABLE_NETWORK
    wifiManager.loop();
    ntpClock.update(wifiManager.isConnected());
    controlServer.loop();
    remoteConsole.loop();
#endif
    UI::update();

    if (UI::isWizardActive()) {
        Core::serviceWatchdog();
        return;
    }

    auto packet = radio.poll();
#if TANKRC_ENABLE_NETWORK
    packet.wifiConnected = wifiManager.isConnected() && !wifiManager.isApMode();
    const auto overrides = controlServer.getOverrides();
#else
    packet.wifiConnected = false;
    Network::Overrides overrides{};
#endif

    if (overrides.hazardOverride) {
        packet.hazard = overrides.hazardEnabled;
    }
    if (overrides.lightsOverride) {
        packet.lightingState = overrides.lightsEnabled;
    }

    auto driveCommand = packet.drive;
    if (packet.status == Comms::RcStatusMode::Locked) {
        driveCommand.throttle = 0.0F;
        driveCommand.turn = 0.0F;
    } else if (packet.status == Comms::RcStatusMode::Debug) {
        driveCommand.throttle *= 0.5F;
        driveCommand.turn *= 0.5F;
    }

    driveController.setCommand(driveCommand);

    Comms::SlaveProtocol::LightingCommand lightingCommand{};
    lightingCommand.ultrasonicLeft = packet.auxChannel5;
    lightingCommand.ultrasonicRight = packet.auxChannel6;
    lightingCommand.status = static_cast<std::uint8_t>(packet.status);
    if (packet.hazard) {
        lightingCommand.flags |= Comms::SlaveProtocol::LightingHazard;
    }
    if (packet.rcLinked) {
        lightingCommand.flags |= Comms::SlaveProtocol::LightingRcLinked;
    }
    if (packet.wifiConnected) {
        lightingCommand.flags |= Comms::SlaveProtocol::LightingWifiLinked;
    }
    const bool outputsEnabled = packet.status != Comms::RcStatusMode::Locked;

    const bool lightingInstalled = runtimeConfig.features.lightingEnabled;
    const bool hazardActive = lightingInstalled && packet.hazard;
    const bool lightEnable = lightingInstalled && outputsEnabled && packet.lightingState;
    const bool lightingEffective = lightEnable || hazardActive;
    if (lightingEffective) {
        lightingCommand.flags |= Comms::SlaveProtocol::LightingEnabled;
    }

    driveController.setLightingCommand(lightingCommand);
    driveController.update();

    sound.update(outputsEnabled && packet.soundState);

#if TANKRC_ENABLE_NETWORK
    Network::ControlState state{};
    state.steering = driveCommand.turn;
    state.throttle = driveCommand.throttle;
    state.hazard = packet.hazard;
    state.lighting = lightingEffective;
    state.mode = packet.status;
    state.rcLinked = packet.rcLinked;
    state.wifiLinked = packet.wifiConnected;
    state.ultrasonicLeft = packet.auxChannel5;
    state.ultrasonicRight = packet.auxChannel6;
    state.serverTime = ntpClock.now();
    controlServer.updateState(state);

    if (sessionLogger.enabled()) {
        const unsigned long nowMs = millis();
        if (lastLogMs == 0 || nowMs - lastLogMs >= 200) {
            lastLogMs = nowMs;
            Logging::LogEntry entry{};
            entry.epoch = ntpClock.now();
            entry.steering = driveCommand.turn;
            entry.throttle = driveCommand.throttle;
            entry.hazard = packet.hazard;
            entry.mode = packet.status;
            entry.battery = driveController.readBatteryVoltage();
            sessionLogger.log(entry);
        }
    }
#endif

    Core::serviceWatchdog();
    delay(1);
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
