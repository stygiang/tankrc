#include "network/control_server.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <vector>

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

const char CONTROL_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>TankRC Control Hub</title>
<style>
:root {
    --bg:#0c111b;
    --panel:#161d2b;
    --accent:#1ecad3;
    --text:#f4f6ff;
    --warn:#ffb703;
    --danger:#ff3864;
}
* { box-sizing:border-box; }
body {
    margin:0;
    font-family:"Segoe UI",system-ui,-apple-system,sans-serif;
    background:linear-gradient(135deg,#05070c,#111b2f);
    color:var(--text);
}
header {
    padding:1.5rem 2rem;
    font-size:1.5rem;
    font-weight:600;
    letter-spacing:0.05em;
    background:rgba(0,0,0,0.4);
    border-bottom:1px solid rgba(255,255,255,0.08);
}
main {
    display:flex;
    flex-wrap:wrap;
    gap:1.5rem;
    padding:1.5rem;
}
.panel {
    background:var(--panel);
    border-radius:16px;
    padding:1.5rem;
    flex:1 1 320px;
    box-shadow:0 15px 40px rgba(0,0,0,0.35);
}
#rc-model {
    width:220px;
    height:140px;
    margin:0 auto 1rem;
    border-radius:18px;
    background:linear-gradient(145deg,#1f2a3f,#131b2d);
    position:relative;
    transition:transform 0.3s ease;
}
#rc-model.debug { box-shadow:0 0 25px rgba(0,200,255,0.7); }
#rc-model.locked { box-shadow:0 0 25px rgba(255,56,100,0.8); }
#rc-model.active { box-shadow:0 0 25px rgba(30,202,211,0.8); }
#rc-model.hazard { animation:hazardPulse 0.8s ease-in-out infinite; }
@keyframes hazardPulse {
    0% { transform:scale(1); }
    50% { transform:scale(1.03); }
    100% { transform:scale(1); }
}
.track {
    position:absolute; top:10px; bottom:10px; width:18px;
    background:#0b0f18; border-radius:12px;
}
.track.left { left:-24px; }
.track.right { right:-24px; }
.light {
    position:absolute;
    width:28px; height:20px;
    border-radius:12px;
    filter:blur(0.5px);
    background:#111;
}
.light.head { top:12px; }
.light.tail { bottom:12px; }
.light.left { left:18px; }
.light.right { right:18px; }
.stats { text-align:center; font-size:0.95rem; opacity:0.85; }
button, input, select {
    font:inherit;
}
button, .cta {
    background:var(--accent);
    border:none;
    color:#02060e;
    padding:0.75rem 1.2rem;
    border-radius:999px;
    font-weight:600;
    cursor:pointer;
    transition:transform 0.2s ease, box-shadow 0.2s ease;
}
button:hover, .cta:hover { transform:translateY(-2px); box-shadow:0 10px 20px rgba(30,202,211,0.35); }
form label { display:block; margin-top:0.8rem; font-size:0.9rem; text-transform:uppercase; letter-spacing:0.05em; opacity:0.8; }
form input, form select {
    width:100%;
    margin-top:0.3rem;
    padding:0.6rem;
    border-radius:10px;
    border:1px solid rgba(255,255,255,0.08);
    background:rgba(5,8,15,0.8);
    color:var(--text);
}
.switch-row {
    display:flex;
    align-items:center;
    justify-content:space-between;
    padding:0.4rem 0;
}
.badge {
    display:inline-flex;
    align-items:center;
    gap:0.4rem;
    padding:0.25rem 0.65rem;
    border-radius:999px;
    font-size:0.75rem;
    font-weight:600;
}
.badge.ok { background:rgba(30,202,211,0.2); color:var(--accent); }
.badge.warn { background:rgba(255,183,3,0.18); color:var(--warn); }
.badge.danger { background:rgba(255,56,100,0.18); color:var(--danger); }
.controls-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:1rem; }
.pin-hint { opacity:0.75; font-size:0.85rem; margin-bottom:0.8rem; }
.driver-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:1rem; }
.driver-card { background:rgba(5,8,15,0.6); border:1px solid rgba(255,255,255,0.08); border-radius:14px; padding:1rem; box-shadow:0 6px 18px rgba(0,0,0,0.35); }
.driver-card header { font-size:1.1rem; font-weight:600; margin-bottom:0.6rem; }
.driver-visual { display:flex; justify-content:space-between; font-size:0.85rem; margin-bottom:0.8rem; opacity:0.7; }
.driver-visual span { display:block; font-size:0.78rem; }
.pin-field { margin-bottom:0.6rem; }
.pin-field label { display:block; font-size:0.85rem; margin-bottom:0.2rem; }
.pin-input { display:flex; gap:0.4rem; }
.pin-input input { flex:1; }
.pin-input button { padding:0.45rem 0.9rem; border-radius:8px; border:none; background:var(--accent); color:#02060e; font-weight:600; cursor:pointer; }
.pin-input button:hover { filter:brightness(1.05); }
.pin-input input.pin-saved { box-shadow:0 0 0 2px rgba(30,202,211,0.6); }
.aux-card { display:flex; flex-direction:column; }
.pcf-select { min-width:90px; border-radius:8px; border:1px solid rgba(255,255,255,0.15); background:rgba(5,8,15,0.8); color:var(--text); padding:0.4rem; }
.pcf-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(120px,1fr)); gap:0.6rem; }
.pcf-cell { padding:0.6rem; border-radius:10px; border:1px solid rgba(255,255,255,0.08); background:rgba(5,8,15,0.6); text-align:center; box-shadow:0 4px 12px rgba(0,0,0,0.25); }
.pcf-cell.assigned { border-color:var(--accent); box-shadow:0 6px 16px rgba(30,202,211,0.3); }
.pcf-cell strong { display:block; font-size:0.9rem; }
.pcf-cell span { display:block; margin-top:0.3rem; font-size:0.78rem; opacity:0.8; }
</style>
</head>
<body>
<header>TankRC Control Hub</header>
<main>
<section class="panel" id="model-panel">
    <h2>Vehicle Status</h2>
    <div id="rc-model">
        <div class="track left"></div>
        <div class="track right"></div>
        <div class="light head left"></div>
        <div class="light head right"></div>
        <div class="light tail left"></div>
        <div class="light tail right"></div>
    </div>
    <div class="stats" id="status-text">Connecting...</div>
