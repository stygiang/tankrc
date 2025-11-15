#pragma once

#include <WebServer.h>

#include "comms/radio_link.h"
#include "config/runtime_config.h"
#include "logging/session_logger.h"
#include "network/wifi_manager.h"
#include "storage/config_store.h"

namespace TankRC::Network {
struct ControlState {
    float steering = 0.0F;
    float throttle = 0.0F;
    bool hazard = false;
    bool lighting = false;
    Comms::RcStatusMode mode = Comms::RcStatusMode::Active;
    bool rcLinked = true;
    bool wifiLinked = true;
    float ultrasonicLeft = 1.0F;
    float ultrasonicRight = 1.0F;
    std::uint32_t serverTime = 0;
};

struct Overrides {
    bool hazardOverride = false;
    bool hazardEnabled = false;
    bool lightsOverride = false;
    bool lightsEnabled = false;
};

class ControlServer {
  public:
    using ApplyConfigCallback = void (*)();

    void begin(WifiManager* wifi,
               Config::RuntimeConfig* config,
               Storage::ConfigStore* store,
               ApplyConfigCallback applyCallback,
               Logging::SessionLogger* logger);
    void loop();
    void updateState(const ControlState& state);
    Overrides getOverrides() const;
    void clearOverrides();
    void notifyConfigApplied();

  private:
    void handleRoot();
    void handleStatus();
    void handleConfigGet();
    void handleConfigExport();
    void handleConfigImport();
    void handleConfigPost();
    void handleControlPost();
    String buildStatusJson() const;
    String buildConfigJson(bool includeSensitive = false) const;
    void sendJson(const String& body);

    WifiManager* wifi_ = nullptr;
    Config::RuntimeConfig* config_ = nullptr;
    Storage::ConfigStore* store_ = nullptr;
    ApplyConfigCallback applyCallback_ = nullptr;
    Logging::SessionLogger* logger_ = nullptr;
    WebServer server_{80};
    ControlState state_{};
    Overrides overrides_{};
};
}  // namespace TankRC::Network
