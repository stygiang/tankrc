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

const char CONTROL_PAGE_TEMPLATE[] PROGMEM = R"HTML(R"HTML(<!DOCTYPE html>
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
    --warn:#ffb703;
    --danger:#ff3864;
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
    max-width:1200px;
    margin:0 auto 3rem;
    padding:1rem;
}
header.hero {
    background:var(--panel);
    margin:1rem;
    border-radius:18px;
    padding:1.25rem 2rem;
    display:flex;
    justify-content:space-between;
    align-items:center;
    gap:1rem;
    box-shadow:0 20px 50px rgba(0,0,0,0.35);
}
.hero h1 { margin:0; font-size:1.8rem; }
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
.panel h2 { margin:0; font-size:1.25rem; }
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
.feature-card label { display:flex; align-items:center; justify-content:space-between; }
.feature-card input[type="checkbox"] { transform:scale(1.2); margin-right:0.3rem; }
.feature-status { font-size:0.85rem; font-weight:600; letter-spacing:0.04em; }
.feature-status.enabled { color:var(--accent); }
.feature-status.disabled { color:#f4f6ff; opacity:0.6; }
.test-panel ul { margin:0.75rem 0 0; padding-left:1rem; color:var(--muted); }
.tabs { display:flex; gap:0.5rem; }
.tabs button {
    border:none;
    background:rgba(255,255,255,0.05);
    color:var(--text);
    padding:0.45rem 1rem;
    border-radius:999px;
    cursor:pointer;
    transition:background 0.2s ease;
}
.tabs button.active { background:var(--accent); color:#021214; }
.pin-grid {
    margin-top:1.25rem;
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(260px,1fr));
    gap:1rem;
}
.pin-card {
    background:rgba(255,255,255,0.02);
    border:1px solid rgba(255,255,255,0.07);
    border-radius:16px;
    padding:1rem;
    display:flex;
    flex-direction:column;
    gap:0.5rem;
    position:relative;
}
.pin-card__head {
    display:flex;
    justify-content:space-between;
    align-items:flex-start;
    gap:0.5rem;
}
.pin-card__head strong { font-size:1rem; }
.pin-card__owner { font-size:0.8rem; color:var(--muted); }
.pin-card__value { font-size:0.9rem; font-weight:600; }
.pin-card__desc { margin:0; color:var(--muted); font-size:0.85rem; }
.pin-card__input {
    display:flex;
    gap:0.5rem;
}
.pin-card__input input {
    flex:1;
    background:rgba(255,255,255,0.05);
    border:1px solid rgba(255,255,255,0.1);
    border-radius:10px;
    padding:0.45rem 0.75rem;
    color:var(--text);
}
.pin-card__input input::placeholder { color:rgba(255,255,255,0.4); }
.pin-card__input button {
    border:none;
    background:var(--accent);
    color:#021214;
    border-radius:10px;
    padding:0.4rem 0.9rem;
    cursor:pointer;
}
.pin-card__hint { color:var(--muted); font-size:0.75rem; margin:0; }
.pin-card__suggestions {
    display:flex;
    flex-wrap:wrap;
    gap:0.55rem;
    margin:0.75rem 0;
}
.pin-card__suggestions button {
    border:none;
    border-radius:14px;
    padding:0.55rem 1.1rem;
    background:rgba(255,255,255,0.08);
    color:var(--text);
    cursor:pointer;
    font-size:0.95rem;
    min-width:90px;
    text-align:center;
    transition:background 0.2s ease, transform 0.2s ease;
}
.pin-card__suggestions button:hover {
    background:rgba(255,255,255,0.18);
}
.pin-card__suggestions button.selected {
    background:var(--accent);
    color:#021214;
    transform:translateY(-1px);
}
.pin-card__overlay, .pin-card__board-popup {
    position:absolute;
    top:50%;
    left:50%;
    transform:translate(-50%,-50%);
    background:linear-gradient(180deg,rgba(2,7,17,0.97),rgba(5,17,32,0.97));
    border:1px solid rgba(59,227,164,0.35);
    border-radius:16px;
    padding:1.25rem;
    width:280px;
    text-align:center;
    box-shadow:0 25px 55px rgba(0,0,0,0.6);
    z-index:10;
}
.pin-card__overlay.hidden, .pin-card__board-popup.hidden {
    display:none;
}
.pin-card__overlay-buttons {
    display:flex;
    justify-content:space-between;
    gap:0.5rem;
    margin-top:0.8rem;
}
.pin-card__board {
    margin-top:0.6rem;
    border:none;
    background:rgba(255,255,255,0.12);
    color:var(--text);
    border-radius:12px;
    padding:0.55rem 0.9rem;
    cursor:pointer;
    transition:background 0.2s ease;
}
.pin-card__board:hover {
    background:rgba(255,255,255,0.18);
}
.pin-card__board-popup {
    display:flex;
    flex-direction:column;
    gap:0.65rem;
}
.pin-card__board-popup strong {
    display:block;
    font-size:1rem;
    margin-bottom:0.15rem;
}
.pin-card__board-popup p {
    margin:0;
    color:var(--muted);
    font-size:0.85rem;
}
.pin-card__board-options {
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(130px,1fr));
    gap:0.5rem;
}
.pin-card__board-option {
    border:none;
    border-radius:10px;
    padding:0.5rem 0.6rem;
    background:rgba(255,255,255,0.1);
    color:var(--text);
    cursor:pointer;
    min-height:42px;
}
.pin-card__board-option:hover {
    background:rgba(59,227,164,0.2);
}
.pin-card--static {
    background:rgba(59,227,164,0.04);
    border-color:rgba(59,227,164,0.35);
}
.pin-card--static .pin-card__value {
    color:var(--accent);
    font-weight:600;
}
.pin-card--static.free .pin-card__value {
    color:var(--muted);
}
.pin-card__static-meta {
    display:flex;
    justify-content:space-between;
    align-items:center;
    gap:0.35rem;
    font-size:0.8rem;
    color:var(--muted);
}
.pin-card__static-pill {
    border-radius:999px;
    padding:0.2rem 0.9rem;
    background:rgba(255,255,255,0.09);
}
.pin-card__message { min-height:1.25rem; font-size:0.8rem; }
.pin-card__message.success { color:var(--accent); }
.pin-card__message.error { color:var(--danger); }
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
    <div class="status-tags" id="statusList">
        <span class="status-pill" id="statusBadge">Connecting...</span>
    </div>
