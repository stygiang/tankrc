#pragma once

#include <Print.h>

#include "config/runtime_config.h"
#include "storage/config_store.h"

namespace TankRC {
namespace Control {
class DriveController;
}

namespace Features {
class SoundFx;
}

}  // namespace TankRC

namespace TankRC::UI {
enum class ConsoleSource { Serial, Remote };

struct Context {
    Config::RuntimeConfig* config = nullptr;
    Storage::ConfigStore* store = nullptr;
    Control::DriveController* drive = nullptr;
    Features::SoundFx* sound = nullptr;
};

using ApplyConfigCallback = void (*)();

void begin(const Context& ctx, ApplyConfigCallback applyCallback);
void update();
bool isWizardActive();
void addConsoleTap(Print* tap);
void removeConsoleTap(Print* tap);
#if TANKRC_ENABLE_NETWORK
void setRemoteConsoleTap(Print* tap);
#endif
void injectRemoteLine(const String& line, ConsoleSource source);
}  // namespace TankRC::UI
