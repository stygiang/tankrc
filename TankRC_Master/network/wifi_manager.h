#pragma once
#ifndef TANKRC_NETWORK_WIFI_MANAGER_H
#define TANKRC_NETWORK_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

#include "config/runtime_config.h"

namespace TankRC::Network {
class WifiManager {
  public:
    void begin(const Config::RuntimeConfig& config);
    void applyConfig(const Config::RuntimeConfig& config);
    void loop();

    bool isConnected() const;
    bool isApMode() const;
    String ipAddress() const;
    String apAddress() const;
    String activeSsid() const;
    String apSsid() const { return String(config_.wifi.apSsid); }

  private:
    void startAp();

    Config::RuntimeConfig config_{};
    bool apMode_ = false;
};
}  // namespace TankRC::Network
#endif  // TANKRC_NETWORK_WIFI_MANAGER_H
