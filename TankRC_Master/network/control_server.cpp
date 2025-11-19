#include "network/control_server.h"

#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <vector>

#include "config/pin_schema.h"
namespace TankRC::Network {
namespace {

class JsonStream {
  public:
    explicit JsonStream(const String& input) : data_(input), raw_(data_.c_str()), len_(data_.length()) {}

    bool parseObject(const std::function<bool(const String&)>& handler) {
        skipWhitespace();
        if (!consume('{')) {
            return false;
        }
        skipWhitespace();
        if (peek() == '}') {
            get();
            return true;
        }
        while (true) {
            String key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (!consume(':')) {
                return false;
            }
            skipWhitespace();
            if (!handler(key)) {
                return false;
            }
            skipWhitespace();
            char c = get();
            if (c == '}') {
                break;
            }
            if (c != ',') {
                return false;
            }
            skipWhitespace();
        }
        return true;
    }

    bool parseArray(const std::function<bool(size_t)>& handler) {
        skipWhitespace();
        if (!consume('[')) {
            return false;
        }
        skipWhitespace();
        if (peek() == ']') {
            get();
            return true;
        }
        size_t index = 0;
        while (true) {
            if (!handler(index)) {
                return false;
            }
            ++index;
            skipWhitespace();
            char c = get();
            if (c == ']') {
                break;
            }
            if (c != ',') {
                return false;
            }
            skipWhitespace();
        }
        return true;
    }

