#include "network/wifi_manager.h"

namespace TankRC::Network {
void WifiManager::begin(const Config::RuntimeConfig& config) {
    config_ = config;
    startAp();
}

void WifiManager::applyConfig(const Config::RuntimeConfig& config) {
    config_ = config;
    startAp();
}

void WifiManager::loop() {
    delay(0);
}

bool WifiManager::isConnected() const {
    return false;
}

bool WifiManager::isApMode() const {
    return true;
}

String WifiManager::ipAddress() const {
    return WiFi.softAPIP().toString();
}

String WifiManager::apAddress() const {
    return WiFi.softAPIP().toString();
}

String WifiManager::activeSsid() const {
    return String(config_.wifi.apSsid);
}

void WifiManager::startAp() {
    apMode_ = true;
    WiFi.mode(WIFI_AP);
    const char* apSsid = config_.wifi.apSsid[0] ? config_.wifi.apSsid : "sharc";
    const char* apPass = config_.wifi.apPassword[0] ? config_.wifi.apPassword : "tankrc123";
    WiFi.softAP(apSsid, apPass);
    Serial.print(F("[TankRC] Access point \""));
    Serial.print(apSsid);
    Serial.print(F("\" initialized ("));
    Serial.print(WiFi.softAPIP());
    Serial.println(F(")."));
}
}  // namespace TankRC::Network