</header>
<main>
    <section class="panel">
        <header>
            <div>
                <h2>Feature toggles</h2>
                <p style="margin:0; color:var(--muted);">Flip lights, sound, sensors, Wi-Fi, and tip-over protection.</p>
            </div>
            <div class="status-pill">AP: sharc</div>
        </header>
        <div class="feature-grid" id="featureGrid"></div>
    </section>
    <section class="panel test-panel">
        <header>
            <div>
                <h2>Diagnostics & testing</h2>
                <p style="margin:0; color:var(--muted);">Use the serial console command <code>tests</code> to exercise motors, sound, and battery readings.</p>
            </div>
        </header>
        <ul>
            <li>Run <strong>Motor sweep</strong> to verify both tracks spin and pivot.</li>
            <li><strong>Sound pulse</strong> drives the speaker briefly for feedback.</li>
            <li><strong>Battery read</strong> reports voltage when requested.</li>
        </ul>
    </section>
    <section class="panel">
        <header>
            <div>
                <h2>Pin assignments</h2>
                <p style="margin:0; color:var(--muted);">Cards show current owners, allowed values, and whether PCA/PCF expanders are supported.</p>
            </div>
        <div class="tabs">
            <button data-board="master" class="active">Master ESP</button>
            <button data-board="slave">Slave ESP</button>
            <button data-board="pcf8575">PCF8575 Expander</button>
            <button data-board="pca9685">PCA9685 PWM</button>
        </div>
        </header>
        <div class="pin-grid" id="pinGrid"></div>
    </section>