    bool parseString(String& out) {
        skipWhitespace();
        if (!consume('\"')) {
            return false;
        }
        out = "";
        while (pos_ < len_) {
            char c = get();
            if (c == '\"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= len_) {
                    return false;
                }
                char esc = get();
                switch (esc) {
                    case '\"':
                        out += '\"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u': {
                        if (pos_ + 4 > len_) {
                            return false;
                        }
                        uint16_t value = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = get();
                            value <<= 4;
                            if (h >= '0' && h <= '9') value |= h - '0';
                            else if (h >= 'a' && h <= 'f') value |= 10 + (h - 'a');
                            else if (h >= 'A' && h <= 'F') value |= 10 + (h - 'A');
                            else return false;
                        }
                        if (value <= 0x7F) {
                            out += static_cast<char>(value);
                        } else if (value <= 0x7FF) {
                            out += static_cast<char>(0xC0 | ((value >> 6) & 0x1F));
                            out += static_cast<char>(0x80 | (value & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | ((value >> 12) & 0x0F));
                            out += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (value & 0x3F));
                        }
                        break;
                    }
                    default:
                        return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }

    bool parseBool(bool& out) {
        skipWhitespace();
        if (matchLiteral("true")) {
            out = true;
            return true;
        }
        if (matchLiteral("false")) {
            out = false;
            return true;
        }
        return false;
    }

    bool parseNull() {
        skipWhitespace();
        return matchLiteral("null");
    }

    bool parseNumber(double& out) {
        skipWhitespace();
        size_t start = pos_;
        if (peek() == '-' || peek() == '+') {
            ++pos_;
        }
        while (std::isdigit(peek())) {
            ++pos_;
        }
        if (peek() == '.') {
            ++pos_;
            while (std::isdigit(peek())) {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            while (std::isdigit(peek())) {
                ++pos_;
            }
        }
        if (start == pos_) {
            return false;
        }
        String number = data_.substring(start, pos_);
        out = number.toFloat();
        return true;
    }

    bool parseInt(int& out) {
        double number = 0.0;
        if (!parseNumber(number)) {
            return false;
        }
        out = static_cast<int>(number);
        return true;
    }

    bool skipValue() {
        skipWhitespace();
        char c = peek();
        if (c == '\"') {
            String tmp;
            return parseString(tmp);
        }
        if (c == '{') {
            return parseObject([&](const String&) { return skipValue(); });
        }
        if (c == '[') {
            return parseArray([&](size_t) { return skipValue(); });
        }
        if (c == 't' || c == 'f') {
            bool b;
            return parseBool(b);
        }
        if (c == 'n') {
            return parseNull();
        }
        double number;
        return parseNumber(number);
    }

  private:
    void skipWhitespace() {
        while (pos_ < len_ && std::isspace(static_cast<unsigned char>(raw_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    char peek() const {
        if (pos_ >= len_) {
            return '\0';
        }
        return raw_[pos_];
    }

    char get() {
        if (pos_ >= len_) {
            return '\0';
        }
        return raw_[pos_++];
    }

    bool matchLiteral(const char* literal) {
        size_t len = strlen(literal);
        if (pos_ + len > len_) {
            return false;
        }
        if (strncmp(raw_ + pos_, literal, len) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    String data_;
    const char* raw_;
    size_t len_;
    size_t pos_ = 0;
};

const char CONTROL_PAGE_TEMPLATE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>TankRC Control Hub</title>
<style>
:root {
    --bg:#050b16;
    --panel:#10182b;
    --accent:#3be3a4;
    --accent-dark:#24b784;
    --text:#f5f7fb;
    --muted:#8a9db7;
}
* { box-sizing:border-box; }
body {
    margin:0;
    min-height:100vh;
    font-family:"Segoe UI",system-ui,-apple-system,sans-serif;
    color:var(--text);
    background:linear-gradient(180deg,#04070f,#0d1321 45%,#0c111b);
}
main {
    max-width:900px;
    margin:0 auto;
    padding:1.5rem 1rem 3rem;
}
header.hero {
    background:var(--panel);
    border-radius:18px;
    margin:1rem;
    padding:1.25rem 1.75rem;
    display:flex;
    justify-content:space-between;
    align-items:center;
    gap:1rem;
    box-shadow:0 20px 50px rgba(0,0,0,0.35);
}
.hero h1 { margin:0; font-size:1.9rem; }
.hero p { margin:0.35rem 0 0; color:var(--muted); }
.status-tags { display:flex; gap:0.6rem; flex-wrap:wrap; }
.status-pill {
    background:rgba(255,255,255,0.08);
    padding:0.4rem 0.9rem;
    border-radius:999px;
    font-size:0.85rem;
    letter-spacing:0.03em;
}
.panel {
    background:var(--panel);
    border-radius:18px;
    margin:1rem;
    padding:1.5rem;
    box-shadow:0 20px 60px rgba(0,0,0,0.45);
}
.panel > header {
    display:flex;
    justify-content:space-between;
    align-items:center;
    flex-wrap:wrap;
    gap:0.5rem;
}
.panel h2 { margin:0; font-size:1.35rem; }
.panel p { margin:0; color:var(--muted); }
.feature-grid {
    margin-top:1rem;
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(220px,1fr));
    gap:1rem;
}
.feature-card {
    border:1px solid rgba(255,255,255,0.06);
    border-radius:14px;
    padding:1rem;
    background:rgba(255,255,255,0.02);
    display:flex;
    flex-direction:column;
    gap:0.75rem;
}
.feature-card strong { display:block; font-size:1.05rem; }
.feature-card small { color:var(--muted); }
.feature-card__actions {
    display:flex;
    justify-content:flex-end;
    gap:0.5rem;
    margin-top:0.25rem;
}
.feature-card__actions button {
    flex:1;
    border:none;
    border-radius:10px;
    padding:0.45rem 0.6rem;
    cursor:pointer;
    font-size:0.9rem;
    font-weight:600;
    transition:background 0.2s ease, color 0.2s ease;
}
.feature-card__actions button.enable {
    background:rgba(59,227,164,0.12);
    color:var(--accent);
}
.feature-card__actions button.disable {
    background:rgba(255,255,255,0.08);
    color:var(--text);
}
.feature-card__actions button:disabled {
    opacity:0.4;
    cursor:not-allowed;
}
.feature-status { font-size:0.85rem; font-weight:600; letter-spacing:0.04em; }
.feature-status.enabled { color:var(--accent); }
.feature-status.disabled { color:#f4f6ff; opacity:0.6; }
.toast {
    position:fixed;
    left:50%;
    bottom:1.5rem;
    transform:translateX(-50%);
    background:rgba(15,40,70,0.9);
    border:1px solid rgba(255,255,255,0.1);
    padding:0.75rem 1.1rem;
    border-radius:999px;
    display:none;
    align-items:center;
    box-shadow:0 15px 30px rgba(0,0,0,0.5);
}
.toast.show { display:flex; }
@media (max-width:600px) {
    header.hero { flex-direction:column; align-items:flex-start; }
    .panel { margin:0.8rem; }
}
</style>
</head>
<body>
<header class="hero">
    <div>
        <h1>TankRC Control Hub</h1>
        <p>Connect to the "sharc" access point to reach this page from the master tank.</p>
    </div>
    <div class="status-tags">
        <span class="status-pill" id="statusBadge">Connecting...</span>
    </div>
</header>
<main>
    <section class="panel">
        <header>
            <div>
                <h2>Feature toggles</h2>
                <p style="margin:0;">Enable or disable key modules such as lighting, sound, sensors, and Wi-Fi.</p>
            </div>
            <div class="status-pill">AP: sharc</div>
        </header>
        <div class="feature-grid" id="featureGrid"></div>
    </section>
</main>
<div class="toast" id="toast"></div>
<script>
const featureFields = [
    { key: 'lightingEnabled', stateKey: 'lighting', label: 'Lighting', description: 'Light bar channels and blink patterns.' },
    { key: 'soundEnabled', stateKey: 'sound', label: 'Sound', description: 'Speaker output and FX engine.' },
    { key: 'sensorsEnabled', stateKey: 'sensors', label: 'Sensors', description: 'Ultrasonic and tip sensors.' },
    { key: 'wifiEnabled', stateKey: 'wifi', label: 'Wi-Fi', description: 'Enable station/AP networking.' },
    { key: 'ultrasonicEnabled', stateKey: 'ultrasonic', label: 'Ultrasonic', description: 'Allow ultrasonic range sensors.' },
    { key: 'tipOverEnabled', stateKey: 'tip', label: 'Tip-over', description: 'Enable tip-over protection routines.' },
];
let config = null;
const featureGrid = document.getElementById('featureGrid');
const toast = document.getElementById('toast');
const statusBadge = document.getElementById('statusBadge');
const refreshIntervalMs = 4000;

function showToast(message, tone = 'info') {
    toast.textContent = message;
    toast.style.borderColor = tone === 'danger' ? 'rgba(255,56,100,0.6)' : tone === 'warn' ? 'rgba(255,183,3,0.6)' : 'rgba(59,227,164,0.6)';
    toast.classList.add('show');
    clearTimeout(toast.dataset.timeout);
    toast.dataset.timeout = setTimeout(() => toast.classList.remove('show'), 2800);
}

function applyFeatureToggle(field, enabled, statusEl, enableBtn, disableBtn) {
    enableBtn.disabled = true;
    disableBtn.disabled = true;
    const payload = { [field.key]: enabled ? '1' : '0' };
    postConfig(payload)
        .then(() => refreshConfig())
        .then(() => {
            statusEl.textContent = enabled ? 'Enabled' : 'Disabled';
            statusEl.classList.toggle('enabled', enabled);
            statusEl.classList.toggle('disabled', !enabled);
            showToast(`${field.label} ${enabled ? 'enabled' : 'disabled'}`);
        })
        .catch(err => showToast(err.message, 'danger'))
        .finally(() => {
            enableBtn.disabled = enabled;
            disableBtn.disabled = !enabled;
        });
}

function renderFeatureToggles() {
    if (!config) {
        featureGrid.innerHTML = '';
        return;
    }
    featureGrid.innerHTML = '';
    for (const field of featureFields) {
        const enabled = !!config.features[field.stateKey];
        const card = document.createElement('article');
        card.className = 'feature-card';
        card.innerHTML = `
            <strong>${field.label}</strong>
            <small>${field.description}</small>
            <span class="feature-status ${enabled ? 'enabled' : 'disabled'}">
                ${enabled ? 'Enabled' : 'Disabled'}
            </span>
            <div class="feature-card__actions">
                <button type="button" class="enable" data-field="${field.key}">Enable</button>
                <button type="button" class="disable" data-field="${field.key}">Disable</button>
            </div>`;
        const status = card.querySelector('.feature-status');
        const enableBtn = card.querySelector('button.enable');
        const disableBtn = card.querySelector('button.disable');
        enableBtn.disabled = enabled;
        disableBtn.disabled = !enabled;
        enableBtn.addEventListener('click', () => applyFeatureToggle(field, true, status, enableBtn, disableBtn));
        disableBtn.addEventListener('click', () => applyFeatureToggle(field, false, status, enableBtn, disableBtn));
        featureGrid.appendChild(card);
    }
}

async function fetchJson(path) {
    const resp = await fetch(path);
    if (!resp.ok) {
        throw new Error(`Request failed (${resp.status})`);
    }
    return resp.json();
}

async function refreshConfig() {
    config = await fetchJson('/api/config');
    renderFeatureToggles();
}

async function refreshStatus() {
    const state = await fetchJson('/api/status');
    const labels = [];
    labels.push(`RC ${state.rcLink ? 'online' : 'offline'}`);
    labels.push(`Wi-Fi ${state.wifiLink ? 'online' : 'offline'}`);
    labels.push(state.mode);
    statusBadge.textContent = labels.join(' • ');
}

async function postConfig(payload) {
    const data = new URLSearchParams();
    Object.entries(payload).forEach(([key, value]) => data.append(key, value));
    const resp = await fetch('/api/config', {
        method: 'POST',
        body: data,
    });
    if (!resp.ok) {
        throw new Error('Failed to save settings');
    }
}

document.addEventListener('DOMContentLoaded', () => {
    Promise.all([refreshConfig(), refreshStatus()])
        .catch(err => showToast(err.message, 'danger'));
    setInterval(() => {
        refreshConfig().catch(err => showToast(err.message, 'danger'));
        refreshStatus().catch(() => {});
    }, refreshIntervalMs);
});
</script>
</body>
</html>
)HTML";


String modeToString(Comms::RcStatusMode mode) {
    switch (mode) {
        case Comms::RcStatusMode::Debug:
            return "Debug";
        case Comms::RcStatusMode::Locked:
            return "Locked";
        default:
            return "Active";
    }
}

String modeClass(Comms::RcStatusMode mode) {
    switch (mode) {
        case Comms::RcStatusMode::Debug:
            return "debug";
        case Comms::RcStatusMode::Locked:
            return "locked";
        default:
            return "active";
    }
}

String escapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '\"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

bool parseIntStrict(const String& text, int& value) {
    String trimmed = text;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return false;
    }
    int start = 0;
    if (trimmed[start] == '-' || trimmed[start] == '+') {
        ++start;
    }
    if (start >= trimmed.length()) {
        return false;
    }
    for (int i = start; i < trimmed.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            return false;
        }
    }
    value = trimmed.toInt();
    return true;
}

bool parsePinString(const String& text, int& value) {
    String lower = text;
    lower.trim();
    if (lower.isEmpty()) {
        return false;
    }
    lower.toLowerCase();
    if (lower == "none" || lower == "off") {
        value = -1;
        return true;
    }
    if (lower.startsWith("pcf")) {
        String suffix = lower.substring(3);
        suffix.trim();
        int idx = 0;
        if (!parseIntStrict(suffix, idx)) {
            return false;
        }
        if (idx < 0 || idx >= 16) {
            return false;
        }
        value = Config::pinFromPcfIndex(idx);
        return true;
    }
    return parseIntStrict(lower, value);
}
}  // namespace

void ControlServer::begin(WifiManager* wifi,
                          Config::RuntimeConfig* config,
                          Storage::ConfigStore* store,
                          ApplyConfigCallback applyCallback,
                          Logging::SessionLogger* logger) {
    wifi_ = wifi;
    config_ = config;
    store_ = store;
    applyCallback_ = applyCallback;
    logger_ = logger;

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/api/config", HTTP_GET, [this]() { handleConfigGet(); });
    server_.on("/api/config", HTTP_POST, [this]() { handleConfigPost(); });
    server_.on("/api/control", HTTP_POST, [this]() { handleControlPost(); });
    server_.on("/api/config/export", HTTP_GET, [this]() { handleConfigExport(); });
    server_.on("/api/config/import", HTTP_POST, [this]() { handleConfigImport(); });
    server_.on("/api/logs", HTTP_GET, [this]() {
        if (server_.hasArg("format") && server_.arg("format") == "csv") {
            auto entries = logger_ ? logger_->entries() : std::vector<Logging::LogEntry>{};
            String csv = "epoch,steering,throttle,hazard,mode,battery\n";
            for (const auto& e : entries) {
                csv += String(e.epoch) + "," + String(e.steering, 3) + "," + String(e.throttle, 3) + "," +
                       String(e.hazard ? 1 : 0) + "," + String(static_cast<int>(e.mode)) + "," + String(e.battery, 2) + "\n";
            }
            server_.send(200, "text/csv", csv);
        } else {
            auto entries = logger_ ? logger_->entries() : std::vector<Logging::LogEntry>{};
            String json = "[";
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                json += "{\"epoch\":" + String(e.epoch) + ",\"steering\":" + String(e.steering, 3) + ",\"throttle\":" + String(e.throttle, 3) +
                        ",\"hazard\":" + String(e.hazard ? 1 : 0) + ",\"mode\":" + String(static_cast<int>(e.mode)) +
                        ",\"battery\":" + String(e.battery, 2) + "}";
                if (i + 1 < entries.size()) {
                    json += ",";
                }
            }
            json += "]";
            sendJson(json);
        }
    });
    server_.begin();
}

void ControlServer::loop() {
    server_.handleClient();
}

void ControlServer::updateState(const ControlState& state) {
    state_ = state;
}

Overrides ControlServer::getOverrides() const {
    return overrides_;
}

void ControlServer::clearOverrides() {
    overrides_ = {};
}

void ControlServer::notifyConfigApplied() {
    // nothing for now, placeholder if we need to refresh caches later
}

void ControlServer::handleRoot() {
    String page = CONTROL_PAGE_TEMPLATE;
    server_.send(200, "text/html", page);
}

void ControlServer::handleStatus() {
    sendJson(buildStatusJson());
}

void ControlServer::handleConfigGet() {
    sendJson(buildConfigJson());
}

void ControlServer::handleConfigExport() {
    sendJson(buildConfigJson(true));
}

void ControlServer::handleConfigImport() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }

    String body = server_.arg("plain");
    JsonStream parser(body);
    bool changed = false;

    auto copyString = [](const String& src, char* dest, size_t len) {
        if (len == 0) {
            return;
        }
        src.toCharArray(dest, len);
        dest[len - 1] = '\0';
    };

    auto assignPinArg = [&](const char* name, int& target, bool allowPcf) {
        if (!server_.hasArg(name)) {
            return;
        }
        String raw = server_.arg(name);
        raw.trim();
        if (raw.isEmpty()) {
            return;
        }
        int parsed = target;
        bool ok = allowPcf ? parsePinString(raw, parsed) : parseIntStrict(raw, parsed);
        if (!ok) {
            return;
        }
        target = parsed;
        changed = true;
    };

    bool ok = parser.parseObject([&](const String& key) {
        if (key == "wifi") {
            return parser.parseObject([&](const String& wifiKey) {
                String value;
                if (wifiKey == "ssid") {
                    if (!parser.parseString(value)) return false;
                    copyString(value, config_->wifi.ssid, sizeof(config_->wifi.ssid));
                    changed = true;
                    return true;
                }
                if (wifiKey == "password") {
                    if (!parser.parseString(value)) return false;
                    copyString(value, config_->wifi.password, sizeof(config_->wifi.password));
                    changed = true;
                    return true;
                }
                if (wifiKey == "apSsid") {
                    if (!parser.parseString(value)) return false;
                    copyString(value, config_->wifi.apSsid, sizeof(config_->wifi.apSsid));
                    changed = true;
                    return true;
                }
                if (wifiKey == "apPassword") {
                    if (!parser.parseString(value)) return false;
                    copyString(value, config_->wifi.apPassword, sizeof(config_->wifi.apPassword));
                    changed = true;
                    return true;
                }
                return parser.skipValue();
            });
        }
        if (key == "features") {
            return parser.parseObject([&](const String& featureKey) {
                bool value = false;
                if (featureKey == "lighting") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.lightsEnabled = value;
                    changed = true;
                    return true;
                }
                if (featureKey == "sound") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.soundEnabled = value;
                    changed = true;
                    return true;
                }
                if (featureKey == "sensors") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.sensorsEnabled = value;
                    changed = true;
                    return true;
                }
                if (featureKey == "wifi") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.wifiEnabled = value;
                    changed = true;
                    return true;
                }
                if (featureKey == "ultrasonic") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.ultrasonicEnabled = value;
                    changed = true;
                    return true;
                }
                if (featureKey == "tip") {
                    if (!parser.parseBool(value)) return false;
                    config_->features.tipOverEnabled = value;
                    changed = true;
                    return true;
                }
                return parser.skipValue();
            });
        }
        if (key == "lighting") {
            return parser.parseObject([&](const String& lightKey) {
                if (lightKey == "pcaAddress") {
                    int addr = 0;
                    if (!parser.parseInt(addr)) return false;
                    if (addr >= 0 && addr <= 127) {
                        config_->lighting.pcaAddress = static_cast<std::uint8_t>(addr);
                        changed = true;
                    }
                    return true;
                }
                if (lightKey == "pwmFrequency") {
                    int freq = 0;
                    if (!parser.parseInt(freq)) return false;
                    if (freq > 0) {
                        config_->lighting.pwmFrequency = static_cast<std::uint16_t>(freq);
                        changed = true;
                    }
                    return true;
                }
                if (lightKey == "blink") {
                    return parser.parseObject([&](const String& blinkKey) {
                        if (blinkKey == "wifi") {
                            bool val = false;
                            if (!parser.parseBool(val)) return false;
                            config_->lighting.blink.wifi = val;
                            changed = true;
                            return true;
                        }
                        if (blinkKey == "rc") {
                            bool val = false;
                            if (!parser.parseBool(val)) return false;
                            config_->lighting.blink.rc = val;
                            changed = true;
                            return true;
                        }
                        if (blinkKey == "period") {
                            int period = 0;
                            if (!parser.parseInt(period)) return false;
                            if (period > 0) {
                                config_->lighting.blink.periodMs = static_cast<std::uint16_t>(period);
                                changed = true;
                            }
                            return true;
                        }
                        return parser.skipValue();
                    });
                }
                if (lightKey == "channels") {
                    auto parseRgb = [&](Config::RgbChannel& rgb) {
                        return parser.parseObject([&](const String& rgbKey) {
                            int value = 0;
                            if (!parser.parseInt(value)) return false;
                            if (rgbKey == "r") rgb.r = value;
                            else if (rgbKey == "g") rgb.g = value;
                            else if (rgbKey == "b") rgb.b = value;
                            else return false;
                            changed = true;
                            return true;
                        });
                    };
                    return parser.parseObject([&](const String& rgbKey) {
                        if (rgbKey == "frontLeft") return parseRgb(config_->lighting.channels.frontLeft);
                        if (rgbKey == "frontRight") return parseRgb(config_->lighting.channels.frontRight);
                        if (rgbKey == "rearLeft") return parseRgb(config_->lighting.channels.rearLeft);
                        if (rgbKey == "rearRight") return parseRgb(config_->lighting.channels.rearRight);
                        return parser.skipValue();
                    });
                }
                return parser.skipValue();
            });
        }
        if (key == "pins") {
            return parser.parseObject([&](const String& pinKey) {
                if (pinKey == "leftDriver") {
                    return parser.parseObject([&](const String& driverKey) {
                        if (driverKey == "motorA") {
                            bool ok = parser.parseObject([&](const String& chanKey) {
                                int value = 0;
                                if (!parser.parseInt(value)) return false;
                                if (chanKey == "pwm") config_->pins.leftDriver.motorA.pwm = value;
                                else if (chanKey == "in1") config_->pins.leftDriver.motorA.in1 = value;
                                else if (chanKey == "in2") config_->pins.leftDriver.motorA.in2 = value;
                                else return false;
                                changed = true;
                                return true;
                            });
                            return ok;
                        }
                        if (driverKey == "motorB") {
                            return parser.parseObject([&](const String& chanKey) {
                                int value = 0;
                                if (!parser.parseInt(value)) return false;
                                if (chanKey == "pwm") config_->pins.leftDriver.motorB.pwm = value;
                                else if (chanKey == "in1") config_->pins.leftDriver.motorB.in1 = value;
                                else if (chanKey == "in2") config_->pins.leftDriver.motorB.in2 = value;
                                else return false;
                                changed = true;
                                return true;
                            });
                        }
                        if (driverKey == "standby") {
                            int value = 0;
                            if (!parser.parseInt(value)) return false;
                            config_->pins.leftDriver.standby = value;
                            changed = true;
                            return true;
                        }
                        return parser.skipValue();
                    });
                }
                if (pinKey == "rightDriver") {
                    return parser.parseObject([&](const String& driverKey) {
                        if (driverKey == "motorA") {
                            return parser.parseObject([&](const String& chanKey) {
                                int value = 0;
                                if (!parser.parseInt(value)) return false;
                                if (chanKey == "pwm") config_->pins.rightDriver.motorA.pwm = value;
                                else if (chanKey == "in1") config_->pins.rightDriver.motorA.in1 = value;
                                else if (chanKey == "in2") config_->pins.rightDriver.motorA.in2 = value;
                                else return false;
                                changed = true;
                                return true;
                            });
                        }
                        if (driverKey == "motorB") {
                            return parser.parseObject([&](const String& chanKey) {
                                int value = 0;
                                if (!parser.parseInt(value)) return false;
                                if (chanKey == "pwm") config_->pins.rightDriver.motorB.pwm = value;
                                else if (chanKey == "in1") config_->pins.rightDriver.motorB.in1 = value;
                                else if (chanKey == "in2") config_->pins.rightDriver.motorB.in2 = value;
                                else return false;
                                changed = true;
                                return true;
                            });
                        }
                        if (driverKey == "standby") {
                            int value = 0;
                            if (!parser.parseInt(value)) return false;
                            config_->pins.rightDriver.standby = value;
                            changed = true;
                            return true;
                        }
                        return parser.skipValue();
                    });
                }
                if (pinKey == "lightBar") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->pins.lightBar = value;
                    changed = true;
                    return true;
                }
                if (pinKey == "speaker") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->pins.speaker = value;
                    changed = true;
                    return true;
                }
                if (pinKey == "batterySense") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->pins.batterySense = value;
                    changed = true;
                    return true;
                }
                if (pinKey == "pcfAddress") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->pins.pcfAddress = value;
                    changed = true;
                    return true;
                }
                return parser.skipValue();
            });
        }
        if (key == "rcPins") {
            return parser.parseArray([&](size_t index) {
                int value = 0;
                if (!parser.parseInt(value)) return false;
                if (index < std::size(config_->rc.channelPins)) {
                    config_->rc.channelPins[index] = value;
                    changed = true;
                }
                return true;
            });
        }
        if (key == "ntp") {
            return parser.parseObject([&](const String& ntpKey) {
                if (ntpKey == "server") {
                    String value;
                    if (!parser.parseString(value)) return false;
                    copyString(value, config_->ntp.server, sizeof(config_->ntp.server));
                    changed = true;
                    return true;
                }
                if (ntpKey == "gmtOffsetSeconds") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->ntp.gmtOffsetSeconds = value;
                    changed = true;
                    return true;
                }
                if (ntpKey == "daylightOffsetSeconds") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    config_->ntp.daylightOffsetSeconds = value;
                    changed = true;
                    return true;
                }
                return parser.skipValue();
            });
        }
        if (key == "logging") {
            return parser.parseObject([&](const String& logKey) {
                if (logKey == "enabled") {
                    bool value = false;
                    if (!parser.parseBool(value)) return false;
                    config_->logging.enabled = value;
                    changed = true;
                    return true;
                }
                if (logKey == "maxEntries") {
                    int value = 0;
                    if (!parser.parseInt(value)) return false;
                    if (value > 0) {
                        config_->logging.maxEntries = static_cast<std::uint16_t>(value);
                        changed = true;
                    }
                    return true;
                }
                return parser.skipValue();
            });
        }

        return parser.skipValue();
    });

    if (!ok) {
        server_.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    if (changed) {
        if (store_) {
            store_->save(*config_);
        }
        if (applyCallback_) {
            applyCallback_();
        }
    }

    sendJson("{\"ok\":true}");
}
void ControlServer::handleConfigPost() {
    bool changed = false;
    auto assignPinArg = [&](const char* name, int& target, bool allowPcf) {
        if (!server_.hasArg(name)) {
            return;
        }
        String raw = server_.arg(name);
        raw.trim();
        if (raw.isEmpty()) {
            return;
        }
        int parsed = target;
        bool ok = allowPcf ? parsePinString(raw, parsed) : parseIntStrict(raw, parsed);
        if (!ok) {
            return;
        }
        target = parsed;
        changed = true;
    };
    auto copyString = [](char* dest, size_t len, const String& src) {
        if (len == 0) {
            return;
        }
        size_t copyLen = std::min(len - 1, static_cast<size_t>(src.length()));
        memcpy(dest, src.c_str(), copyLen);
        dest[copyLen] = '\0';
    };
    if (server_.hasArg("lightingEnabled")) {
        const bool val = server_.arg("lightingEnabled") == "1";
        if (config_->features.lightsEnabled != val) {
            config_->features.lightsEnabled = val;
            changed = true;
        }
    }
    if (server_.hasArg("soundEnabled")) {
        const bool val = server_.arg("soundEnabled") == "1";
        if (config_->features.soundEnabled != val) {
            config_->features.soundEnabled = val;
            changed = true;
        }
    }
    if (server_.hasArg("wifiEnabled")) {
        const bool val = server_.arg("wifiEnabled") == "1";
        if (config_->features.wifiEnabled != val) {
            config_->features.wifiEnabled = val;
            changed = true;
        }
    }
    if (server_.hasArg("ultrasonicEnabled")) {
        const bool val = server_.arg("ultrasonicEnabled") == "1";
        if (config_->features.ultrasonicEnabled != val) {
            config_->features.ultrasonicEnabled = val;
            changed = true;
        }
    }
    if (server_.hasArg("tipEnabled")) {
        const bool val = server_.arg("tipEnabled") == "1";
        if (config_->features.tipOverEnabled != val) {
            config_->features.tipOverEnabled = val;
            changed = true;
        }
    }
    if (server_.hasArg("pcaAddress")) {
        const int addr = server_.arg("pcaAddress").toInt();
        if (addr >= 0 && addr <= 127 && config_->lighting.pcaAddress != addr) {
            config_->lighting.pcaAddress = static_cast<std::uint8_t>(addr);
            changed = true;
        }
    }
    if (server_.hasArg("pwmFrequency")) {
        const int freq = server_.arg("pwmFrequency").toInt();
        if (freq > 0 && freq != config_->lighting.pwmFrequency) {
            config_->lighting.pwmFrequency = static_cast<std::uint16_t>(freq);
            changed = true;
        }
    }
    if (server_.hasArg("blinkWifi")) {
        const bool val = server_.arg("blinkWifi") == "1";
        if (config_->lighting.blink.wifi != val) {
            config_->lighting.blink.wifi = val;
            changed = true;
        }
    }
    if (server_.hasArg("blinkRc")) {
        const bool val = server_.arg("blinkRc") == "1";
        if (config_->lighting.blink.rc != val) {
            config_->lighting.blink.rc = val;
            changed = true;
        }
    }
    if (server_.hasArg("blinkPeriod")) {
        const int val = server_.arg("blinkPeriod").toInt();
        if (val > 0 && val != config_->lighting.blink.periodMs) {
            config_->lighting.blink.periodMs = static_cast<std::uint16_t>(val);
            changed = true;
        }
    }

    if (server_.hasArg("ssid")) {
        const String ssid = server_.arg("ssid");
        if (ssid.length() < sizeof(config_->wifi.ssid)) {
            copyString(config_->wifi.ssid, sizeof(config_->wifi.ssid), ssid);
            changed = true;
        }
    }
    if (server_.hasArg("password")) {
        const String pass = server_.arg("password");
        if (pass.length() > 0) {
            copyString(config_->wifi.password, sizeof(config_->wifi.password), pass);
            changed = true;
        }
    }
    if (server_.hasArg("apSsid")) {
        const String ssid = server_.arg("apSsid");
        if (ssid.length() < sizeof(config_->wifi.apSsid)) {
            copyString(config_->wifi.apSsid, sizeof(config_->wifi.apSsid), ssid);
            changed = true;
        }
    }
    if (server_.hasArg("apPassword")) {
        const String pass = server_.arg("apPassword");
        if (pass.length() > 0) {
            copyString(config_->wifi.apPassword, sizeof(config_->wifi.apPassword), pass);
            changed = true;
        }
    }

    assignPinArg("leftMotorA_pwm", config_->pins.leftDriver.motorA.pwm, false);
    assignPinArg("leftMotorA_in1", config_->pins.leftDriver.motorA.in1, true);
    assignPinArg("leftMotorA_in2", config_->pins.leftDriver.motorA.in2, true);
    assignPinArg("leftMotorB_pwm", config_->pins.leftDriver.motorB.pwm, false);
    assignPinArg("leftMotorB_in1", config_->pins.leftDriver.motorB.in1, true);
    assignPinArg("leftMotorB_in2", config_->pins.leftDriver.motorB.in2, true);
    assignPinArg("leftDriver_stby", config_->pins.leftDriver.standby, true);
    assignPinArg("rightMotorA_pwm", config_->pins.rightDriver.motorA.pwm, false);
    assignPinArg("rightMotorA_in1", config_->pins.rightDriver.motorA.in1, true);
    assignPinArg("rightMotorA_in2", config_->pins.rightDriver.motorA.in2, true);
    assignPinArg("rightMotorB_pwm", config_->pins.rightDriver.motorB.pwm, false);
    assignPinArg("rightMotorB_in1", config_->pins.rightDriver.motorB.in1, true);
    assignPinArg("rightMotorB_in2", config_->pins.rightDriver.motorB.in2, true);
    assignPinArg("rightDriver_stby", config_->pins.rightDriver.standby, true);
    assignPinArg("light_pin", config_->pins.lightBar, true);
    assignPinArg("speaker_pin", config_->pins.speaker, true);
    assignPinArg("battery_pin", config_->pins.batterySense, true);
    assignPinArg("pcfAddress", config_->pins.pcfAddress, false);
    assignPinArg("slave_tx", config_->pins.slaveTx, false);
    assignPinArg("slave_rx", config_->pins.slaveRx, false);
    for (size_t i = 0; i < std::size(config_->rc.channelPins); ++i) {
        String arg = "rc";
        arg += (i + 1);
        assignPinArg(arg.c_str(), config_->rc.channelPins[i], false);
    }

    if (changed) {
        if (store_) {
            store_->save(*config_);
        }
        if (applyCallback_) {
            applyCallback_();
        }
    }

    sendJson("{\"ok\":true}");
}

