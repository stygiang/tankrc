#pragma once

#include <Print.h>

#include "config/runtime_config.h"
#include "storage/config_store.h"

namespace TankRC {
namespace Control {
class DriveController;
}

namespace Features {
class Lighting;
class SoundFx;
}
}  // namespace TankRC

namespace TankRC::UI {
struct Context {
    Config::RuntimeConfig* config = nullptr;
    Storage::ConfigStore* store = nullptr;
    Control::DriveController* drive = nullptr;
    Features::Lighting* lighting = nullptr;
    Features::SoundFx* sound = nullptr;
};

using ApplyConfigCallback = void (*)();

void begin(const Context& ctx, ApplyConfigCallback applyCallback);
void update();
bool isWizardActive();
#if TANKRC_ENABLE_NETWORK
void setRemoteConsoleTap(Print* tap);
#endif
void injectRemoteLine(const String& line);
}  // namespace TankRC::UI