</section>
<section class="panel">
    <h2>Manual Overrides</h2>
    <div class="switch-row">
        <span>Hazard Lights</span>
        <label><input type="checkbox" id="hazardToggle" /> Enable</label>
    </div>
    <div class="switch-row">
        <span>Lighting</span>
        <label><input type="checkbox" id="lightsToggle" /> Force On</label>
    </div>
    <div style="margin-top:1rem;">
        <button id="clearOverrides">Return to RC Control</button>
        <a class="cta" style="display:block;margin-top:0.75rem;text-align:center;" href="/api/logs?format=csv" target="_blank">Download Log CSV</a>
    </div>
    <h3 style="margin-top:1.5rem;">Live Telemetry</h3>
    <div class="controls-grid" id="telemetry"></div>
</section>
<section class="panel">
    <h2>Wi-Fi & Features</h2>
    <form id="configForm">
        <label>Wi-Fi SSID<input type="text" id="ssid" maxlength="31" placeholder="Home WiFi" /></label>
        <label>Wi-Fi Password<input type="password" id="password" maxlength="63" /></label>
        <label>AP SSID<input type="text" id="apSsid" maxlength="31" /></label>
        <label>AP Password<input type="password" id="apPassword" maxlength="63" /></label>
        <label>PCA9685 Address<input type="number" id="pcaAddress" min="0" max="127" /></label>
        <label>PCA9685 Frequency (Hz)<input type="number" id="pwmFrequency" min="40" max="1600" /></label>
        <div class="switch-row">
            <span>Lighting Enabled</span>
            <label><input type="checkbox" id="lightingEnabled" /></label>
        </div>
        <div class="switch-row">
            <span>Sound Enabled</span>
            <label><input type="checkbox" id="soundEnabled" /></label>
        </div>
        <div class="switch-row">
            <span>Wi-Fi Enabled</span>
            <label><input type="checkbox" id="wifiEnabled" /></label>
        </div>
        <div class="switch-row">
            <span>Ultrasonic Enabled</span>
            <label><input type="checkbox" id="ultrasonicEnabled" /></label>
        </div>
        <div class="switch-row">
            <span>Tip-over Enabled</span>
            <label><input type="checkbox" id="tipEnabled" /></label>
        </div>
        <div class="switch-row">
            <span>Wi-Fi Alert Blink</span>
            <label><input type="checkbox" id="blinkWifi" /></label>
        </div>
        <div class="switch-row">
            <span>RC Alert Blink</span>
            <label><input type="checkbox" id="blinkRc" /></label>
        </div>
        <label>Blink Period (ms)<input type="number" id="blinkPeriod" min="100" max="2000" /></label>
        <div class="switch-row">
            <span>Config Backup</span>
            <a class="cta" href="/api/config/export" target="_blank">Download JSON</a>
        </div>
        <label>Import Config<input type="file" id="configFile" accept="application/json" /></label>
        <button type="button" class="cta" id="importConfig" style="margin-top:0.5rem; width:100%;">Import Config</button>
        <button type="submit" class="cta" style="margin-top:1rem; width:100%;">Save & Apply</button>
    </form>