void ControlServer::handleControlPost() {
    if (server_.hasArg("clear")) {
        overrides_ = {};
        sendJson("{\"ok\":true}");
        return;
    }
    if (server_.hasArg("hazardOverride")) {
        overrides_.hazardOverride = server_.arg("hazardOverride") == "1";
        if (overrides_.hazardOverride && server_.hasArg("hazard")) {
            overrides_.hazardEnabled = server_.arg("hazard") == "1";
        }
    }
    if (server_.hasArg("lightsOverride")) {
        overrides_.lightsOverride = server_.arg("lightsOverride") == "1";
        if (overrides_.lightsOverride && server_.hasArg("lights")) {
            overrides_.lightsEnabled = server_.arg("lights") == "1";
        }
    }
    sendJson("{\"ok\":true}");
}

String ControlServer::buildStatusJson() const {
    String json = "{";
    json += "\"steering\":" + String(state_.steering, 3) + ',';
    json += "\"throttle\":" + String(state_.throttle, 3) + ',';
    json += "\"hazard\":" + String(state_.hazard ? 1 : 0) + ',';
    json += "\"lighting\":" + String(state_.lighting ? 1 : 0) + ',';
    json += "\"mode\":\"" + modeToString(state_.mode) + "\",";
    json += "\"modeClass\":\"" + modeClass(state_.mode) + "\",";
    json += "\"rcLink\":" + String(state_.rcLinked ? 1 : 0) + ',';
    json += "\"wifiLink\":" + String(state_.wifiLinked ? 1 : 0) + ',';
    json += "\"ultraLeft\":" + String(state_.ultrasonicLeft, 3) + ',';
    json += "\"ultraRight\":" + String(state_.ultrasonicRight, 3) + ',';
    json += "\"ip\":\"" + escapeJson(wifi_ ? wifi_->ipAddress() : String("")) + "\",";
    json += "\"ap\":\"" + escapeJson(wifi_ ? wifi_->apAddress() : String("")) + "\",";
    json += "\"overrideHazard\":" + String(overrides_.hazardOverride ? 1 : 0) + ',';
    json += "\"overrideLights\":" + String(overrides_.lightsOverride ? 1 : 0) + ",";
    const auto& health = Health::getStatus();
    json += "\"health\":{\"code\":" + String(static_cast<int>(health.code)) + ",\"message\":\"" + escapeJson(String(health.message)) + "\",\"ts\":" + String(health.lastChangeMs) + "},";
    json += "\"logCount\":" + String(logger_ ? logger_->size() : 0) + ",";
    json += "\"serverTime\":" + String(state_.serverTime);
    json += "}";
    return json;
}