</main>
<div class="toast" id="toast"></div>
<script>
const pinSchema = __PIN_SCHEMA_JSON__;
const featureFields = [
    { key: 'lightingEnabled', label: 'Lighting', description: 'Light bar channels and blink patterns.' },
    { key: 'soundEnabled', label: 'Sound', description: 'Speaker output and FX engine.' },
    { key: 'sensorsEnabled', label: 'Sensors', description: 'Ultrasonic and tip sensors.' },
    { key: 'wifiEnabled', label: 'Wi-Fi', description: 'Enable station/AP networking.' },
    { key: 'ultrasonicEnabled', label: 'Ultrasonic', description: 'Allow ultrasonic range sensors.' },
    { key: 'tipOverEnabled', label: 'Tip-over', description: 'Enable tip-over protection routines.' },
];
let config = null;
let activeBoard = 'master';
const featureGrid = document.getElementById('featureGrid');
const pinGrid = document.getElementById('pinGrid');
const toast = document.getElementById('toast');
const boardButtons = document.querySelectorAll('[data-board]');
const statusBadge = document.getElementById('statusBadge');
const refreshIntervalMs = 4000;
const pinInputState = {};
const boardOverrides = loadBoardOverrides();
const boardList = Array.from(new Set(pinSchema.map(entry => entry.board)));
const boardTypeMap = {
    master: 'schema',
    slave: 'schema',
    pcf8575: 'pcf',
    pca9685: 'pca',
};
const suggestionSelections = {};
const boardPopupState = { token: null };
let activeInputSnapshot = null;
const pcaChannelSpecs = [
    { path: 'lighting.channels.frontLeft.r', label: 'Front-left red', owner: 'Lighting · Front-left', description: 'Red PWM for the front-left headlights.', type: 'RGB component' },
    { path: 'lighting.channels.frontLeft.g', label: 'Front-left green', owner: 'Lighting · Front-left', description: 'Green PWM for the front-left headlights.', type: 'RGB component' },
    { path: 'lighting.channels.frontLeft.b', label: 'Front-left blue', owner: 'Lighting · Front-left', description: 'Blue PWM for the front-left headlights.', type: 'RGB component' },
    { path: 'lighting.channels.frontRight.r', label: 'Front-right red', owner: 'Lighting · Front-right', description: 'Red PWM for the front-right headlights.', type: 'RGB component' },
    { path: 'lighting.channels.frontRight.g', label: 'Front-right green', owner: 'Lighting · Front-right', description: 'Green PWM for the front-right headlights.', type: 'RGB component' },
    { path: 'lighting.channels.frontRight.b', label: 'Front-right blue', owner: 'Lighting · Front-right', description: 'Blue PWM for the front-right headlights.', type: 'RGB component' },
    { path: 'lighting.channels.rearLeft.r', label: 'Rear-left red', owner: 'Lighting · Rear-left', description: 'Red PWM for the rear-left reverse lights.', type: 'RGB component' },
    { path: 'lighting.channels.rearLeft.g', label: 'Rear-left green', owner: 'Lighting · Rear-left', description: 'Green PWM for the rear-left reverse lights.', type: 'RGB component' },
    { path: 'lighting.channels.rearLeft.b', label: 'Rear-left blue', owner: 'Lighting · Rear-left', description: 'Blue PWM for the rear-left reverse lights.', type: 'RGB component' },
    { path: 'lighting.channels.rearRight.r', label: 'Rear-right red', owner: 'Lighting · Rear-right', description: 'Red PWM for the rear-right reverse lights.', type: 'RGB component' },
    { path: 'lighting.channels.rearRight.g', label: 'Rear-right green', owner: 'Lighting · Rear-right', description: 'Green PWM for the rear-right reverse lights.', type: 'RGB component' },
    { path: 'lighting.channels.rearRight.b', label: 'Rear-right blue', owner: 'Lighting · Rear-right', description: 'Blue PWM for the rear-right reverse lights.', type: 'RGB component' },
];

function showToast(message, tone = 'info') {
    toast.textContent = message;
    toast.style.borderColor = tone === 'danger' ? 'rgba(255,56,100,0.6)' : tone === 'warn' ? 'rgba(255,183,3,0.6)' : 'rgba(59,227,164,0.6)';
    toast.classList.add('show');
    clearTimeout(toast.dataset.timeout);
    toast.dataset.timeout = setTimeout(() => toast.classList.remove('show'), 2800);
}

function formatPinValue(value) {
    if (typeof value !== 'number') {
        return 'unknown';
    }
    if (value <= -2) {
        return 'pcf' + (-value - 2);
    }
    if (value === -1) {
        return 'unassigned';
    }
    return String(value);
}

