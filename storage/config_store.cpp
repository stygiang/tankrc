#include "storage/config_store.h"

#include <cstddef>

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#endif

namespace TankRC::Storage {
namespace {
constexpr const char* kNamespace = "tankrc";
constexpr const char* kKey = "runtime_cfg";
constexpr std::size_t kConfigSize = sizeof(Config::RuntimeConfig);

#if defined(ARDUINO_ARCH_ESP32)
Preferences prefs;
#else
Config::RuntimeConfig ramConfig = Config::makeDefaultConfig();
bool ramValid = false;
#endif
}  // namespace

bool ConfigStore::begin() {
#if defined(ARDUINO_ARCH_ESP32)
    ready_ = prefs.begin(kNamespace, false);
#else
    ready_ = true;
#endif
    return ready_;
}

bool ConfigStore::load(Config::RuntimeConfig& config) {
    if (!ready_) {
        config = Config::makeDefaultConfig();
        return false;
    }

#if defined(ARDUINO_ARCH_ESP32)
    const size_t stored = prefs.getBytesLength(kKey);
    if (stored == kConfigSize) {
        prefs.getBytes(kKey, &config, kConfigSize);
        if (config.version == Config::kConfigVersion) {
            return true;
        }
    }
#else
    if (ramValid && ramConfig.version == Config::kConfigVersion) {
        config = ramConfig;
        return true;
    }
#endif

    config = Config::makeDefaultConfig();
    return false;
}

bool ConfigStore::save(const Config::RuntimeConfig& config) {
    if (!ready_) {
        return false;
    }

#if defined(ARDUINO_ARCH_ESP32)
    return prefs.putBytes(kKey, &config, kConfigSize) == kConfigSize;
#else
    ramConfig = config;
    ramValid = true;
    return true;
#endif
}

void ConfigStore::reset() {
    if (!ready_) {
        return;
    }

#if defined(ARDUINO_ARCH_ESP32)
    prefs.remove(kKey);
#else
    ramValid = false;
#endif
}
}  // namespace TankRC::Storage