</section>
<section class="panel">
    <h2>Motor Driver Pins</h2>
    <p class="pin-hint">Use GPIO numbers for native pins or type <code>pcf#</code> (e.g. <code>pcf3</code>) for expander lines. Click Update next to any field to push that pin immediately, or use Save & Apply for bulk edits.</p>
    <div class="driver-grid">
        <article class="driver-card">
            <header>Left Driver</header>
            <div class="driver-visual">
                <div><strong>Motor A</strong><span>PWM → IN1 → IN2</span></div>
                <div><strong>Motor B</strong><span>PWM → IN1 → IN2</span></div>
            </div>
            <div class="pin-field"><label>Motor A PWM</label><div class="pin-input"><input type="number" id="leftA_pwm" /><button type="button" data-pin-btn="leftA_pwm">Update</button></div></div>
            <div class="pin-field"><label>Motor A IN1</label><div class="pin-input"><input type="text" id="leftA_in1" placeholder="pcf3" /><button type="button" data-pin-btn="leftA_in1">Update</button></div></div>
            <div class="pin-field"><label>Motor A IN2</label><div class="pin-input"><input type="text" id="leftA_in2" placeholder="pcf4" /><button type="button" data-pin-btn="leftA_in2">Update</button></div></div>
            <div class="pin-field"><label>Motor B PWM</label><div class="pin-input"><input type="number" id="leftB_pwm" /><button type="button" data-pin-btn="leftB_pwm">Update</button></div></div>
            <div class="pin-field"><label>Motor B IN1</label><div class="pin-input"><input type="text" id="leftB_in1" /><button type="button" data-pin-btn="leftB_in1">Update</button></div></div>
            <div class="pin-field"><label>Motor B IN2</label><div class="pin-input"><input type="text" id="leftB_in2" /><button type="button" data-pin-btn="leftB_in2">Update</button></div></div>
            <div class="pin-field"><label>Driver STBY</label><div class="pin-input"><input type="text" id="left_stby" /><button type="button" data-pin-btn="left_stby">Update</button></div></div>
        </article>
        <article class="driver-card">
            <header>Right Driver</header>
            <div class="driver-visual">
                <div><strong>Motor A</strong><span>PWM → IN1 → IN2</span></div>
                <div><strong>Motor B</strong><span>PWM → IN1 → IN2</span></div>
            </div>
            <div class="pin-field"><label>Motor A PWM</label><div class="pin-input"><input type="number" id="rightA_pwm" /><button type="button" data-pin-btn="rightA_pwm">Update</button></div></div>
            <div class="pin-field"><label>Motor A IN1</label><div class="pin-input"><input type="text" id="rightA_in1" /><button type="button" data-pin-btn="rightA_in1">Update</button></div></div>
            <div class="pin-field"><label>Motor A IN2</label><div class="pin-input"><input type="text" id="rightA_in2" /><button type="button" data-pin-btn="rightA_in2">Update</button></div></div>
            <div class="pin-field"><label>Motor B PWM</label><div class="pin-input"><input type="number" id="rightB_pwm" /><button type="button" data-pin-btn="rightB_pwm">Update</button></div></div>
            <div class="pin-field"><label>Motor B IN1</label><div class="pin-input"><input type="text" id="rightB_in1" /><button type="button" data-pin-btn="rightB_in1">Update</button></div></div>
            <div class="pin-field"><label>Motor B IN2</label><div class="pin-input"><input type="text" id="rightB_in2" /><button type="button" data-pin-btn="rightB_in2">Update</button></div></div>
            <div class="pin-field"><label>Driver STBY</label><div class="pin-input"><input type="text" id="right_stby" /><button type="button" data-pin-btn="right_stby">Update</button></div></div>
        </article>
        <article class="driver-card aux-card">
            <header>Auxiliary Pins</header>
            <div class="pin-field"><label>Light Bar</label><div class="pin-input"><input type="text" id="light_pin" /><button type="button" data-pin-btn="light_pin">Update</button></div></div>
            <div class="pin-field"><label>Speaker</label><div class="pin-input"><input type="text" id="speaker_pin" /><button type="button" data-pin-btn="speaker_pin">Update</button></div></div>
            <div class="pin-field"><label>Battery Sense</label><div class="pin-input"><input type="text" id="battery_pin" /><button type="button" data-pin-btn="battery_pin">Update</button></div></div>
            <div class="pin-field"><label>PCF8575 Address</label><div class="pin-input"><input type="number" id="pcfAddress" /><button type="button" data-pin-btn="pcfAddress">Update</button></div></div>
            <div class="pin-field"><label>Slave TX</label><div class="pin-input"><input type="number" id="slave_tx" /><button type="button" data-pin-btn="slave_tx">Update</button></div></div>
            <div class="pin-field"><label>Slave RX</label><div class="pin-input"><input type="number" id="slave_rx" /><button type="button" data-pin-btn="slave_rx">Update</button></div></div>
        </article>
    </div>
