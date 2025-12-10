#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include "net.h"
#include "weather.h"
#include "cards.h"
#include "sprites.h"
#include "settings.h"
#include "mqtt.h"

enum NetState
{
    NET_STATE_IDLE = 0,
    NET_STATE_AP_RUNNING,
    NET_STATE_STA_CONNECTING,
    NET_STATE_STA_RUNNING
};

static NetState g_state = NET_STATE_IDLE;
static bool g_hasCreds = false;
static String g_ssid;
static String g_pass;
static bool g_isApMode = false;
static unsigned long g_staConnectStart = 0;

static WebServer server(80);
static Preferences g_prefs;

static void loadCreds();
static void saveCreds(const String &ssid, const String &pass);
static void handleWifiPost();
static void handleLocationPost();
static void handleCityPost();
static void handleSimulatePost();
static void handleSettingsPost();
static void handleSettingsGet();
static void handleCardSwitch();
static void handleApiCardSwitch();
static void handleApiSimulate();
static void handleCardsConfigPost();
static void handleEditor();
static void handleApiSprites();
static void handleApiSpriteGet();
static void handleApiSpriteSave();
static void handleApiSpriteReset();

// Helper to send HTML chunk and clear buffer
static String g_html;
static void sendChunk() {
    if (g_html.length() > 0) {
        server.sendContent(g_html);
        g_html = "";
    }
}

