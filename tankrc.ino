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
    #hello


    Core::setupHardware();
    configStore.begin();
    if (!configStore.load(runtimeConfig)) {
        configStore.save(runtimeConfig);
    }

    applyRuntimeConfig();
    radio.begin();

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

    const auto command = radio.poll();
    driveController.setCommand(command.drive);

    driveController.update();
    lighting.update(command.lightingState);
    sound.update(command.soundState);
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
}