function formatPinKey(value) {
    if (value === undefined || value === null) {
        return '';
    }
    if (typeof value === 'number') {
        if (value <= -2) {
            return `pcf${-value - 2}`;
        }
        return String(value);
    }
    return String(value);
}

function loadBoardOverrides() {
    try {
        const stored = localStorage.getItem('pinBoardOverride');
        return stored ? JSON.parse(stored) : {};
    } catch (e) {
        return {};
    }
}

function saveBoardOverrides() {
    localStorage.setItem('pinBoardOverride', JSON.stringify(boardOverrides));
}

function getEntryBoard(entry) {
    return boardOverrides[entry.token] || entry.board;
}

function gatherAssignedPins() {
    const map = new Map();
    for (const entry of pinSchema) {
        const value = getValueFromPath(entry.path);
        if (value === null || value === undefined) {
            continue;
        }
        if (value === -1) {
            continue;
        }
        const key = formatPinKey(value);
        if (!key) {
            continue;
        }
        map.set(key, entry);
    }
    return map;
}

function getValueFromPath(path) {
    if (!config) {
        return undefined;
    }
    const parts = path.split('.');
    let cursor = config;
    for (const part of parts) {
        if (cursor == null) {
            return undefined;
        }
        if (/^\d+$/.test(part)) {
            cursor = cursor[Number(part)];
        } else {
            cursor = cursor[part];
        }
    }
    return cursor;
}

function renderFeatureToggles() {
    if (!config) {
        featureGrid.innerHTML = '';
        return;
    }
    featureGrid.innerHTML = '';
    for (const field of featureFields) {
        const card = document.createElement('article');
        card.className = 'feature-card';
        card.innerHTML = `
            <strong>${field.label}</strong>
            <small>${field.description}</small>
            <label>
                <span class="feature-status ${config.features[field.key] ? 'enabled' : 'disabled'}">
                    ${config.features[field.key] ? 'Enabled' : 'Disabled'}
                </span>
                <input type="checkbox" data-field="${field.key}" />
            </label>`;
        const input = card.querySelector('input');
        const status = card.querySelector('.feature-status');
        input.checked = !!config.features[field.key];
        input.addEventListener('change', () => {
            postConfig({ [field.key]: input.checked ? '1' : '0' })
                .then(refreshConfig)
                .then(() => {
                    status.textContent = input.checked ? 'Enabled' : 'Disabled';
                    status.classList.toggle('enabled', input.checked);
                    status.classList.toggle('disabled', !input.checked);
                    showToast(`${field.label} ${input.checked ? 'enabled' : 'disabled'}`);
                })
                .catch(err => showToast(err.message, 'danger'));
        });
        featureGrid.appendChild(card);
    }
}

function validatePinValue(entry, raw) {
    const trimmed = raw.trim().toLowerCase();
    if (!trimmed) {
        return { valid: false, message: 'Type a GPIO number, pcf#, or "none".' };
    }
    if (trimmed === 'none' || trimmed === 'off') {
        return { valid: true, value: 'none' };
    }
    if (trimmed.startsWith('pcf')) {
        if (!entry.allowPcf) {
            return { valid: false, message: 'This signal must stay on a GPIO pin.' };
        }
        const idx = Number(trimmed.substring(3).trim());
        if (!Number.isInteger(idx) || idx < 0 || idx > 15) {
            return { valid: false, message: 'Use pcf0..pcf15 for expander channels.' };
        }
        return { valid: true, value: `pcf${idx}` };
    }
    const number = Number(trimmed);
    if (!Number.isInteger(number)) {
        return { valid: false, message: 'Use a whole GPIO number or pcf#.' };
    }
    if (entry.minValue >= 0 && number < entry.minValue) {
        return { valid: false, message: `Value must be ≥ ${entry.minValue}.` };
    }
    if (entry.maxValue >= 0 && number > entry.maxValue) {
        return { valid: false, message: `Value must be ≤ ${entry.maxValue}.` };
    }
    return { valid: true, value: String(number) };
}


function captureActiveInputState() {
    const active = document.activeElement;
    if (active && active.tagName === 'INPUT' && active.dataset.token) {
        activeInputSnapshot = {
            token: active.dataset.token,
            value: active.value,
            selectionStart: active.selectionStart,
            selectionEnd: active.selectionEnd,
        };
        pinInputState[active.dataset.token] = active.value;
        return;
    }
    activeInputSnapshot = null;
}