</section>
<section class="panel">
    <h2>PCF8575 Map</h2>
    <p class="pin-hint">See which function currently consumes each expander bit. Selecting a PCF value above will update this grid automatically.</p>
    <div class="pcf-grid">
        <div class="pcf-cell" data-pcf-cell="0"><strong>PCF0</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="1"><strong>PCF1</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="2"><strong>PCF2</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="3"><strong>PCF3</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="4"><strong>PCF4</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="5"><strong>PCF5</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="6"><strong>PCF6</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="7"><strong>PCF7</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="8"><strong>PCF8</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="9"><strong>PCF9</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="10"><strong>PCF10</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="11"><strong>PCF11</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="12"><strong>PCF12</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="13"><strong>PCF13</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="14"><strong>PCF14</strong><span>Unused</span></div>
        <div class="pcf-cell" data-pcf-cell="15"><strong>PCF15</strong><span>Unused</span></div>
    </div>
</section>
</main>
<script>
let latestStatus=null;
async function refreshStatus(){
    try{
        const res=await fetch('/api/status');
        if(!res.ok) return;
        const data=await res.json();
        latestStatus=data;
        updateModel(data);
        updateTelemetry(data);
        updateOverridesUI(data);
    }catch(e){console.warn(e);}
}
function updateModel(data){
    const model=document.getElementById('rc-model');
    model.classList.remove('debug','active','locked','hazard');
    model.classList.add(data.modeClass||'active');
    if(data.hazard){ model.classList.add('hazard'); }
    const stats=document.getElementById('status-text');
    stats.innerHTML=`Mode: <strong>${data.mode}</strong><br>Wi-Fi: <span class="badge ${data.wifiLink?'ok':'danger'}">${data.wifiLink?'Online':'Offline'}</span> | RC: <span class="badge ${data.rcLink?'ok':'danger'}">${data.rcLink?'Linked':'Lost'}</span>`;
}
function updateTelemetry(data){
    const wrap=document.getElementById('telemetry');
    wrap.innerHTML=`
        <div>Steering: <strong>${(data.steering||0).toFixed(2)}</strong></div>
        <div>Throttle: <strong>${(data.throttle||0).toFixed(2)}</strong></div>
        <div>Hazard: <strong>${data.hazard?'ON':'off'}</strong></div>
        <div>Lighting: <strong>${data.lighting?'ON':'off'}</strong></div>
        <div>Ultrasonic L: <strong>${(data.ultraLeft||1).toFixed(2)}</strong></div>
        <div>Ultrasonic R: <strong>${(data.ultraRight||1).toFixed(2)}</strong></div>
        <div>Station IP: <strong>${data.ip||'—'}</strong></div>
        <div>AP IP: <strong>${data.ap||'—'}</strong></div>
        <div>Log Entries: <strong>${data.logCount||0}</strong></div>
        <div>Server Time: <strong>${data.serverTime||0}</strong></div>`;
}
function updateOverridesUI(data){
    document.getElementById('hazardToggle').checked=data.overrideHazard;
    document.getElementById('lightsToggle').checked=data.overrideLights;
}
function pinToString(value){
    if(value<=-2){return `pcf${(-value-2)}`;}
    if(value===-1){return 'none';}
    return `${value}`;
}
const pinEditors=[
    {input:'leftA_pwm', param:'leftMotorA_pwm', allowPcf:false, label:'Left Motor A PWM'},
    {input:'leftA_in1', param:'leftMotorA_in1', allowPcf:true, label:'Left Motor A IN1'},
    {input:'leftA_in2', param:'leftMotorA_in2', allowPcf:true, label:'Left Motor A IN2'},
    {input:'leftB_pwm', param:'leftMotorB_pwm', allowPcf:false, label:'Left Motor B PWM'},
    {input:'leftB_in1', param:'leftMotorB_in1', allowPcf:true, label:'Left Motor B IN1'},
    {input:'leftB_in2', param:'leftMotorB_in2', allowPcf:true, label:'Left Motor B IN2'},
    {input:'left_stby', param:'leftDriver_stby', allowPcf:true, label:'Left Driver STBY'},
    {input:'rightA_pwm', param:'rightMotorA_pwm', allowPcf:false, label:'Right Motor A PWM'},
    {input:'rightA_in1', param:'rightMotorA_in1', allowPcf:true, label:'Right Motor A IN1'},
    {input:'rightA_in2', param:'rightMotorA_in2', allowPcf:true, label:'Right Motor A IN2'},
    {input:'rightB_pwm', param:'rightMotorB_pwm', allowPcf:false, label:'Right Motor B PWM'},
    {input:'rightB_in1', param:'rightMotorB_in1', allowPcf:true, label:'Right Motor B IN1'},
    {input:'rightB_in2', param:'rightMotorB_in2', allowPcf:true, label:'Right Motor B IN2'},
    {input:'right_stby', param:'rightDriver_stby', allowPcf:true, label:'Right Driver STBY'},
    {input:'light_pin', param:'light_pin', allowPcf:true, label:'Light Bar'},
    {input:'speaker_pin', param:'speaker_pin', allowPcf:true, label:'Speaker'},
    {input:'battery_pin', param:'battery_pin', allowPcf:true, label:'Battery Sense'},
    {input:'pcfAddress', param:'pcfAddress', allowPcf:false, label:'PCF Address'},
    {input:'slave_tx', param:'slave_tx', allowPcf:false, label:'Slave TX'},
    {input:'slave_rx', param:'slave_rx', allowPcf:false, label:'Slave RX'},
];

