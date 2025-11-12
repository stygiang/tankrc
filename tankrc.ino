#include "TankRC.h"

using namespace TankRC;

static Control::DriveController driveController;
static Features::Lighting lighting;
static Features::SoundFx sound;
static Comms::RadioLink radio;
static Config::RuntimeConfig runtimeConfig = Config::makeDefaultConfig();
static Storage::ConfigStore configStore;

void applyRuntimeConfig();

void setup() {
    Serial.begin(115200);

    Core::setupHardware();
    configStore.begin();
    if (!configStore.load(runtimeConfig)) {
        configStore.save(runtimeConfig);
    }

    applyRuntimeConfig();

    UI::Context uiContext{
        .config = &runtimeConfig,
        .store = &configStore,
        .drive = &driveController,
        .lighting = &lighting,
        .sound = &sound,
    };
    UI::begin(uiContext, applyRuntimeConfig);
}

void loop() {
    UI::update();

    if (UI::isWizardActive()) {
        Core::serviceWatchdog();
        return;
    }

    const auto packet = radio.poll();
    auto driveCommand = packet.drive;
    if (packet.status == Comms::RcStatusMode::Locked) {
        driveCommand.throttle = 0.0F;
        driveCommand.turn = 0.0F;
    } else if (packet.status == Comms::RcStatusMode::Debug) {
        driveCommand.throttle *= 0.5F;
        driveCommand.turn *= 0.5F;
    }

    driveController.setCommand(driveCommand);
    driveController.update();

    const bool outputsEnabled = packet.status != Comms::RcStatusMode::Locked;
    lighting.update(outputsEnabled && packet.lightingState);
    sound.update(outputsEnabled && packet.soundState);
    Core::serviceWatchdog();
}

void applyRuntimeConfig() {
    driveController.begin(runtimeConfig);
    lighting.begin(runtimeConfig.pins.lightBar);
    lighting.setFeatureEnabled(runtimeConfig.features.lightingEnabled);
    lighting.update(false);

    sound.begin(runtimeConfig.pins.speaker);
    sound.setFeatureEnabled(runtimeConfig.features.soundEnabled);
    sound.update(false);
    radio.begin(runtimeConfig);
}
