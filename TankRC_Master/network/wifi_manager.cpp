#include "network/wifi_manager.h"

namespace TankRC::Network {
namespace {
constexpr unsigned long kConnectTimeoutMs = 10000;
constexpr unsigned long kReconnectIntervalMs = 5000;
}

void WifiManager::begin(const Config::RuntimeConfig& config) {
    config_ = config;
    WiFi.mode(WIFI_STA);
    connectStation();
}

void WifiManager::applyConfig(const Config::RuntimeConfig& config) {
    config_ = config;
    if (!apMode_) {
        WiFi.disconnect(true, true);
    } else {
        WiFi.softAPdisconnect(true);
    }
    WiFi.mode(WIFI_STA);
    connectStation();
}

void WifiManager::loop() {
    if (apMode_) {
        delay(0);
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        stationConnecting_ = false;
        delay(0);
        return;
    }
    const unsigned long now = millis();
    if (!stationConnecting_ && now - lastAttemptMs_ > kReconnectIntervalMs) {
        connectStation();
        return;
    }
    if (stationConnecting_ && now - lastAttemptMs_ > kConnectTimeoutMs) {
        startAp();
    }
    delay(0);
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::isApMode() const {
    return apMode_;
}

String WifiManager::ipAddress() const {
    if (apMode_) {
        return WiFi.softAPIP().toString();
    }
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return String("0.0.0.0");
}

String WifiManager::apAddress() const {
    return WiFi.softAPIP().toString();
}

String WifiManager::activeSsid() const {
    if (apMode_) {
        return String(config_.wifi.apSsid);
    }
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.SSID();
    }
    return String();
}

void WifiManager::connectStation() {
    stationConnecting_ = true;
    lastAttemptMs_ = millis();
    apMode_ = false;

    if (config_.wifi.ssid[0] == '\0') {
        startAp();
        return;
    }

    WiFi.begin(config_.wifi.ssid, config_.wifi.password);
}

void WifiManager::startAp() {
    stationConnecting_ = false;
    apMode_ = true;
    WiFi.mode(WIFI_AP);
    const char* apSsid = config_.wifi.apSsid[0] ? config_.wifi.apSsid : "TankRC-Setup";
    const char* apPass = config_.wifi.apPassword[0] ? config_.wifi.apPassword : "tankrc123";
    WiFi.softAP(apSsid, apPass);
}
}  // namespace TankRC::Network