function restoreActiveInputState() {
    if (!activeInputSnapshot) {
        return;
    }
    const input = pinGrid.querySelector(`input[data-token="${activeInputSnapshot.token}"]`);
    if (input) {
        input.value = activeInputSnapshot.value;
        if (typeof activeInputSnapshot.selectionStart === 'number' && typeof activeInputSnapshot.selectionEnd === 'number') {
            try {
                input.setSelectionRange(activeInputSnapshot.selectionStart, activeInputSnapshot.selectionEnd);
            } catch (err) {
                // ignore invalid ranges
            }
        }
        input.focus();
    }
    activeInputSnapshot = null;
}

function renderPinCards() {
    if (!config) {
        pinGrid.innerHTML = '';
        return;
    }
    const assigned = gatherAssignedPins();
    captureActiveInputState();
    const boardType = boardTypeMap[activeBoard] || 'schema';
    if (boardType === 'pcf') {
        renderPcfBoard(assigned);
    } else if (boardType === 'pca') {
        renderPcaBoard();
    } else {
        renderSchemaBoard(assigned);
    }
    restoreActiveInputState();
}

function renderSchemaBoard(assigned) {
    const entries = pinSchema.filter(entry => getEntryBoard(entry) === activeBoard);
    pinGrid.innerHTML = entries.map((entry, index) => {
        const currentValue = formatPinValue(getValueFromPath(entry.path));
        const hints = [];
        if (!entry.allowPcf) {
            hints.push('GPIO-only');
        }
        if (entry.hint) {
            hints.push(entry.hint);
        }
        return `
            <article class="pin-card" data-index="${index}" data-token="${entry.token}">
                <div class="pin-card__head">
                    <div>
                        <strong>${entry.label}</strong>
                        <div class="pin-card__owner">${entry.owner}</div>
                    </div>
                    <div class="pin-card__value">${currentValue}</div>
                </div>
                <p class="pin-card__desc">${entry.description}</p>
                <div class="pin-card__input">
                    <input type="text" placeholder="GPIO, pcf#, or none" data-token="${entry.token}" value="${pinInputState[entry.token] || ''}" />
                    <button type="button">Set</button>
                </div>
                <div class="pin-card__suggestions" data-token="${entry.token}"></div>
                <small class="pin-card__hint">${entry.type}${hints.length ? ' • ' + hints.join(' • ') : ''}</small>
                <div class="pin-card__message" aria-live="polite"></div>
                <button type="button" class="pin-card__board">Move board</button>
                <div class="pin-card__overlay hidden">
                    <p>Pin currently assigned to <span class="overlay-source"></span>. Overwrite?</p>
                    <div class="pin-card__overlay-buttons">
                        <button type="button" class="overlay-confirm">Overwrite</button>
                        <button type="button" class="overlay-cancel">Cancel</button>
                    </div>
                </div>
                <div class="pin-card__board-popup hidden" role="dialog" aria-modal="true">
                    <strong>Move this pin</strong>
                    <p>Select the board that should own this signal.</p>
                    <div class="pin-card__board-options"></div>
                </div>
            </article>
        `;
    }).join('');
    pinGrid.querySelectorAll('.pin-card').forEach(card => {
        const entry = entries[Number(card.dataset.index)];
        const input = card.querySelector('input');
        const setButton = card.querySelector('.pin-card__input button');
        const message = card.querySelector('.pin-card__message');
        const suggestions = card.querySelector('.pin-card__suggestions');
        const overlay = card.querySelector('.pin-card__overlay');
        const overlaySource = overlay.querySelector('.overlay-source');
        const overlayConfirm = overlay.querySelector('.overlay-confirm');
        const overlayCancel = overlay.querySelector('.overlay-cancel');
        const boardBtn = card.querySelector('.pin-card__board');
        const boardPopup = card.querySelector('.pin-card__board-popup');
        const boardOptions = boardPopup.querySelector('.pin-card__board-options');
        updateSuggestions(entry, suggestions, assigned);
        bindSuggestionHandlers(entry, suggestions, input, message, overlay, overlaySource, assigned);
        input.addEventListener('input', () => {
            pinInputState[entry.token] = input.value;
            suggestionSelections[entry.token] = null;
            updateSuggestions(entry, suggestions, assigned);
            bindSuggestionHandlers(entry, suggestions, input, message, overlay, overlaySource, assigned);
        });
        setButton.addEventListener('click', () => handleSetRequest(entry, input, message, assigned, overlay, overlaySource));
        overlayConfirm.addEventListener('click', () => {
            overlay.classList.add('hidden');
            handleSetRequest(entry, input, message, assigned, null, null, true);
        });
        overlayCancel.addEventListener('click', () => overlay.classList.add('hidden'));
        const targetBoards = boardList.filter(board => board !== getEntryBoard(entry));
        boardOptions.innerHTML = targetBoards.length
            ? targetBoards.map(board => `<button type="button" class="pin-card__board-option" data-board="${board}">${board}</button>`).join('')
            : '<span style="font-size:0.85rem;color:var(--muted);">No alternative boards</span>';
        boardOptions.querySelectorAll('.pin-card__board-option').forEach(opt => {
            opt.addEventListener('click', () => {
                boardOverrides[entry.token] = opt.dataset.board;
                saveBoardOverrides();
                boardPopupState.token = null;
                boardPopup.classList.add('hidden');
                renderPinCards();
            });
        });
        const isPopupOpen = boardPopupState.token === entry.token;
        boardPopup.classList.toggle('hidden', !isPopupOpen);
        boardPopup.addEventListener('click', event => event.stopPropagation());
        boardBtn.addEventListener('click', event => {
            event.stopPropagation();
            const isOpen = !boardPopup.classList.contains('hidden');
            if (isOpen) {
                boardPopupState.token = null;
                boardPopup.classList.add('hidden');
                return;
            }
            boardPopupState.token = entry.token;
            boardPopup.classList.remove('hidden');
        });
    });
}