String ControlServer::buildConfigJson(bool includeSensitive) const {
    auto rgbToJson = [&](const Config::RgbChannel& rgb) {
        return "{\"r\":" + String(rgb.r) + ",\"g\":" + String(rgb.g) + ",\"b\":" + String(rgb.b) + "}";
    };

    String json = "{";
    json += "\"wifi\":{";
    json += "\"ssid\":\"" + escapeJson(String(config_->wifi.ssid)) + "\",";
    json += "\"apSsid\":\"" + escapeJson(String(config_->wifi.apSsid)) + "\"";
    if (includeSensitive) {
        json += ",\"password\":\"" + escapeJson(String(config_->wifi.password)) + "\"";
        json += ",\"apPassword\":\"" + escapeJson(String(config_->wifi.apPassword)) + "\"";
    }
    json += "},";

    json += "\"features\":{";
    json += "\"lighting\":" + String(config_->features.lightsEnabled ? 1 : 0) + ",";
    json += "\"sound\":" + String(config_->features.soundEnabled ? 1 : 0) + ",";
    json += "\"sensors\":" + String(config_->features.sensorsEnabled ? 1 : 0) + ",";
    json += "\"wifi\":" + String(config_->features.wifiEnabled ? 1 : 0) + ",";
    json += "\"ultrasonic\":" + String(config_->features.ultrasonicEnabled ? 1 : 0) + ",";
    json += "\"tip\":" + String(config_->features.tipOverEnabled ? 1 : 0);
    json += "},";

    json += "\"lighting\":{";
    json += "\"pcaAddress\":" + String(config_->lighting.pcaAddress) + ",";
    json += "\"pwmFrequency\":" + String(config_->lighting.pwmFrequency) + ",";
    json += "\"channels\":{";
    json += "\"frontLeft\":" + rgbToJson(config_->lighting.channels.frontLeft) + ",";
    json += "\"frontRight\":" + rgbToJson(config_->lighting.channels.frontRight) + ",";
    json += "\"rearLeft\":" + rgbToJson(config_->lighting.channels.rearLeft) + ",";
    json += "\"rearRight\":" + rgbToJson(config_->lighting.channels.rearRight);
    json += "},";
    json += "\"blink\":{";
    json += "\"wifi\":" + String(config_->lighting.blink.wifi ? 1 : 0) + ",";
    json += "\"rc\":" + String(config_->lighting.blink.rc ? 1 : 0) + ",";
    json += "\"period\":" + String(config_->lighting.blink.periodMs);
    json += "}";
    json += "},";

    json += "\"pins\":{";
    auto channelJson = [&](const Config::ChannelPins& ch) {
        return "{\"pwm\":" + String(ch.pwm) + ",\"in1\":" + String(ch.in1) + ",\"in2\":" + String(ch.in2) + "}";
    };
    json += "\"leftDriver\":{";
    json += "\"motorA\":" + channelJson(config_->pins.leftDriver.motorA) + ",";
    json += "\"motorB\":" + channelJson(config_->pins.leftDriver.motorB) + ",";
    json += "\"standby\":" + String(config_->pins.leftDriver.standby);
    json += "},";
    json += "\"rightDriver\":{";
    json += "\"motorA\":" + channelJson(config_->pins.rightDriver.motorA) + ",";
    json += "\"motorB\":" + channelJson(config_->pins.rightDriver.motorB) + ",";
    json += "\"standby\":" + String(config_->pins.rightDriver.standby);
    json += "},";
    json += "\"lightBar\":" + String(config_->pins.lightBar) + ",";
    json += "\"speaker\":" + String(config_->pins.speaker) + ",";
    json += "\"batterySense\":" + String(config_->pins.batterySense) + ",";
    json += "\"pcfAddress\":" + String(config_->pins.pcfAddress);
    json += "},";

    json += "\"rcPins\":[";
    for (std::size_t i = 0; i < std::size(config_->rc.channelPins); ++i) {
        json += String(config_->rc.channelPins[i]);
        if (i + 1 < std::size(config_->rc.channelPins)) {
            json += ",";
        }
    }
    json += "],";

    json += "\"ntp\":{";
    json += "\"server\":\"" + escapeJson(String(config_->ntp.server)) + "\",";
    json += "\"gmtOffsetSeconds\":" + String(config_->ntp.gmtOffsetSeconds) + ",";
    json += "\"daylightOffsetSeconds\":" + String(config_->ntp.daylightOffsetSeconds);
    json += "},";

    json += "\"logging\":{";
    json += "\"enabled\":" + String(config_->logging.enabled ? 1 : 0) + ",";
    json += "\"maxEntries\":" + String(config_->logging.maxEntries);
    json += "}";

    json += "}";
    return json;
}