static void handleRoot()
{
    Settings& cfg = settings_get();
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    
    String& html = g_html;
    html.reserve(4000);
    
    html += R"(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WeatherThing Control Panel</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#f0f0f0;--card:#ffffff;--border:#000000;--text:#000000;--accent:#ffcc00;--success:#4ade80;--danger:#ff6b6b}
body{font-family:'Courier New', Courier, monospace;background-color:var(--bg);background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 60'%3E%3Cpath fill='%23d8eef5' opacity='0.6' d='M85,25c0-8-6-15-14-15c-2,0-3,0.2-5,0.5c-3-5-8-8-14-8c-5,0-9,2-12,5c-5-11-16-18-29-18c-13,0-24,8-29,19c-3-3-7-5-12-5c-9,0-16,7-16,16c0,1,0.1,2,0.3,3c-8,5-13,14-13,25c0,17,14,30,30,30h90c17,0,30-14,30-30c0-12-4-22-16-22z'/%3E%3C/svg%3E"),url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 60'%3E%3Cpath fill='%23c5e4ed' opacity='0.4' d='M85,25c0-8-6-15-14-15c-2,0-3,0.2-5,0.5c-3-5-8-8-14-8c-5,0-9,2-12,5c-5-11-16-18-29-18c-13,0-24,8-29,19c-3-3-7-5-12-5c-9,0-16,7-16,16c0,1,0.1,2,0.3,3c-8,5-13,14-13,25c0,17,14,30,30,30h90c17,0,30-14,30-30c0-12-4-22-16-22z'/%3E%3C/svg%3E");background-size:400px 200px,300px 150px;background-position:0 0,200px 80px;animation:drift 120s linear infinite,drift2 90s linear infinite reverse;color:var(--text);min-height:100vh;line-height:1.5;padding-bottom:50px}
.header{background:#000;color:#fff;padding:20px;text-align:center;border-bottom:6px solid #000;margin-bottom:30px;box-shadow:0 8px 0 rgba(0,0,0,0.2)}
.header h1{font-size:2.5em;font-weight:900;text-transform:uppercase;letter-spacing:-2px;margin-bottom:10px;text-shadow:4px 4px 0 #ff00ff}
.header .subtitle{font-weight:bold;text-transform:uppercase;letter-spacing:2px;font-size:0.8em}
.status-bar{display:flex;justify-content:center;gap:16px;margin-top:16px;flex-wrap:wrap;font-weight:bold}
.status-item{display:flex;align-items:center;gap:8px;background:#fff;color:#000;padding:5px 10px;border:3px solid #000;box-shadow:4px 4px 0 #000}
.status-dot{width:12px;height:12px;background:#000;border:2px solid #000}
.status-dot.online{background:var(--success)}
.status-dot.offline{background:var(--danger)}
.status-dot.ap{background:var(--accent)}
.container{max-width:900px;margin:0 auto;padding:0 20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:30px}
/* Neubrutalism Card */
.card{background:var(--card);border:5px solid #000;padding:20px;box-shadow:10px 10px 0 #000;transition:transform 0.2s,box-shadow 0.2s;position:relative}
.card:hover{transform:translate(-2px,-2px);box-shadow:14px 14px 0 #000}
.card-header{display:flex;align-items:center;gap:12px;margin-bottom:20px;padding-bottom:15px;border-bottom:4px solid #000}
.card-icon{font-size:2em}
.card-title{font-weight:900;font-size:1.4em;text-transform:uppercase;letter-spacing:-1px}
.form-group{margin-bottom:18px}
.form-group label{display:block;font-weight:bold;font-size:0.9em;margin-bottom:8px;text-transform:uppercase}
input,select{width:100%;padding:12px;border:3px solid #000;background:#fff;color:#000;font-family:inherit;font-weight:bold;font-size:16px;border-radius:0;outline:none;transition:all 0.2s}
input:focus,select:focus{background:#000;color:#fff;transform:scale(1.02)}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:14px 24px;border:3px solid #000;background:#000;color:#fff;font-weight:900;text-transform:uppercase;font-size:14px;cursor:pointer;transition:all 0.2s;text-decoration:none;border-radius:0;box-shadow:5px 5px 0 rgba(0,0,0,0.3)}
.btn:hover{transform:translate(-2px,-2px);box-shadow:8px 8px 0 rgba(0,0,0,0.3);background:#333}
.btn:active{transform:translate(2px,2px);box-shadow:2px 2px 0 rgba(0,0,0,0.3)}
.btn-primary{background:#000}
.btn-primary:hover{background:#5ad641;color:#000}
.btn-secondary{background:#fff;color:#000}
.btn-secondary:hover{background:#e0e0e0}
.btn-accent{background:#ffcc00;color:#000}
.btn-accent:hover{background:#ffdd33}
.btn-full{width:100%}
.row{display:flex;gap:15px;align-items:flex-end}
.row>*{flex:1}
.row .btn{flex:0 0 auto}
.info-box{background:#f0f0f0;border:3px solid #000;padding:15px;margin-bottom:15px;font-weight:bold}
.weather-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:15px}
.weather-btn{padding:15px;border:3px solid #000;background:#fff;cursor:pointer;transition:all 0.2s;font-size:1.5em;box-shadow:3px 3px 0 #000}
.weather-btn:hover{transform:translate(-2px,-2px);box-shadow:6px 6px 0 #000;background:#ffffcc}
.temp-input{display:flex;align-items:center;gap:10px}
.temp-input input{width:100px;text-align:center;font-size:1.5em}
.footer{text-align:center;margin-top:40px;font-weight:bold;text-transform:uppercase}
@keyframes drift{0%{background-position:0 0,200px 80px}100%{background-position:400px 200px,600px 230px}}@keyframes drift2{0%{background-position:200px 80px}100%{background-position:-100px -70px}}
@media(max-width:600px){.grid{grid-template-columns:1fr}.row{flex-direction:column}.row>*{width:100%}}
</style></head><body>
<div class="header">
<h1>WeatherThing</h1>
<p class="subtitle">LED Matrix Weather Display</p>
<div class="status-bar">
<div class="status-item"><span class="status-dot )";

    html += g_isApMode ? "ap" : "online";
    html += R"("></span><span>)";
    html += g_isApMode ? "Access Point Mode" : "Connected to WiFi";
    html += R"(</span></div>)";
    
    if (!g_isApMode) {
        html += "<div class='status-item'><strong>IP:</strong> " + WiFi.localIP().toString() + "</div>";
    }
    
    html += R"(</div></div>
<div class="container">
<div class="grid">)";

    // Card names and icons for 16 cards (11 original + 5 social)
    const char* cardNames[] = {"Weather", "Clock", "BTC", "Stocks", "Network", "Audio", "Sparkle", "Aurora", "Games", "MQTT", "RSS", "YouTube", "Twitch", "Twitter", "Insta", "TikTok"};
    const char* cardIcons[] = {"&#x26C5;", "&#x1F551;", "&#x20BF;", "&#x1F4C8;", "&#x1F310;", "&#x1F3A4;", "&#x2728;", "&#x1F308;", "&#x1F3AE;", "&#x1F3E0;", "&#x1F4F0;", "&#x25B6;", "&#x1F4AC;", "&#x2716;", "&#x1F4F7;", "&#x1F3B5;"};
    
    // ========== CARD GALLERY - Full width section ==========
    html += "</div>"; // Close grid temporarily
    html += "<div class=\"card\" style=\"margin-bottom:30px\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F3AC;</span><span class=\"card-title\">Card Gallery</span></div>";
    
    // Auto-cycle controls
    html += "<form method=\"POST\" action=\"/cards_config\" id=\"cardForm\">";
    html += "<div style=\"display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:20px;padding:15px;background:#f8f8f8;border:3px solid #000\">";
    html += "<label style=\"font-weight:bold;display:flex;align-items:center;gap:8px\"><input type=\"checkbox\" name=\"cycleOn\" value=\"1\"" + String(cfg.cycleEnabled ? " checked" : "") + " style=\"width:20px;height:20px\"> Auto-Cycle</label>";
    html += "<div style=\"display:flex;align-items:center;gap:8px\"><span style=\"font-weight:bold\">Every</span><input type=\"number\" name=\"cycleDur\" value=\"" + String(cfg.cycleDuration) + "\" min=\"3\" max=\"3600\" style=\"width:80px\"><span style=\"font-weight:bold\">sec</span></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary\" onclick=\"saveOrder()\" style=\"margin-left:auto\">&#x1F4BE; Save Config</button>";
    html += "</div>";
    
    // Card gallery grid
    html += "<p style=\"font-size:0.85em;margin-bottom:15px;color:#666\">&#x2630; Drag cards to reorder. Click preset to show on display. Toggle checkbox to include in auto-cycle.</p>";
    html += "<div id=\"cardGallery\" style=\"display:flex;flex-direction:column;gap:15px\">";
    
    // Generate cards in order (skip Sparkle=6, Aurora=7, and non-functional social cards 12-15)
    uint16_t seenCards = 0; // Bitmask to track which cards we've already shown
    for(int i=0; i<16; ++i) {
        uint8_t cardIdx = cfg.cardOrder[i];
        if(cardIdx > 15) continue; // Skip invalid entries
        if(cardIdx == 6 || cardIdx == 7) continue; // Skip Sparkle/Aurora (VU-only)
        if(cardIdx >= 12 && cardIdx <= 15) continue; // Skip non-functional social cards (Twitch/Twitter/Insta/TikTok)
        if(seenCards & (1 << cardIdx)) continue; // Skip duplicates
        seenCards |= (1 << cardIdx); // Mark as seen
        bool enabled = cfg.cardEnabled[cardIdx];
        
        html += "<div class=\"gallery-card\" draggable=\"true\" data-idx=\"" + String(cardIdx) + "\" style=\"background:#fff;border:4px solid #000;padding:12px;cursor:grab;opacity:" + String(enabled ? "1" : "0.5") + "\">";
        
        // Header with icon, name, and auto-rotate checkbox
        html += "<div style=\"display:flex;align-items:center;gap:10px;margin-bottom:10px;padding-bottom:10px;border-bottom:2px solid #000\">";
        html += "<span style=\"font-size:1.5em;cursor:grab\" class=\"drag-handle\">&#x2630;</span>";
        html += "<span style=\"font-size:1.4em\">" + String(cardIcons[cardIdx]) + "</span>";
        html += "<span style=\"font-weight:900;font-size:1.1em;text-transform:uppercase;flex:1\">" + String(cardNames[cardIdx]) + "</span>";
        // Prominent auto-rotate checkbox
        html += "<label style=\"display:flex;align-items:center;gap:4px;background:" + String(enabled ? "#4ade80" : "#ff6b6b") + ";padding:3px 8px;border:2px solid #000;font-size:0.7em;font-weight:bold;cursor:pointer\">";
        html += "<input type=\"checkbox\" name=\"en_" + String(cardIdx) + "\" value=\"1\"" + String(enabled ? " checked" : "") + " style=\"width:16px;height:16px\" onchange=\"this.closest('.gallery-card').style.opacity=this.checked?1:0.5;this.parentElement.style.background=this.checked?'#4ade80':'#ff6b6b'\">";
        html += "&#x1F503;</label>";
        html += "</div>";
        
        
        // Preset buttons with checkboxes for rotation inclusion
        html += "<div style=\"display:flex;flex-wrap:wrap;gap:8px;margin-top:8px\">";
        
        // Helper lambda to generate preset button with checkbox
        auto presetBtn = [&](uint8_t card, uint8_t preset, const char* label) {
            bool checked = (cfg.presetEnabled[card] & (1UL << preset)) != 0;
            html += "<div class=\"preset-btn\" style=\"display:flex;align-items:center;gap:10px;background:#fffacd;border:4px solid #000;padding:16px 24px;font-size:1.2em;font-weight:bold;box-shadow:4px 4px 0 #000;transition:all 0.2s;cursor:pointer\" data-card=\"" + String(card) + "\" data-preset=\"" + String(preset) + "\">";
            html += "<input type=\"checkbox\" name=\"p" + String(card) + "_" + String(preset) + "\"" + String(checked ? " checked" : "") + " style=\"width:24px;height:24px;margin:0;cursor:pointer\">";
            html += "<span>" + String(label) + "</span>";
            html += "</div>";
        };
        
        switch(cardIdx) {
            case 0: { // Weather presets + location + display + simulator
                presetBtn(0, 0, "Classic"); presetBtn(0, 1, "Bar"); presetBtn(0, 2, "Corner");
                presetBtn(0, 4, "Minimal"); presetBtn(0, 5, "Day/Nite"); presetBtn(0, 6, "Term");
                presetBtn(0, 8, "Forecast"); presetBtn(0, 9, "Pixel"); presetBtn(0, 10, "LCD");
                presetBtn(0, 11, "Mood"); presetBtn(0, 12, "Type"); presetBtn(0, 13, "Waves");
                presetBtn(0, 14, "Split"); presetBtn(0, 15, "Count"); presetBtn(0, 16, "Stack");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">"; // Close preset buttons, open settings
                
                // Location settings
                float lat, lon;
                weather_get_location(&lat, &lon);
                html += "<details><summary style=\"cursor:pointer;font-weight:bold\">&#x1F4CD; Location</summary>";
                html += "<div style=\"padding:10px 0\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">Current: <b>" + String(lat, 4) + ", " + String(lon, 4) + "</b></p>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<input name=\"city\" placeholder=\"City name\" style=\"flex:1;padding:6px\">";
                html += "</div>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"lat\" type=\"number\" step=\"0.01\" placeholder=\"Lat\" value=\"" + String(lat, 2) + "\" style=\"width:80px;padding:6px\">";
                html += "<input name=\"lon\" type=\"number\" step=\"0.01\" placeholder=\"Lon\" value=\"" + String(lon, 2) + "\" style=\"width:80px;padding:6px\">";
                html += "</div></div></details>";
                
                // Display settings
                html += "<details style=\"margin-top:8px\"><summary style=\"cursor:pointer;font-weight:bold\">&#x1F3A8; Display</summary>";
                html += "<div style=\"padding:10px 0;display:flex;gap:10px;flex-wrap:wrap;align-items:center\">";
                html += "<label>Palette:</label><select name=\"tempPalette\" style=\"padding:6px\">";
                html += "<option value=\"0\""; if (cfg.tempPalette == 0) html += " selected"; html += ">Default</option>";
                html += "<option value=\"1\""; if (cfg.tempPalette == 1) html += " selected"; html += ">Cool</option>";
                html += "<option value=\"2\""; if (cfg.tempPalette == 2) html += " selected"; html += ">Warm</option>";
                html += "</select>";
                html += "<label>Forecast:</label><select name=\"forecastHours\" style=\"padding:6px\">";
                html += "<option value=\"12\""; if (cfg.forecastHours == 12) html += " selected"; html += ">12h</option>";
                html += "<option value=\"24\""; if (cfg.forecastHours == 24) html += " selected"; html += ">24h</option>";
                html += "<option value=\"48\""; if (cfg.forecastHours == 48) html += " selected"; html += ">48h</option>";
                html += "</select></div></details>";
                
                // Simulator
                html += "<details style=\"margin-top:8px\"><summary style=\"cursor:pointer;font-weight:bold\">&#x1F9EA; Simulator</summary>";
                html += "<div style=\"padding:10px 0\">";
                html += "<div class=\"weather-grid\" style=\"display:grid;grid-template-columns:repeat(6,1fr);gap:4px;margin-bottom:8px\">";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"0\" onclick=\"simWeather(this)\" title=\"Sunny\" style=\"padding:8px\">&#x2600;</button>";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"1\" onclick=\"simWeather(this)\" title=\"Partly Cloudy\" style=\"padding:8px\">&#x26C5;</button>";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"2\" onclick=\"simWeather(this)\" title=\"Cloudy\" style=\"padding:8px\">&#x2601;</button>";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"5\" onclick=\"simWeather(this)\" title=\"Rain\" style=\"padding:8px\">&#x1F327;</button>";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"7\" onclick=\"simWeather(this)\" title=\"Storm\" style=\"padding:8px\">&#x26C8;</button>";
                html += "<button type=\"button\" class=\"weather-btn\" data-val=\"8\" onclick=\"simWeather(this)\" title=\"Snow\" style=\"padding:8px\">&#x2744;</button>";
                html += "</div>";
                html += "<div style=\"display:flex;align-items:center;gap:8px\">";
                html += "<label>Temp:</label><input id=\"simTemp\" type=\"number\" value=\"22\" min=\"-50\" max=\"50\" style=\"width:60px;padding:6px\"><span>&deg;C</span>";
                html += "</div></div></details>";
                break;
            }
            case 1: // Clock presets + timezone
                presetBtn(1, 0, "Digital"); presetBtn(1, 1, "Binary"); presetBtn(1, 2, "Minimal");
                presetBtn(1, 3, "Bars"); presetBtn(1, 4, "Nixie"); presetBtn(1, 5, "Glitch");
                presetBtn(1, 6, "Pong"); presetBtn(1, 7, "Word"); presetBtn(1, 8, "Bounce");
                presetBtn(1, 9, "Matrix"); presetBtn(1, 10, "Radar"); presetBtn(1, 11, "Flip");
                presetBtn(1, 12, "Cyber"); presetBtn(1, 13, "Analog");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<label style=\"font-weight:bold\">Timezone:</label>";
                html += "<select name=\"tz\" style=\"padding:6px\">";
                for (int8_t tz = -12; tz <= 14; ++tz) {
                    html += "<option value=\"" + String(tz) + "\"";
                    if (cfg.tzOffset == tz) html += " selected";
                    html += ">UTC" + String(tz >= 0 ? "+" : "") + String(tz) + "</option>";
                }
                html += "</select></div>";
                break;
            case 2: // BTC
                presetBtn(2, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; Uses free CoinGecko API - no key needed!</p>";
                html += "<div style=\"display:flex;align-items:center;gap:10px\">";
                html += "<label style=\"font-weight:bold\">Update every:</label>";
                html += "<select name=\"btcMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {1, 2, 5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 7; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.btcUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + " min</option>";
                }}
                html += "</select></div>";
                break;
            case 3: // Stocks
                presetBtn(3, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; Uses free Yahoo Finance - no key needed! Enter any stock ticker symbol.</p>";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<input name=\"stock\" type=\"text\" maxlength=\"10\" placeholder=\"AAPL\" value=\"" + String(cfg.stockSymbol) + "\" style=\"width:80px;text-transform:uppercase;padding:6px\">";
                html += "<label style=\"font-weight:bold\">Update:</label>";
                html += "<select name=\"stockMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {1, 2, 5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 7; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.stockUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + " min</option>";
                }}
                html += "</select></div>";
                break;
            case 5: // Audio VU presets
                presetBtn(5, 0, "Spectrum"); presetBtn(5, 1, "Wave"); presetBtn(5, 2, "Fire");
                presetBtn(5, 3, "Pulse"); presetBtn(5, 4, "Waterfall"); presetBtn(5, 5, "Strobe");
                presetBtn(5, 6, "Plasma"); presetBtn(5, 7, "Balls"); presetBtn(5, 8, "Matrix");
                presetBtn(5, 9, "Rainbow"); presetBtn(5, 10, "Mirror"); presetBtn(5, 11, "Laser");
                presetBtn(5, 12, "Dancer"); presetBtn(5, 13, "Heart"); presetBtn(5, 14, "Traffic");
                presetBtn(5, 15, "Pacman"); presetBtn(5, 16, "Vortex"); presetBtn(5, 17, "EQ");
                presetBtn(5, 18, "Disco"); presetBtn(5, 19, "Firework"); presetBtn(5, 20, "Rain");
                presetBtn(5, 21, "Nyan"); presetBtn(5, 22, "Ocean"); presetBtn(5, 23, "Tetris");
                presetBtn(5, 24, "Stars"); presetBtn(5, 25, "Lava"); presetBtn(5, 26, "Geo");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<div style=\"display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<label style=\"font-weight:bold\">Palette:</label>";
                html += "<select name=\"palette\" style=\"padding:6px\">";
                for (uint8_t i = 0; i < PALETTE_COUNT; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.vuPalette == i) html += " selected";
                    html += ">" + String(settings_palette_name(i)) + "</option>";
                }
                html += "</select>";
                html += "<label style=\"font-weight:bold\">Speed:</label>";
                html += "<select name=\"speed\" style=\"padding:6px\">";
                for (uint8_t i = 1; i <= 10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.animSpeed == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select></div>";
                html += "<div style=\"display:flex;align-items:center;gap:8px;flex-wrap:wrap\">";
                html += "<label style=\"font-weight:bold\">Gain:</label>";
                html += "<select name=\"micGain\" style=\"padding:6px\">";
                for (uint8_t i = 1; i <= 10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.micGain == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select>";
                html += "<label style=\"font-weight:bold\">Gate:</label>";
                html += "<select name=\"noiseGate\" style=\"padding:6px\">";
                { const char* gates[] = {"Off", "Low", "Med", "High"};
                  const uint8_t gateVals[] = {0, 50, 100, 150};
                  for (uint8_t i = 0; i < 4; ++i) {
                    html += "<option value=\"" + String(gateVals[i]) + "\"";
                    if (cfg.vuNoiseGate >= gateVals[i] && (i == 3 || cfg.vuNoiseGate < gateVals[i+1])) html += " selected";
                    html += ">" + String(gates[i]) + "</option>";
                }}
                html += "</select>";
                html += "<label style=\"display:flex;align-items:center;gap:6px;font-weight:bold;cursor:pointer\"><input type=\"checkbox\" name=\"micInvert\" value=\"1\"";
                if (cfg.vuInvert) html += " checked";
                html += " style=\"width:18px;height:18px\">Invert</label>";
                html += "</div>";
                break;
            case 8: // Game presets + controls
                presetBtn(8, 0, "Flappy"); presetBtn(8, 1, "Snake"); presetBtn(8, 2, "Breakout"); presetBtn(8, 3, "Pong");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x1F3AE; Controls:</b><br>";
                html += "&#x2022; <b>BTN1</b> &rarr; Next / Right<br>";
                html += "&#x2022; <b>BTN2</b> &rarr; Prev / Left<br>";
                html += "&#x2022; <b>Touch</b> &rarr; Jump / Action<br>";
                html += "&#x2022; <b>Hold both 1s</b> &rarr; Exit game</p>";
                break;
            case 9: // MQTT
                presetBtn(9, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>Setup:</b> Enter your MQTT broker address (e.g. Home Assistant IP). Device auto-registers via MQTT discovery.</p>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<input name=\"mqttServer\" placeholder=\"192.168.1.x\" value=\"" + String(cfg.mqttServer) + "\" style=\"flex:1;min-width:120px;padding:6px\">";
                html += "<input name=\"mqttPort\" type=\"number\" value=\"" + String(cfg.mqttPort) + "\" style=\"width:70px;padding:6px\">";
                html += "</div>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"mqttUser\" placeholder=\"user (optional)\" value=\"" + String(cfg.mqttUser) + "\" style=\"flex:1;padding:6px\">";
                html += "<input name=\"mqttPass\" type=\"password\" placeholder=\"pass\" value=\"" + String(cfg.mqttPass) + "\" style=\"flex:1;padding:6px\">";
                html += "</div>";
                break;
            case 10: // RSS
                presetBtn(10, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>Find RSS feeds:</b> Most news sites have RSS. Try adding <code>/rss</code> or <code>/feed</code> to any site URL, or search \"[site name] RSS feed\".</p>";
                html += "<input name=\"rssUrl\" placeholder=\"https://feeds.bbci.co.uk/news/rss.xml\" value=\"" + String(cfg.rssUrl) + "\" style=\"width:100%;padding:6px;margin-bottom:8px\">";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<label>Update:</label><select name=\"rssMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 5; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.rssUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + "m</option>";
                }}
                html += "</select>";
                html += "<label>Speed:</label><select name=\"rssSpd\" style=\"padding:6px\">";
                for(int i=1; i<=10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.rssSpeed == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select></div>";
                break;
            case 11: // YouTube
                presetBtn(11, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>Setup:</b> 1) Go to <a href=\"https://console.cloud.google.com\" target=\"_blank\">console.cloud.google.com</a> 2) Create project 3) Enable \"YouTube Data API v3\" 4) Create API Key under Credentials</p>";
                html += "<input name=\"ytChan\" placeholder=\"Channel ID (UCxxxxx from URL)\" value=\"" + String(cfg.ytChannelId) + "\" style=\"width:100%;padding:6px;margin-bottom:8px\">";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"ytKey\" type=\"password\" placeholder=\"API Key (AIza...)\" value=\"" + String(cfg.ytApiKey) + "\" style=\"flex:1;padding:6px\">";
                html += "<select name=\"socMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 5; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.socialUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + "m</option>";
                }}
                html += "</select></div>";
                break;
            default: // Single preset cards (Network, etc)
                presetBtn(cardIdx, 0, "SHOW");
                break;
        }
        html += "</div>";
        html += "</div>";
    }
    
    html += "</div>"; // End gallery grid
    html += "<input type=\"hidden\" name=\"order\" id=\"orderInput\">";
    html += "</form>";
    
    // JavaScript for gallery
    html += R"(<script>
const gallery = document.getElementById('cardGallery');
let draggedCard = null;

// Touch support
let touchStartY = 0;
let touchItem = null;

gallery.addEventListener('dragstart', e => {
    if(!e.target.classList.contains('gallery-card')) return;
    draggedCard = e.target;
    e.target.style.opacity = '0.4';
    e.target.style.transform = 'scale(0.95)';
});

gallery.addEventListener('dragend', e => {
    if(!e.target.classList.contains('gallery-card')) return;
    e.target.style.opacity = e.target.querySelector('input[type=checkbox]').checked ? '1' : '0.5';
    e.target.style.transform = '';
});

gallery.addEventListener('dragover', e => {
    e.preventDefault();
    const afterEl = getDragAfterEl(gallery, e.clientY);
    if(afterEl == null) gallery.appendChild(draggedCard);
    else gallery.insertBefore(draggedCard, afterEl);
});

// Touch events for mobile
gallery.addEventListener('touchstart', e => {
    const card = e.target.closest('.gallery-card');
    if(!card || !e.target.classList.contains('drag-handle')) return;
    touchItem = card;
    touchStartY = e.touches[0].clientY;
    card.style.opacity = '0.6';
}, {passive:true});

gallery.addEventListener('touchmove', e => {
    if(!touchItem) return;
    e.preventDefault();
    const y = e.touches[0].clientY;
    const afterEl = getDragAfterEl(gallery, y);
    if(afterEl == null) gallery.appendChild(touchItem);
    else gallery.insertBefore(touchItem, afterEl);
}, {passive:false});

gallery.addEventListener('touchend', e => {
    if(touchItem) {
        touchItem.style.opacity = touchItem.querySelector('input[type=checkbox]').checked ? '1' : '0.5';
        touchItem = null;
    }
});

function getDragAfterEl(container, y) {
    const els = [...container.querySelectorAll('.gallery-card:not([style*="opacity: 0.4"])')] ;
    return els.reduce((closest, child) => {
        const box = child.getBoundingClientRect();
        const offset = y - box.top - box.height / 2;
        if(offset < 0 && offset > closest.offset) return {offset, element:child};
        return closest;
    }, {offset: Number.NEGATIVE_INFINITY}).element;
}

function saveOrder() {
    const items = gallery.querySelectorAll('.gallery-card');
    let order = [];
    items.forEach(item => order.push(item.getAttribute('data-idx')));
    document.getElementById('orderInput').value = order.join(',');
}

function showCard(el, card, preset) {
    console.log('showCard called:', card, preset);
    el.style.background='#ffa500';
    fetch('/api/card?card='+card+'&preset='+preset)
    .then(r=>{console.log('Response:', r.status); return r.json();})
    .then(d=>{console.log('Result:', d); el.style.background='#4ade80';setTimeout(()=>el.style.background='',300)})
    .catch(e=>{console.error('Error:', e); el.style.background='#ff0000';});
}

// Event delegation for preset buttons
document.addEventListener('click', function(e) {
    const btn = e.target.closest('.preset-btn');
    if (!btn) return;
    if (e.target.type === 'checkbox') return; // Let checkbox handle itself
    const card = parseInt(btn.dataset.card);
    const preset = parseInt(btn.dataset.preset);
    showCard(btn, card, preset);
});

// Hover effects for preset buttons
document.addEventListener('mouseover', function(e) {
    const btn = e.target.closest('.preset-btn');
    if (btn) { btn.style.transform='translate(-2px,-2px)'; btn.style.boxShadow='6px 6px 0 #000'; }
});
document.addEventListener('mouseout', function(e) {
    const btn = e.target.closest('.preset-btn');
    if (btn) { btn.style.transform=''; btn.style.boxShadow='4px 4px 0 #000'; }
});

</script>)";
    
    html += "</div>"; // End Card Gallery
    sendChunk(); // Flush buffer after card gallery
    
    html += "<div class=\"grid\">"; // Reopen grid

    // WiFi Settings Card
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F4F6;</span><span class=\"card-title\">WiFi Settings</span></div>";
    
    if (g_hasCreds) {
        html += "<div class='info-box'>Connected to: <strong>" + g_ssid + "</strong></div>";
    }
    
    html += "<form method=\"POST\" action=\"/wifi\">";
    html += "<div class=\"form-group\"><label>Network Name (SSID)</label><input name=\"ssid\" value=\"";
    html += g_ssid;
    html += "\" placeholder=\"Enter WiFi network name\"></div>";
    html += "<div class=\"form-group\"><label>Password</label><input name=\"pass\" type=\"password\" placeholder=\"Enter WiFi password\"></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save WiFi Settings</button>";
    html += "</form></div>";
    sendChunk();
    
    // System Configuration Card (Brightness only now)
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x2699;</span><span class=\"card-title\">System Configuration</span></div>";
    
    // Brightness Section
    html += "<details><summary>Brightness & Power</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>Mode</label>";
    html += "<select name=\"brightMode\">";
    html += "<option value=\"0\"";
    if (cfg.brightMode == 0) html += " selected";
    html += ">Auto (light sensor)</option>";
    html += "<option value=\"1\"";
    if (cfg.brightMode == 1) html += " selected";
    html += ">Manual (fixed)</option>";
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Manual Brightness (" + String(cfg.brightManual) + ")</label>";
    html += "<input type=\"range\" name=\"brightManual\" min=\"1\" max=\"80\" value=\"" + String(cfg.brightManual) + "\"></div>";
    html += "<div class=\"form-group\"><label>Auto Min (dark room): " + String(cfg.brightMin) + "</label>";
    html += "<input type=\"range\" name=\"brightMin\" min=\"1\" max=\"40\" value=\"" + String(cfg.brightMin) + "\"></div>";
    html += "<div class=\"form-group\"><label>Auto Max (bright room): " + String(cfg.brightMax) + "</label>";
    html += "<input type=\"range\" name=\"brightMax\" min=\"10\" max=\"80\" value=\"" + String(cfg.brightMax) + "\"></div>";
    html += "<div class=\"form-group\"><label><input type=\"checkbox\" name=\"brightBlank\" value=\"1\"";
    if (cfg.brightBlanking) html += " checked";
    html += "> Use blanking for cleaner readings</label></div>";
    html += "<div class=\"form-group\"><label>Blanking Interval: " + String(cfg.brightBlankSecs) + " sec</label>";
    html += "<input type=\"range\" name=\"blankSec\" min=\"10\" max=\"120\" step=\"10\" value=\"" + String(cfg.brightBlankSecs) + "\"></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Brightness</button>";
    html += "</form></details>";
    
    html += "</div>"; // End card

    // Tools Card
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F6E0;</span><span class=\"card-title\">Tools &amp; Customization</span></div>";
    html += "<p style=\"color:var(--muted);margin-bottom:16px;font-size:0.9em\">Customize the display and edit pixel sprites</p>";
    html += "<a href=\"/editor\" class=\"btn btn-secondary btn-full\" style=\"margin-bottom:10px\">&#x1F3A8; Open Sprite Editor</a>";
    html += "<div class=\"quick-actions\">";
    html += "<a href=\"/\" class=\"btn btn-secondary\">&#x1F504; Refresh</a>";
    html += "<button class=\"btn btn-secondary\" onclick=\"alert('ESP32-C3 | 20x7 Matrix | 12 LED Timeline')\">&#x2139; Info</button>";
    html += "</div></div>";

    html += R"(</div></div>
<div class="footer">
<p><strong>WeatherThing v0.1</strong></p>
<p>by <a href="https://github.com/makeriga">Makeriga</a> • <a href="https://github.com/makeriga/weatherthing-firmware">GitHub</a> • <a href="/editor">Sprite Editor</a></p>
</div>
<script>
function simWeather(btn){
const type=btn.dataset.val;
const temp=document.getElementById('simTemp').value;
document.querySelectorAll('.weather-btn').forEach(b=>b.classList.remove('selected'));
btn.classList.add('selected');
fetch('/api/simulate?type='+type+'&temp='+temp)
.then(r=>r.json())
.then(d=>{if(d.ok){btn.style.background='#4ade80';setTimeout(()=>btn.style.background='',400)}})
.catch(e=>console.error(e));
}
</script>
</body></html>)";

    sendChunk(); // Final chunk
}

static void startServer()
{
    server.on("/", handleRoot);
    server.on("/wifi", HTTP_POST, handleWifiPost);
    server.on("/location", HTTP_POST, handleLocationPost);
    server.on("/city", HTTP_POST, handleCityPost);
    server.on("/simulate", HTTP_POST, handleSimulatePost);
    server.on("/settings", HTTP_POST, handleSettingsPost);
    server.on("/api/settings", HTTP_GET, handleSettingsGet);
    server.on("/editor", handleEditor);
    server.on("/api/sprites", HTTP_GET, handleApiSprites);
    server.on("/api/sprite", HTTP_GET, handleApiSpriteGet);
    server.on("/api/sprite", HTTP_POST, handleApiSpriteSave);
    server.on("/api/sprite/reset", HTTP_POST, handleApiSpriteReset);
    server.on("/card", handleCardSwitch);
    server.on("/api/card", HTTP_GET, handleApiCardSwitch);
    server.on("/api/simulate", HTTP_GET, handleApiSimulate);
    server.on("/cards_config", HTTP_POST, handleCardsConfigPost);
    server.begin();
}

static void loadCreds()
{
    g_ssid = "";
    g_pass = "";
    g_hasCreds = false;

    if (!g_prefs.begin("wtcfg", true))
    {
        return;
    }

    g_ssid = g_prefs.getString("ssid", "");
    g_pass = g_prefs.getString("pass", "");
    g_prefs.end();

    if (g_ssid.length() > 0)
    {
        g_hasCreds = true;
    }
}

static void saveCreds(const String &ssid, const String &pass)
{
    if (!g_prefs.begin("wtcfg", false))
    {
        return;
    }

    g_prefs.putString("ssid", ssid);
    g_prefs.putString("pass", pass);
    g_prefs.end();

    g_ssid = ssid;
    g_pass = pass;
    g_hasCreds = ssid.length() > 0;
}

static void startAp()
{
    uint64_t mac = ESP.getEfuseMac();
    uint32_t low = (uint32_t)(mac & 0xFFFFFFFFull);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", (unsigned int)(low & 0xFFFFu));
    String apSsid = String("WEATHERTHING_") + suffix;

    IPAddress ip(192, 168, 1, 4);
    IPAddress gw(192, 168, 1, 4);
    IPAddress mask(255, 255, 255, 0);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ip, gw, mask);
    bool ok = WiFi.softAP(apSsid.c_str(), "weatherthing");

    g_isApMode = true;
    g_state = NET_STATE_AP_RUNNING;

    if (MDNS.begin("weatherthing"))
    {
        MDNS.addService("http", "tcp", 80);
    }

    startServer();

    Serial.println("========== Access Point Mode ==========");
    Serial.print("  AP SSID: ");
    Serial.println(apSsid);
    Serial.println("  AP Password: weatherthing");
    Serial.print("  AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("  Setup URL: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/");
    Serial.println("========================================");

    String apIpStr = WiFi.softAPIP().toString();
    cards_notify_wifi_connected(apIpStr.c_str());
}

bool net_is_ap_mode()
{
    return g_isApMode;
}

bool net_has_wifi_creds()
{
    return g_hasCreds;
}

void net_begin()
{
    WiFi.persistent(false);

    loadCreds();

    if (g_hasCreds && g_ssid.length() > 0)
    {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_ssid.c_str(), g_pass.c_str());
        g_state = NET_STATE_STA_CONNECTING;
        g_isApMode = false;
        g_staConnectStart = millis();

        Serial.println("WiFi: connecting as client...");
    }
    else
    {
        Serial.println("WiFi: no saved credentials, starting AP");
        startAp();
    }
}

void net_factory_reset()
{
    if (g_prefs.begin("wtcfg", false))
    {
        g_prefs.clear();
        g_prefs.end();
    }

    g_ssid = "";
    g_pass = "";
    g_hasCreds = false;
}

static void handleWifiPost()
{
    String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
    String pass = server.hasArg("pass") ? server.arg("pass") : "";
    ssid.trim();
    pass.trim();

    if (ssid.length() == 0)
    {
        server.send(400, "text/plain", "SSID required");
        return;
    }

    saveCreds(ssid, pass);

    String html;
    html.reserve(1024);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>WeatherThing WiFi</title>";
    html += "<style>body{font-family:sans-serif;background:#000;color:#fff;text-align:center;padding:40px}";
    html += ".box{background:#222;border:4px solid #fff;padding:30px;max-width:400px;margin:0 auto}";
    html += "h1{color:#4ade80;margin-bottom:20px}";
    html += ".spinner{width:40px;height:40px;border:4px solid #333;border-top:4px solid #4ade80;border-radius:50%;animation:spin 1s linear infinite;margin:20px auto}";
    html += "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style>";
    html += "</head><body><div class='box'>";
    html += "<h1>&#x2705; WiFi Saved!</h1>";
    html += "<p>Network: <strong>";
    html += ssid;
    html += "</strong></p>";
    html += "<div class='spinner'></div>";
    html += "<p>Rebooting to connect...</p>";
    html += "<p style='color:#888;font-size:0.9em'>Device will restart in 3 seconds</p>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
    
    // Give time for the response to be sent, then reboot
    delay(500);
    server.handleClient();
    delay(2500);
    ESP.restart();
}

static void handleLocationPost()
{
    String latStr = server.hasArg("lat") ? server.arg("lat") : "";
    String lonStr = server.hasArg("lon") ? server.arg("lon") : "";

    float lat = latStr.toFloat();
    float lon = lonStr.toFloat();

    weather_set_location(lat, lon);

    String html;
    html.reserve(512);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>WeatherThing Location</title></head><body>";
    html += "<h1>Location saved</h1>";
    html += "<p>Latitude: ";
    html += String(lat, 4);
    html += "</p>";
    html += "<p>Longitude: ";
    html += String(lon, 4);
    html += "</p>";
    html += "<p>Weather will refresh shortly.</p>";
    html += "<p><a href='/'>Back to setup</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

static void handleCityPost()
{
    String city = server.hasArg("city") ? server.arg("city") : "";
    city.trim();

    if (city.length() < 2)
    {
        server.send(400, "text/plain", "City name required");
        return;
    }

    bool found = weather_set_city(city.c_str());

    String html;
    html.reserve(512);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>WeatherThing City</title></head><body>";
    
    if (found)
    {
        float lat, lon;
        weather_get_location(&lat, &lon);
        html += "<h1>City found!</h1>";
        html += "<p>City: ";
        html += city;
        html += "</p>";
        html += "<p>Coordinates: ";
        html += String(lat, 4);
        html += ", ";
        html += String(lon, 4);
        html += "</p>";
        html += "<p>Weather will refresh shortly.</p>";
    }
    else
    {
        html += "<h1>City not found</h1>";
        html += "<p>Could not find: ";
        html += city;
        html += "</p>";
        html += "<p>Try a different spelling or use coordinates.</p>";
    }
    
    html += "<p><a href='/'>Back to setup</a></p>";
    html += "</body></html>";
    server.send(found ? 200 : 404, "text/html", html);
}

static void handleSimulatePost()
{
    String typeStr = server.arg("type");
    String tempStr = server.arg("temp");
    
    uint8_t type = (uint8_t)typeStr.toInt();
    int8_t temp = (int8_t)tempStr.toInt();
    
    if (type >= WEATHER_TYPE_COUNT) type = 0;
    
    weather_simulate(type, temp);
    
    server.sendHeader("Location", "/");
    server.send(303);
}

// ============== SETTINGS ==============

static void handleSettingsPost()
{
    Settings& cfg = settings_get();
    
    if (server.hasArg("palette")) {
        cfg.vuPalette = (uint8_t)server.arg("palette").toInt();
        if (cfg.vuPalette >= PALETTE_COUNT) cfg.vuPalette = 0;
    }
    
    if (server.hasArg("speed")) {
        cfg.animSpeed = (uint8_t)server.arg("speed").toInt();
        if (cfg.animSpeed < 1) cfg.animSpeed = 1;
        if (cfg.animSpeed > 10) cfg.animSpeed = 10;
    }
    
    if (server.hasArg("micGain")) {
        cfg.micGain = (uint8_t)server.arg("micGain").toInt();
        if (cfg.micGain < 1) cfg.micGain = 1;
        if (cfg.micGain > 10) cfg.micGain = 10;
    }
    
    if (server.hasArg("noiseGate")) {
        cfg.vuNoiseGate = (uint8_t)server.arg("noiseGate").toInt();
    }
    
    // Mic inversion setting (checkbox - true if present)
    cfg.vuInvert = server.hasArg("micInvert");

    if (server.hasArg("tempPalette")) {
        cfg.tempPalette = (uint8_t)server.arg("tempPalette").toInt();
        if (cfg.tempPalette > 2) cfg.tempPalette = 0;
    }
    
    if (server.hasArg("simTimeout")) {
        cfg.simTimeoutSecs = (uint16_t)server.arg("simTimeout").toInt();
    }
    
    if (server.hasArg("forecastHours")) {
        uint8_t hrs = (uint8_t)server.arg("forecastHours").toInt();
        if (hrs == 12 || hrs == 24 || hrs == 48) cfg.forecastHours = hrs;
    }
    
    if (server.hasArg("stock")) {
        String sym = server.arg("stock");
        sym.toUpperCase();
        strncpy(cfg.stockSymbol, sym.c_str(), sizeof(cfg.stockSymbol) - 1);
        cfg.stockSymbol[sizeof(cfg.stockSymbol) - 1] = '\0';
        cfg.stockEnabled = sym.length() > 0;
    }
    
    if (server.hasArg("tz")) {
        int8_t tz = (int8_t)server.arg("tz").toInt();
        if (tz >= -12 && tz <= 14) {
            cfg.tzOffset = tz;
        }
    }
    
    if (server.hasArg("btcMins")) {
        uint8_t mins = (uint8_t)server.arg("btcMins").toInt();
        if (mins >= 1 && mins <= 60) {
            cfg.btcUpdateMins = mins;
        }
    }
    
    if (server.hasArg("stockMins")) {
        uint8_t mins = (uint8_t)server.arg("stockMins").toInt();
        if (mins >= 1 && mins <= 60) {
            cfg.stockUpdateMins = mins;
        }
    }
    
    // Brightness settings
    if (server.hasArg("brightMode")) {
        cfg.brightMode = (uint8_t)server.arg("brightMode").toInt();
    }
    if (server.hasArg("brightManual")) {
        cfg.brightManual = (uint8_t)server.arg("brightManual").toInt();
        if (cfg.brightManual < 1) cfg.brightManual = 1;
        if (cfg.brightManual > 80) cfg.brightManual = 80;
    }
    if (server.hasArg("brightMin")) {
        cfg.brightMin = (uint8_t)server.arg("brightMin").toInt();
        if (cfg.brightMin < 1) cfg.brightMin = 1;
        if (cfg.brightMin > 40) cfg.brightMin = 40;
    }
    if (server.hasArg("brightMax")) {
        cfg.brightMax = (uint8_t)server.arg("brightMax").toInt();
        if (cfg.brightMax < 10) cfg.brightMax = 10;
        if (cfg.brightMax > 80) cfg.brightMax = 80;
    }
    // Checkbox: if not present, it means unchecked
    cfg.brightBlanking = server.hasArg("brightBlank");
    if (server.hasArg("blankSec")) {
        cfg.brightBlankSecs = (uint8_t)server.arg("blankSec").toInt();
        if (cfg.brightBlankSecs < 10) cfg.brightBlankSecs = 10;
        if (cfg.brightBlankSecs > 120) cfg.brightBlankSecs = 120;
    }
    
    // MQTT settings
    bool mqttChanged = false;
    if (server.hasArg("mqttServer")) {
        String srv = server.arg("mqttServer");
        srv.trim();
        if (strcmp(cfg.mqttServer, srv.c_str()) != 0) {
            strncpy(cfg.mqttServer, srv.c_str(), sizeof(cfg.mqttServer) - 1);
            cfg.mqttServer[sizeof(cfg.mqttServer) - 1] = '\0';
            mqttChanged = true;
        }
    }
    if (server.hasArg("mqttPort")) {
        uint16_t port = (uint16_t)server.arg("mqttPort").toInt();
        if (port > 0 && port < 65535) {
            if (cfg.mqttPort != port) {
                cfg.mqttPort = port;
                mqttChanged = true;
            }
        }
    }
    if (server.hasArg("mqttUser")) {
        String usr = server.arg("mqttUser");
        usr.trim();
        strncpy(cfg.mqttUser, usr.c_str(), sizeof(cfg.mqttUser) - 1);
        cfg.mqttUser[sizeof(cfg.mqttUser) - 1] = '\0';
    }
    if (server.hasArg("mqttPass")) {
        String pwd = server.arg("mqttPass");
        // Only update if not empty (don't clear existing password)
        if (pwd.length() > 0) {
            strncpy(cfg.mqttPass, pwd.c_str(), sizeof(cfg.mqttPass) - 1);
            cfg.mqttPass[sizeof(cfg.mqttPass) - 1] = '\0';
        }
    }
    if (server.hasArg("mqttTopic")) {
        String top = server.arg("mqttTopic");
        top.trim();
        strncpy(cfg.mqttTopic, top.c_str(), sizeof(cfg.mqttTopic) - 1);
        cfg.mqttTopic[sizeof(cfg.mqttTopic) - 1] = '\0';
    }
    
    // Enable MQTT if server is configured
    cfg.mqttEnabled = (cfg.mqttServer[0] != '\0');
    
    // RSS settings
    if (server.hasArg("rssUrl")) {
        String url = server.arg("rssUrl");
        url.trim();
        strncpy(cfg.rssUrl, url.c_str(), sizeof(cfg.rssUrl) - 1);
        cfg.rssUrl[sizeof(cfg.rssUrl) - 1] = '\0';
    }
    if (server.hasArg("rssMins")) {
        uint8_t mins = (uint8_t)server.arg("rssMins").toInt();
        if (mins >= 1 && mins <= 240) cfg.rssUpdateMins = mins;
    }
    if (server.hasArg("rssSpd")) {
        cfg.rssSpeed = (uint8_t)server.arg("rssSpd").toInt();
        if (cfg.rssSpeed < 1) cfg.rssSpeed = 1;
        if (cfg.rssSpeed > 10) cfg.rssSpeed = 10;
    }
    if (server.hasArg("rssPal")) {
        cfg.rssPalette = (uint8_t)server.arg("rssPal").toInt();
        if (cfg.rssPalette >= PALETTE_COUNT) cfg.rssPalette = 0;
    }
    
    // YouTube settings (only functional social media card)
    if (server.hasArg("ytChan")) {
        String ch = server.arg("ytChan");
        ch.trim();
        strncpy(cfg.ytChannelId, ch.c_str(), sizeof(cfg.ytChannelId) - 1);
        cfg.ytChannelId[sizeof(cfg.ytChannelId) - 1] = '\0';
    }
    if (server.hasArg("ytKey")) {
        String key = server.arg("ytKey");
        if (key.length() > 0) {
            strncpy(cfg.ytApiKey, key.c_str(), sizeof(cfg.ytApiKey) - 1);
            cfg.ytApiKey[sizeof(cfg.ytApiKey) - 1] = '\0';
        }
    }
    if (server.hasArg("socMins")) {
        uint8_t mins = (uint8_t)server.arg("socMins").toInt();
        if (mins >= 1 && mins <= 60) cfg.socialUpdateMins = mins;
    }
    
    settings_save();
    
    // Reinitialize MQTT if settings changed
    if (mqttChanged && cfg.mqttEnabled) {
        mqtt_begin();
    }
    
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleSettingsGet()
{
    Settings& cfg = settings_get();
    String json = "{";
    json += "\"animSpeed\":" + String(cfg.animSpeed) + ",";
    json += "\"vuPalette\":" + String(cfg.vuPalette) + ",";
    json += "\"vuSensitivity\":" + String(cfg.vuSensitivity) + ",";
    json += "\"weatherPreset\":" + String(cfg.weatherPreset) + ",";
    json += "\"stockSymbol\":\"" + String(cfg.stockSymbol) + "\",";
    json += "\"stockEnabled\":" + String(cfg.stockEnabled ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
}

// ============== CARD SWITCHING ==============

static void handleCardSwitch()
{
    if (server.hasArg("card")) {
        uint8_t card = (uint8_t)server.arg("card").toInt();
        cards_switch_to(card);
    }
    if (server.hasArg("preset")) {
        uint8_t preset = (uint8_t)server.arg("preset").toInt();
        cards_set_preset(preset);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

// AJAX-friendly card switching
static void handleApiCardSwitch()
{
    if (server.hasArg("card")) {
        uint8_t card = (uint8_t)server.arg("card").toInt();
        cards_switch_to(card);
    }
    if (server.hasArg("preset")) {
        uint8_t preset = (uint8_t)server.arg("preset").toInt();
        cards_set_preset(preset);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

// AJAX-friendly weather simulation
static void handleApiSimulate()
{
    if (server.hasArg("type") && server.hasArg("temp")) {
        uint8_t wtype = (uint8_t)server.arg("type").toInt();
        int8_t temp = (int8_t)server.arg("temp").toInt();
        weather_simulate(wtype, temp);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleCardsConfigPost()
{
    Settings& cfg = settings_get();
    
    // 1. Parse Order
    if (server.hasArg("order")) {
        String orderStr = server.arg("order");
        // Expected format: "0,1,2,3,..."
        int start = 0;
        int idx = 0;
        while (start < (int)orderStr.length() && idx < 16) {
            int comma = orderStr.indexOf(',', start);
            if (comma == -1) comma = orderStr.length();
            
            String val = orderStr.substring(start, comma);
            cfg.cardOrder[idx] = (uint8_t)val.toInt();
            
            start = comma + 1;
            idx++;
        }
    }
    
    // 2. Update Enabled States (16 cards: 0-15)
    for(int i=0; i<16; ++i) {
        String key = "en_" + String(i);
        cfg.cardEnabled[i] = server.hasArg(key);
    }
    
    // 2b. Update Per-Preset Enabled States
    // Reset all presets to disabled first, then enable checked ones
    for(int card=0; card<16; ++card) {
        cfg.presetEnabled[card] = 0;
    }
    // Process preset checkboxes (format: p{card}_{preset})
    for(int i=0; i<server.args(); ++i) {
        String name = server.argName(i);
        if (name.startsWith("p") && name.indexOf('_') > 0) {
            int underscore = name.indexOf('_');
            int card = name.substring(1, underscore).toInt();
            int preset = name.substring(underscore + 1).toInt();
            if (card >= 0 && card < 16 && preset >= 0 && preset < 32) {
                cfg.presetEnabled[card] |= (1UL << preset);
            }
        }
    }
    
    // 3. Auto Cycle Settings
    cfg.cycleEnabled = server.hasArg("cycleOn");
    
    if (server.hasArg("cycleDur")) {
        uint16_t dur = (uint16_t)server.arg("cycleDur").toInt();
        if (dur < 3) dur = 3;
        if (dur > 3600) dur = 3600;
        cfg.cycleDuration = dur;
    }
    
    // 4. Audio settings (from Audio card in gallery)
    if (server.hasArg("palette")) {
        cfg.vuPalette = (uint8_t)server.arg("palette").toInt();
    }
    if (server.hasArg("speed")) {
        cfg.animSpeed = (uint8_t)server.arg("speed").toInt();
        if (cfg.animSpeed < 1) cfg.animSpeed = 1;
        if (cfg.animSpeed > 10) cfg.animSpeed = 10;
    }
    if (server.hasArg("micGain")) {
        cfg.micGain = (uint8_t)server.arg("micGain").toInt();
        if (cfg.micGain < 1) cfg.micGain = 1;
        if (cfg.micGain > 10) cfg.micGain = 10;
    }
    if (server.hasArg("noiseGate")) {
        cfg.vuNoiseGate = (uint8_t)server.arg("noiseGate").toInt();
    }
    cfg.vuInvert = server.hasArg("micInvert");
    
    // 5. Weather settings (from Weather card in gallery)
    if (server.hasArg("tempPalette")) {
        cfg.tempPalette = (uint8_t)server.arg("tempPalette").toInt();
        if (cfg.tempPalette > 2) cfg.tempPalette = 0;
    }
    if (server.hasArg("forecastHours")) {
        cfg.forecastHours = (uint8_t)server.arg("forecastHours").toInt();
    }
    
    settings_save();
    
    server.sendHeader("Location", "/");
    server.send(303);
}

// ============== SPRITE EDITOR ==============

static const char* SPRITE_NAMES[] = {
    "Digit 0", "Digit 1", "Digit 2", "Digit 3", "Digit 4",
    "Digit 5", "Digit 6", "Digit 7", "Digit 8", "Digit 9",
    "Sun", "Cloud", "Rain", "Storm", "Snow", "Wind",
    "WiFi", "Sad Face", "Bitcoin"
};

static void handleEditor()
{
    String html;
    html.reserve(10000);
    
    html += R"(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sprite Editor - WeatherThing</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--success:#238636;--danger:#da3633}
body{font-family:'Helvetica Neue',Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding:20px}
.header{background:linear-gradient(135deg,#1a1f26 0%,#0d1117 100%);border-bottom:1px solid var(--border);padding:20px;text-align:center}
.header h1{font-size:1.6em;color:var(--accent);margin-bottom:4px}
.back-link{color:var(--accent);text-decoration:none;font-size:0.9em}
.back-link:hover{text-decoration:underline}
.container{max-width:900px;margin:0 auto;padding:20px}
.layout{display:grid;grid-template-columns:1fr 320px;gap:20px}
@media(max-width:800px){.layout{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:20px;margin-bottom:16px}
.card-header{display:flex;align-items:center;gap:10px;margin-bottom:16px;padding-bottom:12px;border-bottom:1px solid var(--border)}
.card-title{color:var(--accent);font-weight:600;font-size:1.1em}
select{width:100%;padding:12px 14px;border-radius:10px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:14px;margin-bottom:16px}
select:focus{outline:none;border-color:var(--accent)}
.editor-area{display:flex;flex-direction:column;align-items:center}
.grid-container{background:#000;padding:8px;border-radius:12px;margin-bottom:16px}
.grid{display:inline-grid;gap:3px}
.pixel{width:36px;height:36px;background:#1a1a1a;border-radius:4px;cursor:pointer;transition:all 0.15s;border:1px solid #333}
.pixel:hover{background:#333;border-color:#555}
.pixel.on{background:#fff;border-color:#58a6ff;box-shadow:0 0 10px rgba(88,166,255,0.5)}
.info{color:var(--muted);font-size:0.85em;text-align:center}
.preview-section{text-align:center}
.preview-label{color:var(--muted);font-size:0.8em;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px}
.preview-box{display:inline-block;background:#000;padding:16px;border-radius:12px;margin-bottom:16px}
.mini-grid{display:inline-grid;gap:2px}
.mini-pixel{width:12px;height:12px;background:#1a1a1a;border-radius:2px}
.mini-pixel.on{background:#fff}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:12px 18px;border-radius:10px;border:none;font-weight:600;font-size:14px;cursor:pointer;transition:all 0.2s}
.btn-primary{background:var(--success);color:#fff}
.btn-primary:hover{background:#2ea043}
.btn-secondary{background:var(--border);color:var(--text)}
.btn-secondary:hover{background:#484f58}
.btn-danger{background:var(--danger);color:#fff}
.btn-danger:hover{background:#f85149}
.btn-full{width:100%}
.btn-group{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:16px}
.sprite-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(90px,1fr));gap:8px}
.sprite-btn{padding:10px 8px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);cursor:pointer;transition:all 0.2s;font-size:0.8em;text-align:center}
.sprite-btn:hover{border-color:var(--accent);background:rgba(88,166,255,0.1)}
.sprite-btn.active{border-color:var(--accent);background:rgba(88,166,255,0.2)}
.status{position:fixed;bottom:20px;right:20px;padding:12px 20px;border-radius:10px;background:var(--success);color:#fff;font-weight:600;transform:translateY(100px);transition:transform 0.3s}
.status.show{transform:translateY(0)}
</style></head><body>)";
    html += "<div class=\"header\">";
    html += "<h1>&#x1F3A8; Sprite Editor</h1>";
    html += "<a href=\"/\" class=\"back-link\">&larr; Back to Control Panel</a>";
    html += "</div>";
    html += "<div class=\"container\">";
    html += "<div class=\"layout\">";
    html += "<div class=\"main-panel\">";
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">Select Sprite to Edit</span></div>";
    html += "<select id=\"spriteSelect\" onchange=\"loadSprite()\">";

    for (int i = 0; i < SPRITE_COUNT; i++) {
        html += "<option value='";
        html += String(i);
        html += "'>";
        html += SPRITE_NAMES[i];
        html += "</option>";
    }

    html += "</select>";
    html += "<div class=\"editor-area\">";
    html += "<div class=\"grid-container\">";
    html += "<div class=\"grid\" id=\"editor\"></div>";
    html += "</div>";
    html += "<p class=\"info\">Click to toggle pixels - Drag to paint</p>";
    html += "</div>";
    html += "<div class=\"btn-group\">";
    html += "<button class=\"btn btn-primary\" onclick=\"saveSprite()\">&#x1F4BE; Save Changes</button>";
    html += "<button class=\"btn btn-secondary\" onclick=\"clearGrid()\">Clear</button>";
    html += "<button class=\"btn btn-secondary\" onclick=\"invertGrid()\">Invert</button>";
    html += "<button class=\"btn btn-danger\" onclick=\"resetSprite()\">Reset</button>";
    html += "</div>";
    html += "</div>";
    html += "</div>";

    html += "<div class=\"side-panel\">";
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">Preview</span></div>";
    html += "<div class=\"preview-section\">";
    html += "<div class=\"preview-box\">";
    html += "<div class=\"mini-grid\" id=\"preview\"></div>";
    html += "</div>";
    html += "<p class=\"preview-label\">Actual Size</p>";
    html += "</div>";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">Quick Select</span></div>";
    html += "<div class=\"sprite-grid\">";

    for (int i = 0; i < SPRITE_COUNT; i++) {
        html += "<button class='sprite-btn' data-id='";
        html += String(i);
        html += "' onclick='quickSelect(";
        html += String(i);
        html += ")'>";
        html += SPRITE_NAMES[i];
        html += "</button>";
    }

    html += R"(</div>
</div>
</div>
</div>
</div>
<div id="status" class="status">Saved!</div>
<script>
let currentSprite = 0;
let width = 7, height = 7;
let pixels = [];
let isDrawing = false;
let drawValue = true;

function showStatus(msg, isError) {
    const s = document.getElementById('status');
    s.textContent = msg;
    s.style.background = isError ? 'var(--danger)' : 'var(--success)';
    s.classList.add('show');
    setTimeout(() => s.classList.remove('show'), 2000);
}

function updateQuickBtns() {
    document.querySelectorAll('.sprite-btn').forEach(b => {
        b.classList.toggle('active', parseInt(b.dataset.id) === currentSprite);
    });
}

function buildGrid() {
    const editor = document.getElementById('editor');
    const preview = document.getElementById('preview');
    editor.innerHTML = '';
    preview.innerHTML = '';
    editor.style.gridTemplateColumns = `repeat(${width}, 36px)`;
    preview.style.gridTemplateColumns = `repeat(${width}, 12px)`;
    
    for (let y = height - 1; y >= 0; y--) {
        for (let x = 0; x < width; x++) {
            const idx = y * width + x;
            const px = document.createElement('div');
            px.className = 'pixel' + (pixels[idx] ? ' on' : '');
            px.dataset.idx = idx;
            px.onmousedown = e => { e.preventDefault(); isDrawing = true; drawValue = !pixels[idx]; toggle(idx); };
            px.onmouseenter = e => { if (isDrawing) toggle(idx, drawValue); };
            px.ontouchstart = e => { e.preventDefault(); isDrawing = true; drawValue = !pixels[idx]; toggle(idx); };
            editor.appendChild(px);
            
            const mp = document.createElement('div');
            mp.className = 'mini-pixel' + (pixels[idx] ? ' on' : '');
            mp.id = 'mp' + idx;
            preview.appendChild(mp);
        }
    }
}

function toggle(idx, val) {
    pixels[idx] = val !== undefined ? val : !pixels[idx];
    document.querySelectorAll('.pixel').forEach(p => {
        if (parseInt(p.dataset.idx) === idx) p.classList.toggle('on', pixels[idx]);
    });
    const mp = document.getElementById('mp' + idx);
    if (mp) mp.classList.toggle('on', pixels[idx]);
}

function loadSprite() {
    currentSprite = parseInt(document.getElementById('spriteSelect').value);
    updateQuickBtns();
    fetch('/api/sprite?id=' + currentSprite)
        .then(r => r.json())
        .then(data => {
            width = data.width || 7;
            height = data.height || 7;
            pixels = [];
            for (let y = 0; y < height; y++) {
                const row = data.rows[y] || 0;
                for (let x = 0; x < width; x++) {
                    pixels[y * width + x] = ((row >> (width - 1 - x)) & 1) === 1;
                }
            }
            buildGrid();
        });
}

function saveSprite() {
    const rows = [];
    for (let y = 0; y < height; y++) {
        let row = 0;
        for (let x = 0; x < width; x++) {
            if (pixels[y * width + x]) row |= (1 << (width - 1 - x));
        }
        rows.push(row);
    }
    fetch('/api/sprite?id=' + currentSprite, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({width, height, rows})
    }).then(r => {
        showStatus(r.ok ? '✓ Saved!' : '✗ Save failed', !r.ok);
    });
}

function resetSprite() {
    if (!confirm('Reset this sprite to default?')) return;
    fetch('/api/sprite/reset?id=' + currentSprite, {method: 'POST'})
        .then(r => { loadSprite(); showStatus('✓ Reset to default', false); });
}

function clearGrid() { pixels = pixels.map(() => false); buildGrid(); }
function invertGrid() { pixels = pixels.map(p => !p); buildGrid(); }
function quickSelect(id) {
    document.getElementById('spriteSelect').value = id;
    loadSprite();
}

document.addEventListener('mouseup', () => isDrawing = false);
document.addEventListener('touchend', () => isDrawing = false);
loadSprite();
</script>
</body></html>)";

    server.send(200, "text/html", html);
}

static void handleApiSprites()
{
    server.send(200, "application/json", sprites_all_to_json());
}

static void handleApiSpriteGet()
{
    int id = server.arg("id").toInt();
    if (id < 0 || id >= SPRITE_COUNT) {
        server.send(400, "application/json", "{\"error\":\"invalid id\"}");
        return;
    }
    server.send(200, "application/json", sprites_to_json((SpriteType)id));
}

static void handleApiSpriteSave()
{
    int id = server.arg("id").toInt();
    if (id < 0 || id >= SPRITE_COUNT) {
        server.send(400, "application/json", "{\"error\":\"invalid id\"}");
        return;
    }
    
    String body = server.arg("plain");
    if (sprites_from_json((SpriteType)id, body)) {
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static void handleApiSpriteReset()
{
    int id = server.arg("id").toInt();
    if (id < 0 || id >= SPRITE_COUNT) {
        server.send(400, "application/json", "{\"error\":\"invalid id\"}");
        return;
    }
    
    if (sprites_reset((SpriteType)id)) {
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"reset failed\"}");
    }
}

void net_loop()
{
    if (g_state == NET_STATE_STA_CONNECTING)
    {
        wl_status_t st = WiFi.status();
        unsigned long now = millis();

        if (st == WL_CONNECTED)
        {
            g_state = NET_STATE_STA_RUNNING;
            g_isApMode = false;

            if (MDNS.begin("weatherthing"))
            {
                MDNS.addService("http", "tcp", 80);
            }

            configTime(0, 0, "pool.ntp.org", "time.nist.gov");

            startServer();

            // Print detailed connection info
            Serial.println("========== WiFi Connected ==========");
            Serial.print("  SSID: ");
            Serial.println(g_ssid);
            Serial.print("  IP Address: ");
            Serial.println(WiFi.localIP());
            Serial.print("  Gateway: ");
            Serial.println(WiFi.gatewayIP());
            Serial.print("  Signal: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            Serial.print("  Web UI: http://");
            Serial.print(WiFi.localIP());
            Serial.println("/");
            Serial.println("=====================================");
            
            // Notify cards to show WiFi status
            String ipStr = WiFi.localIP().toString();
            cards_notify_wifi_connected(ipStr.c_str());
            
            // Initialize MQTT after WiFi is connected
            mqtt_begin();
        }
        else if (now - g_staConnectStart > 15000)
        {
            Serial.println("WiFi STA connect timeout, falling back to AP");
            WiFi.disconnect(true);
            startAp();
        }
    }

    if (g_state == NET_STATE_AP_RUNNING || g_state == NET_STATE_STA_RUNNING)
    {
        server.handleClient();
    }
}