function renderPcfBoard(assigned) {
    const cards = [];
    for (let idx = 0; idx < 16; idx++) {
        const key = `pcf${idx}`;
        const occupant = assigned.get(key);
        const owner = occupant ? occupant.owner : 'Available channel';
        const description = occupant ? occupant.description : 'This expander pin is free to claim.';
        const valueLabel = occupant ? occupant.label : `pcf${idx}`;
        const hint = occupant ? occupant.type : 'PCF8575 I/O';
        const extraClass = occupant ? '' : ' free';
        cards.push(`
            <article class="pin-card pin-card--static${extraClass}">
                <div class="pin-card__head">
                    <div>
                        <strong>PCF8575 I/O ${idx}</strong>
                        <div class="pin-card__owner">${owner}</div>
                    </div>
                    <div class="pin-card__value">${valueLabel}</div>
                </div>
                <p class="pin-card__desc">${description}</p>
                <div class="pin-card__static-meta">
                    <span>${hint}</span>
                    <span class="pin-card__static-pill">${occupant ? 'Assigned' : 'Open'}</span>
                </div>
            </article>
        `);
    }
    pinGrid.innerHTML = cards.join('');
}

function renderPcaBoard() {
    const channelMap = new Map();
    for (const spec of pcaChannelSpecs) {
        const value = getValueFromPath(spec.path);
        if (typeof value === 'number' && value >= 0 && value < 16) {
            channelMap.set(value, spec);
        }
    }
    const cards = [];
    for (let channel = 0; channel < 16; channel++) {
        const spec = channelMap.get(channel);
        const owner = spec ? spec.owner : 'Available channel';
        const description = spec ? spec.description : 'No configuration is using this channel.';
        const label = spec ? spec.label : `Channel ${channel}`;
        const extraClass = spec ? '' : ' free';
        cards.push(`
            <article class="pin-card pin-card--static${extraClass}">
                <div class="pin-card__head">
                    <div>
                        <strong>PCA9685 Channel ${channel}</strong>
                        <div class="pin-card__owner">${owner}</div>
                    </div>
                    <div class="pin-card__value">${label}</div>
                </div>
                <p class="pin-card__desc">${description}</p>
                <div class="pin-card__static-meta">
                    <span>${spec ? spec.type : 'Unassigned'}</span>
                    <span class="pin-card__static-pill">Slot ${channel}</span>
                </div>
            </article>
        `);
    }
    pinGrid.innerHTML = cards.join('');
}

