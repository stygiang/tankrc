#pragma once

#include "../config/build_config.h"

#if TANKRC_ENABLE_NETWORK

#include <Print.h>
#include <WiFi.h>

namespace TankRC::Network {
class RemoteConsole {
  public:
    void begin(uint16_t port = 2323);
    void loop();

  private:
    class ClientPrint : public Print {
      public:
        void setClient(WiFiClient* client) { client_ = client; }
        size_t write(uint8_t b) override {
            if (client_ && client_->connected()) {
                client_->write(b);
            }
            return 1;
        }

      private:
        WiFiClient* client_ = nullptr;
    };

    WiFiServer server_{2323};
    WiFiClient client_{};
    String buffer_;
    ClientPrint clientPrint_{};
};
}  // namespace TankRC::Network

#endif  // TANKRC_ENABLE_NETWORK