function normalizePinInput(value, allowPcf){
    const trimmed=String(value ?? '').trim();
    if(!trimmed){return null;}
    const lower=trimmed.toLowerCase();
    if(allowPcf){
        if(lower==='none'||lower==='off'){return 'none';}
        if(lower.startsWith('pcf')){
            const idx=parseInt(lower.substring(3),10);
            if(Number.isNaN(idx) || idx<0 || idx>15){return null;}
            return `pcf${idx}`;
        }
    }
    if(/^-?\d+$/.test(trimmed)){
        return String(parseInt(trimmed,10));
    }
    return null;
}

function pinValueToIndex(raw){
    if(raw===undefined || raw===null){
        return -1;
    }
    const str=String(raw).trim().toLowerCase();
    if(!str){
        return -1;
    }
    if(str.startsWith('pcf')){
        const idx=parseInt(str.substring(3),10);
        return Number.isNaN(idx) ? -1 : idx;
    }
    const num=parseInt(str,10);
    if(Number.isNaN(num)){
        return -1;
    }
    if(num<=-2){
        return (-num-2);
    }
    return -1;
}

const pcfSelects=[];

async function updatePinField(cfg, overrideValue=null){
    const input=document.getElementById(cfg.input);
    const sourceValue=overrideValue!==null ? overrideValue : input.value;
    const normalized=normalizePinInput(sourceValue, cfg.allowPcf);
    if(normalized===null){
        alert('Enter a valid value (number or pcf#).');
        return;
    }
    const params=new URLSearchParams();
    params.append(cfg.param, normalized);
    const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
    if(res.ok){
        input.value=normalized;
        input.classList.add('pin-saved');
        setTimeout(()=>input.classList.remove('pin-saved'),1200);
        syncPcfSelectors();
        updatePcfMapFromInputs();
    }else{
        alert('Failed to update pin.');
    }
}

