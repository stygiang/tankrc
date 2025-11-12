#pragma once
#ifndef TANKRC_STORAGE_CONFIG_STORE_H
#define TANKRC_STORAGE_CONFIG_STORE_H

#include "config/runtime_config.h"

namespace TankRC::Storage {
class ConfigStore {
  public:
    bool begin();
    bool load(Config::RuntimeConfig& config);
    bool save(const Config::RuntimeConfig& config);
    void reset();

  private:
    bool ready_ = false;
};
}  // namespace TankRC::Storage
#endif  // TANKRC_STORAGE_CONFIG_STORE_H
