#include "storage/config_store.h"

#include <algorithm>
#include <cstddef>

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#endif

namespace TankRC::Storage {
namespace {
constexpr const char* kNamespace = "tankrc";
constexpr const char* kKey = "runtime_cfg";
constexpr const char* kVersionKey = "cfg_version";
constexpr std::size_t kConfigSize = sizeof(Config::RuntimeConfig);

#if defined(ARDUINO_ARCH_ESP32)
Preferences prefs;
#else
Config::RuntimeConfig ramConfig = Config::makeDefaultConfig();
bool ramValid = false;
std::uint32_t ramVersion = Config::kConfigVersion;
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
    if (stored > 0) {
        Config::RuntimeConfig temp = Config::makeDefaultConfig();
        const size_t toRead = std::min(stored, kConfigSize);
        prefs.getBytes(kKey, &temp, toRead);
        config = temp;
        std::uint32_t storedVersion = prefs.getUInt(kVersionKey, config.version);
        if (storedVersion != Config::kConfigVersion) {
            if (Config::migrateConfig(config, storedVersion)) {
                save(config);
            }
        }
        return true;
    }
#else
    if (ramValid) {
        config = ramConfig;
        if (ramVersion != Config::kConfigVersion) {
            if (Config::migrateConfig(config, ramVersion)) {
                save(config);
            }
        }
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
    const bool ok = prefs.putBytes(kKey, &config, kConfigSize) == kConfigSize;
    if (ok) {
        prefs.putUInt(kVersionKey, config.version);
    }
    return ok;
#else
    ramConfig = config;
    ramValid = true;
    ramVersion = config.version;
    return true;
#endif
}

void ConfigStore::reset() {
    if (!ready_) {
        return;
    }

#if defined(ARDUINO_ARCH_ESP32)
    prefs.remove(kKey);
    prefs.remove(kVersionKey);
#else
    ramValid = false;
    ramVersion = Config::kConfigVersion;
#endif
}
}  // namespace TankRC::Storage