function registerPinEditors(){
    pinEditors.forEach(cfg=>{
        const btn=document.querySelector(`[data-pin-btn="${cfg.input}"]`);
        if(btn){
            btn.addEventListener('click',()=>updatePinField(cfg));
        }
    });
}

function attachPcfSelectors(){
    pinEditors.forEach(cfg=>{
        if(!cfg.allowPcf){
            return;
        }
        const input=document.getElementById(cfg.input);
        if(!input || input.dataset.pcfSelectorAttached){
            return;
        }
        const select=document.createElement('select');
        select.className='pcf-select';
        select.dataset.pinInput=cfg.input;
        const gpioOpt=document.createElement('option');
        gpioOpt.value='__gpio__';
        gpioOpt.textContent='GPIO';
        select.appendChild(gpioOpt);
        const noneOpt=document.createElement('option');
        noneOpt.value='none';
        noneOpt.textContent='none';
        select.appendChild(noneOpt);
        for(let i=0;i<16;++i){
            const opt=document.createElement('option');
            opt.value=`pcf${i}`;
            opt.textContent=`pcf${i}`;
            select.appendChild(opt);
        }
        select.addEventListener('change',()=>{
            if(select.value==='__gpio__'){
                input.focus();
                return;
            }
            updatePinField(cfg, select.value);
        });
        input.parentElement.appendChild(select);
        input.dataset.pcfSelectorAttached='1';
        pcfSelects.push(select);
    });
    syncPcfSelectors();
}

function syncPcfSelectors(){
    pcfSelects.forEach(select=>{
        const input=document.getElementById(select.dataset.pinInput);
        if(!input){
            return;
        }
        const val=String(input.value||'').trim().toLowerCase();
        if(!val){
            select.value='__gpio__';
        }else if(val.startsWith('pcf')){
            select.value=val;
        }else if(val==='none' || val==='off' || val==='-1'){
            select.value='none';
        }else{
            select.value='__gpio__';
        }
    });
}

