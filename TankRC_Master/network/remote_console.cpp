#include "network/remote_console.h"

#if TANKRC_ENABLE_NETWORK

#include "ui/console.h"

namespace TankRC::Network {
void RemoteConsole::begin(uint16_t port) {
    server_ = WiFiServer(port);
    server_.begin();
}

void RemoteConsole::loop() {
    if (!client_ || !client_.connected()) {
        if (client_) {
            client_.stop();
            UI::setRemoteConsoleTap(nullptr);
        }
        WiFiClient next = server_.available();
        if (next) {
            client_ = next;
            client_.println("TankRC remote console ready. Type help.");
            buffer_.clear();
            clientPrint_.setClient(&client_);
            UI::setRemoteConsoleTap(&clientPrint_);
            client_.print("> ");
        }
        return;
    }

    while (client_.available()) {
        char c = static_cast<char>(client_.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            String line = buffer_;
            buffer_.clear();
            UI::injectRemoteLine(line);
        } else {
            buffer_ += c;
        }
    }
}
}  // namespace TankRC::Network

#endif  // TANKRC_ENABLE_NETWORK