String ControlServer::buildPinSchemaJson() const {
    String json = "[";
    for (std::size_t i = 0; i < Config::kPinSchemaCount; ++i) {
        const auto& entry = Config::kPinSchema[i];
        json += "{";
        json += "\"board\":\"" + escapeJson(String(entry.board)) + "\",";
        json += "\"path\":\"" + escapeJson(String(entry.path)) + "\",";
        json += "\"token\":\"" + escapeJson(String(entry.token)) + "\",";
        json += "\"label\":\"" + escapeJson(String(entry.label)) + "\",";
        json += "\"owner\":\"" + escapeJson(String(entry.owner)) + "\",";
        json += "\"description\":\"" + escapeJson(String(entry.description)) + "\",";
        json += "\"type\":\"" + escapeJson(String(entry.type)) + "\",";
        json += "\"hint\":\"" + escapeJson(String(entry.hint)) + "\",";
        json += "\"allowPcf\":" + String(entry.allowPcf ? 1 : 0) + ",";
        json += "\"gpioOnly\":" + String(entry.gpioOnly ? 1 : 0) + ",";
        json += "\"defaultPin\":" + String(entry.defaultPin) + ",";
        json += "\"minValue\":" + String(entry.minValue) + ",";
        json += "\"maxValue\":" + String(entry.maxValue);
        json += "}";
        if (i + 1 < Config::kPinSchemaCount) {
            json += ",";
        }
    }
    json += "]";
    return json;
}

void ControlServer::sendJson(const String& body) {
    server_.send(200, "application/json", body);
}
}  // namespace TankRC::Network