function updateSuggestions(entry, container, assignedMap) {
    const options = computeFreePins(entry, assignedMap);
    if (!options.length) {
        container.innerHTML = '';
        return;
    }
    container.innerHTML = options.map(value => `<button type="button" data-value="${value}">${value}</button>`).join('');
}

function bindSuggestionHandlers(entry, container, input, message, overlay, overlaySource, assignedMap) {
    container.querySelectorAll('button').forEach(button => {
        const value = button.dataset.value;
        button.classList.toggle('selected', suggestionSelections[entry.token] === value);
        button.addEventListener('click', event => {
            event.stopPropagation();
            const currentlySelected = suggestionSelections[entry.token];
            if (currentlySelected === value) {
                suggestionSelections[entry.token] = null;
                container.querySelectorAll('button').forEach(btn => btn.classList.remove('selected'));
                handleSetRequest(entry, input, message, assignedMap, overlay, overlaySource, false, value);
            } else {
                suggestionSelections[entry.token] = value;
                container.querySelectorAll('button').forEach(btn => btn.classList.toggle('selected', btn === button));
                if (message) {
                    message.textContent = `Click again to assign ${value}.`;
                    message.className = 'pin-card__message';
                }
            }
        });
    });
}

function computeFreePins(entry, assignedMap) {
    const used = new Set();
    assignedMap.forEach((assignedEntry, key) => {
        if (assignedEntry.token !== entry.token) {
            used.add(key);
        }
    });
    const options = [];
    const start = Math.max(0, entry.minValue >= 0 ? entry.minValue : 0);
    const end = entry.maxValue >= 0 ? entry.maxValue : 39;
    for (let pin = start; pin <= end && options.length < 5; pin++) {
        const key = String(pin);
        if (!used.has(key)) {
            options.push(key);
        }
    }
    if (entry.allowPcf) {
        for (let idx = 0; idx < 16 && options.length < 5; idx++) {
            const key = `pcf${idx}`;
            if (!used.has(key)) {
                options.push(key);
            }
        }
    }
    return options;
}

function handleSetRequest(entry, input, message, assignedMap, overlay, overlaySource, forced = false, overrideValue = null) {
    const rawValue = overrideValue !== null ? overrideValue : input.value;
    const result = validatePinValue(entry, rawValue);
    if (!result.valid) {
        message.textContent = result.message;
        message.className = 'pin-card__message error';
        return;
    }
    const freshAssigned = gatherAssignedPins();
    const conflict = freshAssigned.get(result.value);
    if (conflict && conflict.token !== entry.token && overlay && !forced) {
        overlaySource.textContent = conflict.label;
        overlay.classList.remove('hidden');
        return;
    }
    applyPinChange(entry, input, message, result.value);
}

function applyPinChange(entry, input, message, value) {
    message.textContent = 'Saving…';
    message.className = 'pin-card__message';
    const payload = { [entry.token]: value };
    postConfig(payload)
        .then(() => refreshConfig())
        .then(() => {
            message.textContent = 'Updated';
            message.className = 'pin-card__message success';
            suggestionSelections[entry.token] = null;
            input.value = '';
            pinInputState[entry.token] = '';
        })
        .catch(err => {
            message.textContent = err.message;
            message.className = 'pin-card__message error';
        });
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
    renderPinCards();
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

function setActiveBoard(board) {
    activeBoard = board;
    boardPopupState.token = null;
    boardButtons.forEach(btn => btn.classList.toggle('active', btn.dataset.board === board));
    renderPinCards();
}

document.addEventListener('DOMContentLoaded', () => {
    boardButtons.forEach(button => {
        button.addEventListener('click', () => setActiveBoard(button.dataset.board));
    });
    document.addEventListener('click', event => {
        if (boardPopupState.token &&
            !event.target.closest('.pin-card__board-popup') &&
            !event.target.closest('.pin-card__board')) {
            const openPopup = pinGrid.querySelector('.pin-card__board-popup:not(.hidden)');
            if (openPopup) {
                openPopup.classList.add('hidden');
            }
            boardPopupState.token = null;
        }
    });
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
    page.replace("__PIN_SCHEMA_JSON__", buildPinSchemaJson());
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