function updatePcfMapFromInputs(){
    const assignments=new Array(16).fill('Unused');
    pinEditors.forEach(cfg=>{
        if(!cfg.allowPcf){
            return;
        }
        const input=document.getElementById(cfg.input);
        if(!input){
            return;
        }
        const idx=pinValueToIndex(input.value);
        if(idx>=0 && idx<assignments.length){
            assignments[idx]=cfg.label;
        }
    });
    document.querySelectorAll('[data-pcf-cell]').forEach(cell=>{
        const idx=parseInt(cell.dataset.pcfCell,10);
        const label=assignments[idx];
        cell.classList.toggle('assigned', label!=='Unused');
        const span=cell.querySelector('span');
        if(span){
            span.textContent=label;
        }
    });
}
async function loadConfig(){
    try{
        const res=await fetch('/api/config');
        if(!res.ok) return;
        const cfg=await res.json();
        document.getElementById('ssid').value=cfg.wifi.ssid||'';
        document.getElementById('password').value='';
        document.getElementById('apSsid').value=cfg.wifi.apSsid||'';
        document.getElementById('apPassword').value='';
        document.getElementById('pcaAddress').value=cfg.lighting.pcaAddress;
        document.getElementById('pwmFrequency').value=cfg.lighting.pwmFrequency;
        document.getElementById('lightingEnabled').checked=cfg.features.lighting;
        document.getElementById('soundEnabled').checked=cfg.features.sound;
        document.getElementById('wifiEnabled').checked=cfg.features.wifi;
        document.getElementById('ultrasonicEnabled').checked=cfg.features.ultrasonic;
        document.getElementById('tipEnabled').checked=cfg.features.tip;
        document.getElementById('blinkWifi').checked=cfg.blink.wifi;
        document.getElementById('blinkRc').checked=cfg.blink.rc;
        document.getElementById('blinkPeriod').value=cfg.blink.period;
        const pins=cfg.pins;
        document.getElementById('leftA_pwm').value=pins.leftDriver.motorA.pwm;
        document.getElementById('leftA_in1').value=pinToString(pins.leftDriver.motorA.in1);
        document.getElementById('leftA_in2').value=pinToString(pins.leftDriver.motorA.in2);
        document.getElementById('leftB_pwm').value=pins.leftDriver.motorB.pwm;
        document.getElementById('leftB_in1').value=pinToString(pins.leftDriver.motorB.in1);
        document.getElementById('leftB_in2').value=pinToString(pins.leftDriver.motorB.in2);
        document.getElementById('left_stby').value=pinToString(pins.leftDriver.standby);
        document.getElementById('rightA_pwm').value=pins.rightDriver.motorA.pwm;
        document.getElementById('rightA_in1').value=pinToString(pins.rightDriver.motorA.in1);
        document.getElementById('rightA_in2').value=pinToString(pins.rightDriver.motorA.in2);
        document.getElementById('rightB_pwm').value=pins.rightDriver.motorB.pwm;
        document.getElementById('rightB_in1').value=pinToString(pins.rightDriver.motorB.in1);
        document.getElementById('rightB_in2').value=pinToString(pins.rightDriver.motorB.in2);
        document.getElementById('right_stby').value=pinToString(pins.rightDriver.standby);
        document.getElementById('light_pin').value=pinToString(pins.lightBar);
        document.getElementById('speaker_pin').value=pinToString(pins.speaker);
        document.getElementById('battery_pin').value=pinToString(pins.batterySense);
        document.getElementById('pcfAddress').value=pins.pcfAddress;
        document.getElementById('slave_tx').value=pins.slaveTx;
        document.getElementById('slave_rx').value=pins.slaveRx;
        syncPcfSelectors();
        updatePcfMapFromInputs();
    }catch(e){console.warn(e);}
}
async function submitConfig(evt){
    evt.preventDefault();
    const params=new URLSearchParams();
    params.append('ssid',document.getElementById('ssid').value);
    params.append('password',document.getElementById('password').value);
    params.append('apSsid',document.getElementById('apSsid').value);
    params.append('apPassword',document.getElementById('apPassword').value);
    params.append('pcaAddress',document.getElementById('pcaAddress').value);
    params.append('pwmFrequency',document.getElementById('pwmFrequency').value);
    params.append('lightingEnabled',document.getElementById('lightingEnabled').checked?'1':'0');
    params.append('soundEnabled',document.getElementById('soundEnabled').checked?'1':'0');
    params.append('wifiEnabled',document.getElementById('wifiEnabled').checked?'1':'0');
    params.append('ultrasonicEnabled',document.getElementById('ultrasonicEnabled').checked?'1':'0');
    params.append('tipEnabled',document.getElementById('tipEnabled').checked?'1':'0');
    params.append('blinkWifi',document.getElementById('blinkWifi').checked?'1':'0');
    params.append('blinkRc',document.getElementById('blinkRc').checked?'1':'0');
    params.append('blinkPeriod',document.getElementById('blinkPeriod').value);
    params.append('leftMotorA_pwm',document.getElementById('leftA_pwm').value);
    params.append('leftMotorA_in1',document.getElementById('leftA_in1').value);
    params.append('leftMotorA_in2',document.getElementById('leftA_in2').value);
    params.append('leftMotorB_pwm',document.getElementById('leftB_pwm').value);
    params.append('leftMotorB_in1',document.getElementById('leftB_in1').value);
    params.append('leftMotorB_in2',document.getElementById('leftB_in2').value);
    params.append('leftDriver_stby',document.getElementById('left_stby').value);
    params.append('rightMotorA_pwm',document.getElementById('rightA_pwm').value);
    params.append('rightMotorA_in1',document.getElementById('rightA_in1').value);
    params.append('rightMotorA_in2',document.getElementById('rightA_in2').value);
    params.append('rightMotorB_pwm',document.getElementById('rightB_pwm').value);
    params.append('rightMotorB_in1',document.getElementById('rightB_in1').value);
    params.append('rightMotorB_in2',document.getElementById('rightB_in2').value);
    params.append('rightDriver_stby',document.getElementById('right_stby').value);
    params.append('light_pin',document.getElementById('light_pin').value);
    params.append('speaker_pin',document.getElementById('speaker_pin').value);
    params.append('battery_pin',document.getElementById('battery_pin').value);
    params.append('pcfAddress',document.getElementById('pcfAddress').value);
    params.append('slave_tx',document.getElementById('slave_tx').value);
    params.append('slave_rx',document.getElementById('slave_rx').value);
    const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
    if(res.ok){ alert('Settings updated. Device may reboot/reconnect.'); }
}
async function sendOverride(){
    const params=new URLSearchParams();
    params.append('hazardOverride',document.getElementById('hazardToggle').checked?'1':'0');
    params.append('hazard',document.getElementById('hazardToggle').checked?'1':'0');
    params.append('lightsOverride',document.getElementById('lightsToggle').checked?'1':'0');
    params.append('lights',document.getElementById('lightsToggle').checked?'1':'0');
    await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
}
async function clearOverrides(){
    await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'clear=1'});
    document.getElementById('hazardToggle').checked=false;
    document.getElementById('lightsToggle').checked=false;
}
async function importConfigFile(evt){
    const fileInput=document.getElementById('configFile');
    if(!fileInput.files.length){ alert('Select a JSON file to import.'); return; }
    const file=fileInput.files[0];
    const text=await file.text();
    const res=await fetch('/api/config/import',{method:'POST',headers:{'Content-Type':'application/json'},body:text});
    if(res.ok){ alert('Config imported. Device will apply the new settings.'); }
    else { alert('Import failed.'); }
}
document.addEventListener('DOMContentLoaded',()=>{
    registerPinEditors();
    attachPcfSelectors();
    loadConfig();
    refreshStatus();
    setInterval(refreshStatus,1000);
    document.getElementById('configForm').addEventListener('submit',submitConfig);
    document.getElementById('hazardToggle').addEventListener('change',sendOverride);
    document.getElementById('lightsToggle').addEventListener('change',sendOverride);
    document.getElementById('clearOverrides').addEventListener('click',clearOverrides);
    document.getElementById('importConfig').addEventListener('click',importConfigFile);
});
</script>
</body>
</html>)HTML";

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
    server_.send_P(200, "text/html", CONTROL_PAGE);
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

    auto copyString = [](char* dest, size_t len, const String& src) {
        if (len == 0) {
            return;
        }
        size_t copyLen = std::min(len - 1, static_cast<size_t>(src.length()));
        memcpy(dest, src.c_str(), copyLen);
        dest[copyLen] = '\0';
    };

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

void ControlServer::sendJson(const String& body) {
    server_.send(200, "application/json", body);
}
}  // namespace TankRC::Network
