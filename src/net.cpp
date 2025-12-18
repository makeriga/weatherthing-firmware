#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "net.h"
#include "weather.h"
#include "cards.h"
#include "sprites.h"
#include "settings.h"
#include "weatherthing_hw.h"
#include "mqtt.h"
#include "build_info.h"
#include "http_worker.h"
#include "custom_overlay.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

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
static void handleApiTouchShortcut();
static void handleApiVersion();
static void handleApiDiag();
static void handleApiCheckUpdate();
static void handleCardsConfigPost();
static void handleEditor();
static void handleApiSprites();
static void handleApiSpriteGet();
static void handleApiSpriteSave();
static void handleApiSpriteReset();

static void handleApiOverlayGet();
static void handleApiOverlayClear();
static void handleApiOverlayMatrix();
static void handleApiOverlayTimeline();
static void handleApiOverlayText();
static void handleApiLang();

// ============== TRANSLATION SYSTEM ==============
// Returns English or Latvian string based on settings
static inline const char* TR(const char* en, const char* lv) {
    return settings_get().uiLang == 1 ? lv : en;
}

// Helper to send HTML chunk and clear buffer
static String g_html;
static void sendChunk() {
    if (g_html.length() > 0) {
        server.sendContent(g_html);
        g_html = "";
    }
    // Feed watchdog during long HTML generation to prevent timeout
    esp_task_wdt_reset();
}

static bool parseHexColor(const String& s, uint32_t* out)
{
    const char* p = s.c_str();
    if (!p || !out) return false;
    if (p[0] == '#') p++;
    if (strlen(p) != 6) return false;
    uint8_t r = (uint8_t)strtoul(String(p).substring(0, 2).c_str(), nullptr, 16);
    uint8_t g = (uint8_t)strtoul(String(p).substring(2, 4).c_str(), nullptr, 16);
    uint8_t b = (uint8_t)strtoul(String(p).substring(4, 6).c_str(), nullptr, 16);
    *out = wt_color(r, g, b);
    return true;
}

static bool parseJsonColor(const JsonVariantConst& v, uint32_t* out)
{
    if (!out) return false;

    if (v.is<const char*>())
    {
        String s = v.as<const char*>();
        return parseHexColor(s, out);
    }

    if (v.is<uint32_t>())
    {
        *out = v.as<uint32_t>();
        return true;
    }

    if (v.is<long>())
    {
        *out = (uint32_t)v.as<long>();
        return true;
    }

    return false;
}

static void apiSendError(int code, const char* msg)
{
    String json;
    json.reserve(96);
    json += "{\"error\":\"";
    json += msg ? msg : "error";
    json += "\"}";
    server.send(code, "application/json", json);
}

static void handleRoot()
{
    Settings& cfg = settings_get();
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    
    String& html = g_html;
    html.reserve(4000);

    auto colorHex = [&](uint32_t c) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", (unsigned)((c >> 16) & 0xFF), (unsigned)((c >> 8) & 0xFF), (unsigned)(c & 0xFF));
        return String(buf);
    };
    
    html += R"(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WeatherThing Control Panel</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#f0f0f0;--card:#ffffff;--border:#000000;--text:#000000;--accent:#ffcc00;--success:#4ade80;--danger:#ff6b6b}
body{font-family:'Courier New', Courier, monospace;background-color:var(--bg);background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 60'%3E%3Cpath fill='%23d8eef5' opacity='0.6' d='M85,25c0-8-6-15-14-15c-2,0-3,0.2-5,0.5c-3-5-8-8-14-8c-5,0-9,2-12,5c-5-11-16-18-29-18c-13,0-24,8-29,19c-3-3-7-5-12-5c-9,0-16,7-16,16c0,1,0.1,2,0.3,3c-8,5-13,14-13,25c0,17,14,30,30,30h90c17,0,30-14,30-30c0-12-4-22-16-22z'/%3E%3C/svg%3E"),url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 60'%3E%3Cpath fill='%23c5e4ed' opacity='0.4' d='M85,25c0-8-6-15-14-15c-2,0-3,0.2-5,0.5c-3-5-8-8-14-8c-5,0-9,2-12,5c-5-11-16-18-29-18c-13,0-24,8-29,19c-3-3-7-5-12-5c-9,0-16,7-16,16c0,1,0.1,2,0.3,3c-8,5-13,14-13,25c0,17,14,30,30,30h90c17,0,30-14,30-30c0-12-4-22-16-22z'/%3E%3C/svg%3E");background-size:400px 200px,300px 150px;background-position:0 0,200px 80px;animation:drift 120s linear infinite,drift2 90s linear infinite reverse;color:var(--text);min-height:100vh;line-height:1.5;padding-bottom:50px}
.header{background:#000;color:#fff;padding:15px 20px;text-align:center;border-bottom:6px solid #000;margin-bottom:30px;box-shadow:0 8px 0 rgba(0,0,0,0.2)}
.logo-svg{height:60px;width:auto}.logo-svg path,.logo-svg rect{fill:url(#rg)}
@keyframes rainbowShift{0%,100%{stop-color:#ff0000}14%{stop-color:#ff8800}28%{stop-color:#ffff00}42%{stop-color:#00ff00}57%{stop-color:#00ffff}71%{stop-color:#0088ff}85%{stop-color:#ff00ff}}.rs1{animation:rainbowShift 4s linear infinite}.rs2{animation:rainbowShift 4s linear infinite;animation-delay:-1.3s}.rs3{animation:rainbowShift 4s linear infinite;animation-delay:-2.6s}
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
.update-bubble{display:none;position:absolute;top:100%;right:10px;background:#ff6b6b;color:#fff;padding:8px 14px;border:3px solid #000;box-shadow:4px 4px 0 #000;font-size:0.8em;white-space:nowrap;z-index:100;animation:pulse 2s infinite}
.update-bubble.show{display:block}
.update-bubble a{color:#fff;text-decoration:underline}
@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.03)}}
.weather-title{position:relative;overflow:hidden;display:inline-block}
.weather-title::before{content:'\\2600\\FE0F';position:absolute;animation:sunRain 4s ease-in-out infinite}
.weather-title::after{content:'\\1F327\\FE0F';position:absolute;animation:sunRain 4s ease-in-out infinite;animation-delay:2s}
@keyframes sunRain{0%{left:-30px;opacity:0}15%{left:0;opacity:1}35%{left:100%;opacity:0}100%{left:100%;opacity:0}}
@keyframes drift{0%{background-position:0 0,200px 80px}100%{background-position:400px 200px,600px 230px}}@keyframes drift2{0%{background-position:200px 80px}100%{background-position:-100px -70px}}
@media(max-width:600px){.grid{grid-template-columns:1fr}.row{flex-direction:column}.row>*{width:100%}}
.lang-switch{position:absolute;top:10px;right:10px;display:flex;gap:4px}
.lang-btn{padding:6px 10px;border:2px solid #fff;background:transparent;color:#fff;font-weight:bold;cursor:pointer;font-size:0.8em;transition:all 0.2s}
.lang-btn:hover{background:rgba(255,255,255,0.2)}
.lang-btn.active{background:#fff;color:#000}
</style>
<script>
function showCard(c,p,el){if(el)el.style.background='#ffa500';fetch('/api/card?card='+c+'&preset='+p).then(function(r){return r.json();}).then(function(){if(el){el.style.background='#4ade80';setTimeout(function(){el.style.background='#fffacd';},500);}}).catch(function(){if(el)el.style.background='#f00';});}
function showFirstPreset(btn,card){showCard(card,0,btn);}
function saveTouchShortcut(){var sel=document.querySelector('select[name=touchShortcut]');var btn=event.target;btn.textContent='Saving...';fetch('/api/touch_shortcut?val='+sel.value).then(function(r){return r.json();}).then(function(){btn.textContent='\\u2705 Saved!';btn.style.background='#4ade80';setTimeout(function(){btn.textContent='\\uD83D\\uDCBE Save';btn.style.background='#4CAF50';},1500);}).catch(function(){btn.textContent='\\u274C Error';btn.style.background='#f00';});}
function setLang(lang){fetch('/api/lang?lang='+lang).then(function(r){return r.json();}).then(function(){location.reload();}).catch(function(){alert('Error changing language');});}
window.addEventListener('DOMContentLoaded', function(){
const lg=document.querySelector('.logo-svg linearGradient');
if(lg && !lg.getAttribute('id')) lg.setAttribute('id','rg');
document.querySelectorAll('.logo-svg path,.logo-svg rect').forEach(function(el){el.style.fill='url(#rg)';});
});
</script>
</head><body>
<div class="header" style="position:relative">
<div class="lang-switch"><button class="lang-btn )";
    html += (cfg.uiLang == 0) ? "active" : "";
    html += "\" onclick=\"setLang('en')\">EN</button><button class=\"lang-btn ";
    html += (cfg.uiLang == 1) ? "active" : "";
    html += "\" onclick=\"setLang('lv')\">LV</button></div>";
    html += "<div id=\"updateBubble\" class=\"update-bubble\">&#x1F680; <a href=\"https://makeriga.github.io/weatherthing-firmware/?lang=";
    html += (cfg.uiLang == 1) ? "lv" : "en";
    html += "\" target=\"_blank\">";
    html += TR("Update available!", "Pieejams atjauninājums!");
    html += R"(</a></div>
<svg class="logo-svg" viewBox="0 0 243 29" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient x1="0%%" y1="0%%" x2="100%%" y2="0%%"><stop offset="0%%" class="rs1"/><stop offset="50%%" class="rs2"/><stop offset="100%%" class="rs3"/></linearGradient></defs><g><path d="M11.545,16.062c-0.922,1.893-1.746,3.842-2.795,5.662 c-0.932,1.616-1.973,3.216-3.22,4.593c-1.633,1.803-3.694,1.406-4.508-0.903c-0.614-1.742-1.06-3.664-1.02-5.493 c0.104-4.712,0.499-9.419,0.826-14.125c0.078-1.115,0.57-2.02,1.933-1.735c1.251,0.261,1.25,1.215,1.172,2.26 c-0.335,4.509-0.675,9.019-0.893,13.534c-0.063,1.31,0.274,2.64,0.428,3.961c0.129,0.03,0.259,0.062,0.389,0.093 c0.408-0.498,0.855-0.97,1.217-1.5c3.037-4.461,4.97-9.447,6.865-14.451c0.154-0.405,0.287-0.817,0.442-1.223 c0.326-0.853,0.854-1.426,1.858-1.172c1.028,0.259,1.316,0.947,1.176,1.974c-0.278,2.052-0.589,4.11-0.675,6.175 c-0.092,2.206,0.041,4.422,0.118,6.633c0.018,0.521,0.16,1.05,0.321,1.551c0.372,1.158,0.767,2.417,2.128,2.689 c1.407,0.282,2.219-0.828,2.897-1.795c2.464-3.51,3.708-7.51,4.212-11.71c0.256-2.133,0.175-4.321,0.063-6.476 c-0.047-0.915-0.628-1.791-0.841-2.714c-0.123-0.532-0.199-1.451,0.064-1.613c0.463-0.286,1.52-0.395,1.766-0.104 c0.747,0.885,1.468,1.931,1.761,3.031c0.794,2.979,0.568,6.018,0.081,9.039c-0.644,3.993-1.918,7.769-4.068,11.194 c-0.73,1.164-1.676,2.261-2.733,3.135c-2.494,2.062-5.679,1.462-6.979-1.475c-0.838-1.893-1.108-4.05-1.526-6.109 c-0.188-0.924-0.114-1.9-0.16-2.852C11.745,16.11,11.645,16.086,11.545,16.062z"/><path d="M125.396,10.28c-0.787,0.44-1.24,0.694-1.694,0.947 c-0.69,0.385-1.433,0.397-1.667-0.407c-0.164-0.562-0.069-1.456,0.292-1.854c0.519-0.567,1.405-0.783,2.099-1.209 c0.788-0.483,1.493-1.118,2.315-1.523c0.541-0.267,1.23-0.35,1.845-0.323c0.804,0.033,1.597,0.359,2.398,0.378 c2.59,0.062,5.184-0.028,7.769,0.094c0.76,0.036,1.583,0.465,2.221,0.927c1.071,0.775,1.444,1.874,0.917,3.186 c-0.835,2.08-2.523,3.387-4.287,4.554c-1.559,1.032-3.259,1.852-4.898,2.76c-0.443,0.245-0.897,0.474-1.188,0.626 c3.227,1.791,6.419,3.543,9.586,5.339c0.531,0.301,1.19,0.711,1.362,1.216c0.19,0.557,0.089,1.453-0.27,1.875 c-0.246,0.29-1.355,0.275-1.724-0.031c-4.106-3.407-8.894-5.3-14.237-6.572c-0.154,1.285-0.316,2.551-0.455,3.819 c-0.062,0.572-0.031,1.157-0.125,1.724c-0.157,0.952-0.248,2.033-1.692,1.751c-1.071-0.21-1.741-1.24-1.519-2.394 c0.777-4.016,1.576-8.026,2.369-12.038C124.979,12.285,125.156,11.449,125.396,10.28z M126.591,18.329 c1.874-0.944,3.375-1.573,4.742-2.419c2.283-1.413,4.518-2.913,6.682-4.502c0.479-0.351,0.92-1.348,0.748-1.818 c-0.164-0.452-1.136-0.828-1.768-0.853c-1.677-0.065-3.371-0.016-5.041,0.154c-3.257,0.33-3.263,0.374-4.028,3.572 C127.495,14.262,127.104,16.069,126.591,18.329z"/><path d="M35.61,7.394c2.665-0.47,5.451-0.999,8.251-1.429 c0.736-0.113,1.586-0.078,2.259,0.194c0.441,0.18,0.658,0.909,0.976,1.393c-0.44,0.268-0.849,0.677-1.325,0.781 c-3.281,0.724-6.581,1.357-9.863,2.076c-1.34,0.293-2.118,1.187-2.414,2.557c-0.26,1.203-0.638,2.382-1.045,3.867 c1.539-0.425,2.758-0.778,3.986-1.096c1.819-0.47,3.64-0.933,5.469-1.358c0.557-0.129,1.205-0.306,1.7-0.136 c0.54,0.185,0.951,0.746,1.418,1.144c-0.411,0.378-0.76,0.952-1.242,1.102c-3.356,1.041-6.71,2.115-10.125,2.925 c-1.862,0.441-2.158,1.558-2.146,3.107c0.012,1.518,0.567,2.299,2.067,2.09c2.074-0.289,4.111-0.896,6.135-1.47 c0.869-0.247,1.631-0.877,2.502-1.101c0.479-0.123,1.333,0.072,1.557,0.42c0.238,0.37,0.024,1.126-0.173,1.645 c-0.114,0.299-0.556,0.552-0.904,0.672c-2.808,0.973-5.572,2.179-8.46,2.788c-3.644,0.769-6.164-1.483-5.563-5.139 c0.793-4.823,2.118-9.563,3.33-14.308c0.151-0.593,0.94-1.272,1.553-1.422C34.109,6.562,34.826,7.1,35.61,7.394z"/><path d="M109.328,7.453c3.009-0.532,5.791-1.049,8.583-1.501 c0.653-0.105,1.438-0.197,1.986,0.068c0.59,0.284,0.973,0.996,1.445,1.522c-0.527,0.28-1.024,0.701-1.585,0.819 c-3.052,0.642-6.127,1.169-9.177,1.815c-1.744,0.371-2.857,1.437-3.182,3.28c-0.187,1.059-0.536,2.089-0.863,3.326 c2.985-0.785,5.662-1.521,8.355-2.183c0.927-0.229,1.915-0.433,2.843-0.339c0.493,0.05,0.919,0.762,1.375,1.174 c-0.389,0.342-0.724,0.855-1.175,0.996c-3.673,1.148-7.372,2.211-11.052,3.336c-1.109,0.338-1.481,1.494-1.243,3.403 c0.147,1.188,1.003,1.606,1.953,1.439c2.068-0.362,4.118-0.868,6.143-1.431c0.867-0.242,1.661-0.757,2.477-1.172 c0.753-0.384,1.563-0.386,1.808,0.442c0.156,0.532-0.104,1.579-0.516,1.823c-3.176,1.878-6.564,3.323-10.312,3.386 c-2.932,0.048-4.734-2.054-4.409-5.198c0.285-2.764,0.861-5.509,1.47-8.226c0.473-2.107,1.082-4.2,1.837-6.222 C106.771,6.188,108.283,6.077,109.328,7.453z"/><path d="M87.137,17.21c3.134-0.485,5.988-0.89,8.82-1.412 c0.337-0.063,0.698-0.732,0.806-1.179c0.54-2.247,0.995-4.514,1.49-6.771c0.113-0.517,0.237-1.033,0.401-1.534 c0.366-1.12,1.22-1.334,2.195-0.979c0.941,0.343,1.028,1.047,0.754,2.003c-0.765,2.668-1.412,5.369-2.027,7.753 c0.728,0.66,1.172,1.063,1.616,1.467c-0.472,0.404-0.96,0.792-1.41,1.219c-0.318,0.302-0.783,0.625-0.844,0.994 c-0.423,2.582-1.393,5.119-0.571,7.824c0.124,0.408-0.375,1.267-0.812,1.509c-0.409,0.226-1.371,0.115-1.64-0.214 c-0.476-0.582-0.873-1.457-0.835-2.185c0.106-2.011,0.453-4.009,0.705-6.013c0.041-0.322,0.083-0.646,0.166-1.291 c-3.114,0.646-6.063,1.234-8.992,1.902c-0.281,0.064-0.601,0.614-0.641,0.968c-0.208,1.841-0.229,3.708-0.547,5.525 c-0.095,0.538-0.896,1.261-1.454,1.343c-0.988,0.145-1.841-0.439-1.716-1.612c0.152-1.433,0.483-2.846,0.701-4.273 c0.052-0.346,0.106-0.879-0.084-1.043c-1.263-1.085-0.382-1.939,0.202-2.863c0.361-0.573,0.824-1.151,0.959-1.788 c0.602-2.825,1.13-5.667,1.621-8.514c0.179-1.038,0.614-1.639,1.716-1.405c1.065,0.227,1.354,1.035,1.162,1.991 c-0.461,2.313-0.981,4.616-1.47,6.925C87.314,16.014,87.256,16.479,87.137,17.21z"/><path d="M59.188,7.013c2.275,2.074,2.008,4.98,2.495,7.628 c0.195,1.06-0.071,2.208,1.64,2.295c0.795,0.041,0.784,1.396-0.134,1.719c-0.975,0.342-0.94,0.922-0.856,1.662 c0.206,1.817,0.399,3.636,0.625,5.451c0.115,0.919-0.063,1.833-1.071,1.81c-0.515-0.012-1.349-0.829-1.452-1.39 c-0.396-2.164-0.556-4.37-0.833-6.819c-2.279,0.649-4.447,1.426-6.682,1.859c-2.233,0.434-3.518,1.634-4.171,3.74 c-0.326,1.052-0.725,2.117-1.307,3.04c-0.304,0.481-1.126,0.994-1.625,0.917c-0.9-0.14-1.307-0.968-0.992-1.897 c0.37-1.091,0.74-2.2,1.281-3.209c2.181-4.068,4.391-8.122,6.664-12.139c1.265-2.236,2.68-4.387,4.017-6.583 c0.586-0.963,1.236-1.875,2.505-1.084c1.237,0.771,0.657,1.709,0.069,2.596C59.283,6.724,59.249,6.866,59.188,7.013z M57.671,9.776 c-0.187,0-0.374,0-0.561,0c-1.677,2.799-3.354,5.597-5.026,8.387c0.279,0.179,0.324,0.233,0.359,0.227 c1.276-0.227,2.547-0.484,3.827-0.687c3.012-0.472,3.074-0.471,2.573-3.441C58.586,12.744,58.07,11.271,57.671,9.776z"/><path d="M78.417,6.242c-0.179,1.172-0.291,2.067-0.456,2.952 c-0.81,4.357-1.674,8.705-2.424,13.072c-0.175,1.017,0.157,2.117,0.006,3.143c-0.125,0.848-0.494,1.738-1.014,2.408 c-0.192,0.248-1.409,0.14-1.658-0.188c-0.463-0.607-0.869-1.544-0.758-2.259c0.908-5.852,1.941-11.685,2.933-17.524 c0.04-0.235,0.043-0.477,0.081-0.927c-0.463,0.063-0.878,0.05-1.243,0.181c-2.49,0.895-4.977,1.801-7.447,2.751 c-0.633,0.243-1.226,0.742-1.769-0.053c-0.55-0.804-0.622-1.699,0.137-2.362c0.601-0.525,1.356-0.935,2.109-1.219 c6.859-2.584,14.009-3.458,21.287-3.519c1.113-0.01,1.916,0.538,1.868,1.726c-0.044,1.104-0.861,1.295-1.855,1.313 c-2.795,0.053-5.588,0.192-8.381,0.316C79.313,6.077,78.797,6.189,78.417,6.242z"/></g><g><path d="M147.869,5.977c0,0.828,0,1.504,0,2.299c-0.793,0-1.514,0-2.362,0 c0-0.699,0-1.421,0-2.299C146.204,5.977,146.97,5.977,147.869,5.977z"/><path d="M151.616,5.973c0,0.761,0,1.472,0,2.299c-0.791,0-1.498,0-2.36,0 c-0.034-0.743-0.067-1.455-0.107-2.299C150.026,5.973,150.786,5.973,151.616,5.973z"/><path d="M155.288,5.99c0,0.733,0,1.398,0,2.291c-0.691,0.039-1.399,0.077-2.222,0.125 c0-0.862,0-1.579,0-2.416C153.773,5.99,154.479,5.99,155.288,5.99z"/><path d="M159.11,8.287c-0.847,0-1.514,0-2.309,0c0-0.781,0-1.486,0-2.333 c0.767-0.033,1.478-0.063,2.309-0.103C159.11,6.71,159.11,7.421,159.11,8.287z"/><path d="M223.285,15.829c0-0.811,0-1.477,0-2.26c0.774,0,1.485,0,2.338,0 c0,0.716,0,1.421,0,2.26C224.9,15.829,224.192,15.829,223.285,15.829z"/><path d="M167.444,8.235c0-0.751,0-1.46,0-2.281c0.77,0,1.477,0,2.315,0 c0,0.697,0,1.409,0,2.281C169.04,8.235,168.292,8.235,167.444,8.235z"/><path d="M179.501,5.909c0.813,0,1.482,0,2.295,0c0,0.765,0,1.479,0,2.326 c-0.737,0-1.448,0-2.295,0C179.501,7.526,179.501,6.811,179.501,5.909z"/><path d="M175.341,17.279c0.813,0,1.486,0,2.313,0c0,0.761,0,1.523,0,2.39 c-0.747,0-1.468,0-2.313,0C175.341,18.912,175.341,18.197,175.341,17.279z"/><path d="M190.5,8.195c0-0.828,0-1.496,0-2.261c0.807,0,1.522,0,2.347,0 c0,0.777,0,1.486,0,2.261C191.993,8.195,191.244,8.195,190.5,8.195z"/><path d="M194.319,5.929c0.831,0,1.504,0,2.268,0c0,0.805,0,1.527,0,2.368 c-0.726,0-1.437,0-2.268,0C194.319,7.452,194.319,6.691,194.319,5.929z"/><path d="M214.65,17.316c0.04,0.758,0.077,1.475,0.123,2.319c-0.872,0-1.584,0-2.382,0 c0-0.809,0-1.524,0-2.319C213.16,17.316,213.822,17.316,214.65,17.316z"/><path d="M216.118,8.235c0-0.772,0-1.484,0-2.289c0.805,0,1.521,0,2.36,0 c0,0.729,0,1.438,0,2.289C217.765,8.235,217.054,8.235,216.118,8.235z"/><path d="M228.999,8.303c-0.741,0-1.409,0-2.237,0c0-0.747,0-1.506,0-2.384 c0.678,0,1.391,0,2.237,0C228.999,6.651,228.999,7.369,228.999,8.303z"/><path d="M216.124,19.596c0-0.83,0-1.498,0-2.249c0.807,0,1.522,0,2.354,0 c0,0.783,0,1.49,0,2.249C217.628,19.596,216.871,19.596,216.124,19.596z"/><path d="M223.283,19.569c0-0.757,0-1.42,0-2.248c0.757-0.044,1.468-0.084,2.324-0.134 c0,0.818,0,1.526,0,2.382C224.906,19.569,224.202,19.569,223.283,19.569z"/><path d="M181.823,27.261c-0.823,0-1.494,0-2.309,0c0-0.785,0-1.542,0-2.385 c0.759,0,1.472,0,2.309,0C181.823,25.657,181.823,26.366,181.823,27.261z"/><path d="M153.048,27.179c0-0.798,0-1.514,0-2.316c0.803,0,1.52,0,2.362,0 c0,0.764,0,1.477,0,2.316C154.699,27.179,153.984,27.179,153.048,27.179z"/><path d="M167.424,24.849c0.793,0,1.512,0,2.402,0c0.035,0.753,0.069,1.475,0.111,2.356 c-0.854,0-1.622,0-2.514,0C167.424,26.458,167.424,25.697,167.424,24.849z"/><path d="M240.325,27.269c-0.813,0-1.48,0-2.265,0c0-0.785,0-1.504,0-2.349 c0.713,0,1.424,0,2.265,0C240.325,25.63,240.325,26.343,240.325,27.269z"/><path d="M203.649,12.102c-0.784,0-1.404,0-2.174,0c0-0.735,0-1.448,0-2.318 c0.668-0.036,1.383-0.075,2.174-0.117C203.649,10.54,203.649,11.297,203.649,12.102z"/><path d="M207.49,12.088c-0.816,0-1.484,0-2.345,0c-0.042-0.747-0.081-1.465-0.129-2.353 c0.838,0,1.597,0,2.474,0C207.49,10.444,207.49,11.164,207.49,12.088z"/><path d="M190.596,24.864c0.786,0,1.452,0,2.288,0c0.038,0.759,0.072,1.479,0.111,2.335 c-0.832,0-1.547,0-2.399,0C190.596,26.506,190.596,25.79,190.596,24.864z"/><path d="M225.668,9.747c0,0.807,0,1.473,0,2.263c-0.789,0-1.502,0-2.343,0 c0-0.715,0-1.43,0-2.263C224.019,9.747,224.735,9.747,225.668,9.747z"/><path d="M194.359,24.839c0.765,0,1.427,0,2.243,0c0,0.745,0,1.504,0,2.378 c-0.693,0-1.402,0-2.243,0C194.359,26.44,194.359,25.679,194.359,24.839z"/><path d="M167.442,13.473c0.843,0,1.511,0,2.293,0c0,0.765,0,1.47,0,2.318 c-0.763,0-1.47,0-2.293,0C167.442,15.067,167.442,14.358,167.442,13.473z"/><path d="M179.528,13.456c0.751,0,1.36,0,2.207,0c0.034,0.77,0.067,1.515,0.105,2.379 c-0.841,0-1.496,0-2.313,0C179.528,15.063,179.528,14.265,179.528,13.456z"/><path d="M167.331,9.785c0.749,0,1.359,0,2.206,0c0.035,0.769,0.067,1.514,0.107,2.378 c-0.843,0-1.498,0-2.313,0C167.331,11.393,167.331,10.594,167.331,9.785z"/><path d="M203.741,13.548c0,0.811,0,1.479,0,2.271c-0.775,0-1.484,0-2.303,0 c0-0.721,0-1.436,0-2.271C202.159,13.548,202.871,13.548,203.741,13.548z"/><path d="M208.658,13.446c0.838,0,1.512,0,2.285,0c0,0.795,0,1.517,0,2.368 c-0.803,0-1.521,0-2.285,0C208.658,15.006,208.658,14.247,208.658,13.446z"/><path d="M160.385,5.915c0.954,0,1.681,0,2.479,0c0.036,0.776,0.071,1.51,0.111,2.356 c-0.814,0-1.54,0-2.476,0C160.466,7.58,160.431,6.858,160.385,5.915z"/><path d="M189.094,5.931c0,0.791,0,1.512,0,2.354c-0.816,0-1.586,0-2.389,0 c0-0.765,0-1.5,0-2.354C187.443,5.931,188.156,5.931,189.094,5.931z"/><path d="M203.661,8.289c-0.834,0-1.444,0-2.245,0c-0.039-0.745-0.075-1.456-0.117-2.299 c0.821,0,1.53,0,2.362,0C203.661,6.686,203.661,7.389,203.661,8.289z"/><path d="M203.766,27.238c-0.789,0-1.514,0-2.382,0c-0.036-0.73-0.071-1.454-0.111-2.298 c0.858,0,1.625,0,2.493,0C203.766,25.693,203.766,26.416,203.766,27.238z"/><path d="M153.026,19.596c0-0.811,0-1.48,0-2.231c0.787,0,1.502,0,2.356,0 c0,0.735,0,1.435,0,2.231C154.627,19.596,153.915,19.596,153.026,19.596z"/><path d="M186.706,27.225c0-0.801,0-1.581,0-2.436c0.795,0,1.524,0,2.385,0 c0,0.776,0,1.546,0,2.436C188.311,27.225,187.541,27.225,186.706,27.225z"/><path d="M190.584,21.109c0.786,0,1.448,0,2.236,0c0,0.773,0,1.475,0,2.273 c-0.745,0-1.406,0-2.236,0C190.584,22.709,190.584,22.01,190.584,21.109z"/><path d="M190.546,12.185c0-0.903,0-1.567,0-2.39c0.721-0.042,1.379-0.081,2.195-0.131 c0,0.797,0,1.512,0,2.382C192.114,12.086,191.449,12.127,190.546,12.185z"/><path d="M201.473,17.343c0.785,0,1.509,0,2.303,0c0,0.741,0,1.41,0,2.217 c-0.727,0-1.438,0-2.303,0C201.473,18.924,201.473,18.219,201.473,17.343z"/><path d="M169.739,19.604c-0.342,0.054-0.58,0.125-0.818,0.123 c-0.429-0.004-0.858-0.05-1.427-0.089c0-0.749,0-1.457,0-2.281c0.707,0,1.415,0,2.245,0 C169.739,18.113,169.739,18.77,169.739,19.604z"/><path d="M155.418,15.809c-0.342,0.054-0.58,0.125-0.816,0.123 c-0.432-0.004-0.86-0.049-1.429-0.087c0-0.751,0-1.458,0-2.283c0.707,0,1.414,0,2.245,0 C155.418,14.318,155.418,14.977,155.418,15.809z"/><path d="M190.541,13.49c0.878,0,1.498,0,2.257,0c0,0.755,0,1.469,0,2.318 c-0.718,0-1.427,0-2.257,0C190.541,15.058,190.541,14.347,190.541,13.49z"/><path d="M155.422,21.1c0,0.797,0,1.454,0,2.295c-0.791,0.035-1.498,0.067-2.36,0.107 c-0.052-0.771-0.098-1.423-0.159-2.285C153.761,21.177,154.556,21.142,155.422,21.1z"/><path d="M214.671,23.627c-0.745,0-1.456,0-2.291,0c0-0.737,0-1.438,0-2.257 c0.741,0,1.455,0,2.291,0C214.671,22.087,214.671,22.79,214.671,23.627z"/><path d="M218.385,15.795c-0.747,0-1.457,0-2.291,0c0-0.737,0-1.438,0-2.259 c0.739,0,1.452,0,2.291,0C218.385,14.255,218.385,14.959,218.385,15.795z"/><path d="M218.385,12c-0.747,0-1.457,0-2.291,0c0-0.737,0-1.438,0-2.257 c0.739,0,1.452,0,2.291,0C218.385,10.461,218.385,11.164,218.385,12z"/><path d="M192.799,19.619c-0.772,0-1.429,0-2.209,0c0-0.788,0-1.494,0-2.288 c0.753,0,1.41,0,2.209,0C192.799,18.026,192.799,18.733,192.799,19.619z"/><path d="M173.625,19.619c-0.771,0-1.429,0-2.21,0c0-0.788,0-1.494,0-2.288 c0.755,0,1.411,0,2.21,0C173.625,18.026,173.625,18.733,173.625,19.619z"/><path d="M181.662,19.619c-0.771,0-1.429,0-2.209,0c0-0.788,0-1.494,0-2.288 c0.755,0,1.412,0,2.209,0C181.662,18.026,181.662,18.733,181.662,19.619z"/><path d="M216.086,27.209c0-0.769,0-1.494,0-2.36c0.818,0.036,1.55,0.069,2.407,0.107 c0.038,0.776,0.07,1.446,0.107,2.253C217.735,27.209,216.96,27.209,216.086,27.209z"/><path d="M232.677,5.813c0,0.97,0,1.692,0,2.499c-0.786,0-1.446,0-2.261,0 c-0.046-0.788-0.083-1.508-0.131-2.39C231.142,5.883,231.859,5.852,232.677,5.813z"/><path d="M234.039,17.216c0.749,0,1.427,0,2.253,0c0,0.792,0,1.569,0,2.463 c-0.743,0-1.454,0-2.253,0C234.039,18.885,234.039,18.155,234.039,17.216z"/><path d="M228.875,27.263c-0.736,0-1.402,0-2.201,0c0-0.789,0-1.558,0-2.426 c0.724,0,1.389,0,2.201,0C228.875,25.619,228.875,26.393,228.875,27.263z"/><path d="M181.798,9.854c0,0.691,0,1.354,0,2.138c-0.774,0-1.486,0-2.307,0 c-0.041-0.725-0.079-1.391-0.121-2.138C180.209,9.854,180.955,9.854,181.798,9.854z"/><path d="M223.294,21.227c0.776,0,1.502,0,2.334,0c0,0.718,0,1.371,0,2.261 c-0.721,0.036-1.476,0.076-2.334,0.117C223.294,22.719,223.294,22.008,223.294,21.227z"/><path d="M234.257,5.907c0.751,0,1.414,0,2.163,0c0,0.801,0,1.527,0,2.372 c-0.719,0-1.372,0-2.163,0C234.257,7.536,234.257,6.814,234.257,5.907z"/><path d="M201.285,21.282c0.95,0,1.667,0,2.523,0c-0.044,0.735-0.08,1.381-0.121,2.112 c-0.785,0-1.44,0-2.279,0C201.373,22.745,201.333,22.099,201.285,21.282z"/><path d="M179.498,23.374c0-0.727,0-1.37,0-2.098c0.735-0.031,1.403-0.062,2.331-0.102 c-0.034,0.771-0.063,1.448-0.098,2.199C180.911,23.374,180.261,23.374,179.498,23.374z"/><path d="M218.451,23.408c-0.785,0-1.502,0-2.325,0c0-0.667,0-1.266,0-1.999 c0.751,0,1.469,0,2.325,0C218.451,22.018,218.451,22.661,218.451,23.408z"/><path d="M240.162,19.638c-0.793,0-1.4,0-2.201,0c-0.036-0.762-0.067-1.469-0.105-2.293 c0.829,0,1.488,0,2.307,0C240.162,18.05,240.162,18.755,240.162,19.638z"/><path d="M153.122,10.012c0.809,0,1.473,0,2.249,0c0.028,0.667,0.056,1.266,0.088,2.021 c-0.795,0-1.502,0-2.337,0C153.122,11.396,153.122,10.711,153.122,10.012z"/><path d="M237.997,8.239c0-0.783,0-1.494,0-2.313c0.734,0,1.381,0,2.151,0 c0,0.743,0,1.452,0,2.313C239.504,8.239,238.861,8.239,237.997,8.239z"/><path d="M169.683,23.428c-0.772,0-1.442,0-2.243,0c0-0.713,0-1.36,0-2.117 c0.741-0.032,1.413-0.063,2.243-0.102C169.683,21.938,169.683,22.58,169.683,23.428z"/><path d="M232.62,24.856c0,0.851,0,1.563,0,2.372c-0.736,0-1.389,0-2.191,0 c0-0.757,0-1.51,0-2.372C231.098,24.856,231.746,24.856,232.62,24.856z"/><path d="M236.472,27.259c-0.868,0-1.472,0-2.239,0c0-0.787,0-1.544,0-2.402 c0.67,0,1.317,0,2.116,0C236.387,25.623,236.424,26.341,236.472,27.259z"/><path d="M238.014,23.349c0-0.701,0-1.287,0-1.984c0.739-0.04,1.395-0.076,2.199-0.119 c0,0.723,0,1.356,0,2.104C239.548,23.349,238.894,23.349,238.014,23.349z"/></g></svg>
<div class="status-bar" style="margin-top:12px">
<div class="status-item"><span class="status-dot )";

    html += g_isApMode ? "ap" : "online";
    html += R"("></span><span>)";
    html += g_isApMode ? TR("Access Point Mode", "Piekļuves punkta režīms") : TR("Connected to WiFi", "Savienots ar WiFi");
    html += R"(</span></div>)";
    
    if (!g_isApMode) {
        html += "<div class='status-item'><strong>IP:</strong> " + WiFi.localIP().toString() + "</div>";
    }
    
    html += R"(</div></div>
<div class="container">)";

    // Card names and icons for 16 cards (12 original + 4 timers)
    const char* cardNames[] = {
        cfg.uiLang == 1 ? "<span class='weather-title'>Laiks</span>" : "<span class='weather-title'>Weather</span>",
        cfg.uiLang == 1 ? "Pulkstenis" : "Clock",
        "BTC", "Stocks", "Network",
        cfg.uiLang == 1 ? "Audio" : "Audio",
        "(Audio)", "(Audio)",
        cfg.uiLang == 1 ? "Spēles" : "Games",
        "MQTT", "RSS", "YouTube",
        cfg.uiLang == 1 ? "Atskaite" : "Countdown",
        "Pomodoro",
        cfg.uiLang == 1 ? "Saule" : "Sun",
        cfg.uiLang == 1 ? "Hronometrs" : "Stopwatch"
    };
    const char* cardIcons[] = {"&#x26C5;", "&#x1F551;", "&#x20BF;", "&#x1F4C8;", "&#x1F310;", "&#x1F3A4;", "", "", "&#x1F3AE;", "&#x1F3E0;", "&#x1F4F0;", "&#x25B6;", "&#x23F1;", "&#x1F345;", "&#x2600;", "&#x23F1;"};
    
    // ========== WiFi FIRST when in AP mode ==========
    if (g_isApMode) {
        html += "<div class=\"card\" style=\"margin-bottom:30px;background:#fffacd\">";
        html += "<div class=\"card-header\"><span class=\"card-icon\">&#x26A0;</span><span class=\"card-title\">";
        html += TR("WiFi Setup Required", "Nepieciešams WiFi iestatījums");
        html += "</span></div>";
        html += "<p style=\"margin-bottom:15px;font-weight:bold\">";
        html += TR("Connect to your home WiFi to use the weather display!", "Savienojiet ar mājas WiFi, lai izmantotu laika displeju!");
        html += "</p>";
        html += "<form method=\"POST\" action=\"/wifi\">";
        html += "<div class=\"form-group\"><label>";
        html += TR("Network Name (SSID)", "Tīkla nosaukums (SSID)");
        html += "</label><input name=\"ssid\" value=\"" + g_ssid + "\" placeholder=\"";
        html += TR("Enter WiFi network name", "Ievadiet WiFi tīkla nosaukumu");
        html += "\"></div>";
        html += "<div class=\"form-group\"><label>";
        html += TR("Password", "Parole");
        html += "</label><input name=\"pass\" type=\"password\" placeholder=\"";
        html += TR("Enter WiFi password", "Ievadiet WiFi paroli");
        html += "\"></div>";
        html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; ";
        html += TR("Save WiFi Settings", "Saglabāt WiFi iestatījumus");
        html += "</button>";
        html += "</form></div>";
        sendChunk();
    }

    // ========== QUICK SETUP - Location, WiFi, Timezone at TOP ==========
    html += "<div class=\"card\" style=\"margin-bottom:30px;background:#e8f4fc;border:5px solid #0088cc\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x2699;</span><span class=\"card-title\">";
    html += TR("Quick Setup", "Ātrā iestatīšana");
    html += "</span></div>";
    html += "<p style=\"margin-bottom:15px;font-weight:bold;color:#333\">&#x1F4A1; ";
    html += TR("Configure these essential settings first for best experience!", "Vispirms konfigurējiet šos būtiskos iestatījumus!");
    html += "</p>";
    
    html += "<form method=\"POST\" action=\"/cards_config\" style=\"display:grid;gap:20px\">";
    
    // Location Section
    html += "<div style=\"background:#fff;border:3px solid #000;padding:15px\">";
    html += "<div style=\"font-weight:900;font-size:1.1em;margin-bottom:10px\">&#x1F4CD; ";
    html += TR("Location", "Atrašanās vieta");
    html += "</div>";
    float lat, lon;
    weather_get_location(&lat, &lon);
    html += "<p style=\"font-size:0.85em;color:#666;margin-bottom:8px\">";
    html += TR("Current", "Pašreizējā");
    html += ": <b>" + String(lat, 4) + ", " + String(lon, 4) + "</b> (";
    html += TR("Default: Riga, Latvia", "Noklusējums: Rīga, Latvija");
    html += ")</p>";
    html += "<div style=\"display:flex;gap:10px;flex-wrap:wrap;align-items:center\">";
    html += "<input name=\"city\" placeholder=\"";
    html += TR("Enter city name", "Ievadiet pilsētas nosaukumu");
    html += "\" style=\"flex:1;min-width:150px;padding:10px\">";
    html += "<span style=\"font-weight:bold\">";
    html += TR("OR", "VAI");
    html += "</span>";
    html += "<input name=\"lat\" type=\"number\" step=\"0.0001\" placeholder=\"";
    html += TR("Latitude", "Platums");
    html += "\" value=\"" + String(lat, 4) + "\" style=\"width:100px;padding:10px\">";
    html += "<input name=\"lon\" type=\"number\" step=\"0.0001\" placeholder=\"";
    html += TR("Longitude", "Garums");
    html += "\" value=\"" + String(lon, 4) + "\" style=\"width:100px;padding:10px\">";
    html += "</div></div>";
    
    // WiFi Section (if not in AP mode)
    if (!g_isApMode) {
        html += "<div style=\"background:#fff;border:3px solid #000;padding:15px\">";
        html += "<div style=\"font-weight:900;font-size:1.1em;margin-bottom:10px\">&#x1F4F6; WiFi</div>";
        if (g_hasCreds) {
            html += "<p style=\"font-size:0.85em;color:#666;margin-bottom:8px\">";
            html += TR("Connected to", "Savienots ar");
            html += ": <b>" + g_ssid + "</b> (IP: " + WiFi.localIP().toString() + ")</p>";
        }
        html += "<div style=\"display:flex;gap:10px;flex-wrap:wrap\">";
        html += "<input name=\"ssid\" placeholder=\"";
        html += TR("Network name (SSID)", "T\u012bkla nosaukums (SSID)");
        html += "\" value=\"" + g_ssid + "\" style=\"flex:1;min-width:150px;padding:10px\">";
        html += "<input name=\"pass\" type=\"password\" placeholder=\"";
        html += TR("Password", "Parole");
        html += "\" style=\"flex:1;min-width:150px;padding:10px\">";
        html += "</div></div>";
    }
    
    // Timezone Section
    html += "<div style=\"background:#fff;border:3px solid #000;padding:15px\">";
    html += "<div style=\"font-weight:900;font-size:1.1em;margin-bottom:10px\">&#x1F570; ";
    html += TR("Timezone", "Laika josla");
    html += "</div>";
    html += "<p style=\"font-size:0.85em;color:#666;margin-bottom:8px\">";
    html += TR("Current", "Pa\u0161reiz\u0113j\u0101");
    html += ": <b>GMT" + String(cfg.tzOffset >= 0 ? "+" : "") + String(cfg.tzOffset) + "</b> (";
    html += TR("Default: GMT+2 Riga", "Noklus\u0113jums: GMT+2 R\u012bga");
    html += ")</p>";
    html += "<div style=\"display:flex;gap:10px;align-items:center\">";
    html += "<select name=\"tz\" style=\"padding:10px;min-width:200px\">";
    for (int8_t tz = -12; tz <= 14; ++tz) {
        html += "<option value=\"" + String(tz) + "\"";
        if (cfg.tzOffset == tz) html += " selected";
        html += ">GMT" + String(tz >= 0 ? "+" : "") + String(tz);
        // Add common city labels
        if (tz == -8) html += " (Los Angeles)";
        else if (tz == -5) html += " (New York)";
        else if (tz == 0) html += " (London)";
        else if (tz == 1) html += " (Paris, Berlin)";
        else if (tz == 2) html += " (Riga, Helsinki)";
        else if (tz == 3) html += " (Moscow)";
        else if (tz == 8) html += " (Singapore)";
        else if (tz == 9) html += " (Tokyo)";
        html += "</option>";
    }
    html += "</select></div></div>";
    
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\" onclick=\"saveOrder()\" style=\"font-size:1.1em;padding:15px\">&#x1F4BE; ";
    html += TR("Save Quick Setup", "Saglab\u0101t \u0100tros iestat\u012bjumus");
    html += "</button>";
    html += "</form></div>";
    sendChunk();

    // ========== CARD GALLERY - Full width section ==========
    html += "<div class=\"card\" style=\"margin-bottom:30px\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F3AC;</span><span class=\"card-title\">";
    html += TR("Card Gallery", "Kar\u0161u galerija");
    html += "</span></div>";
    
    // ========== EXPLAINER BLOCK ==========
    html += "<div style=\"background:linear-gradient(135deg,#e8f4fc,#f0f8e8);border:3px solid #2196F3;border-radius:12px;padding:20px;margin-bottom:20px\">";
    html += "<div style=\"font-size:1.2em;font-weight:bold;margin-bottom:15px;color:#1565C0\">&#x1F4A1; ";
    html += TR("How It Works", "K\u0101 tas str\u0101d\u0101");
    html += "</div>";
    
    // Visual guide with icons
    html += "<div style=\"display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:15px\">";
    
    // Cards explanation
    html += "<div style=\"background:#fff;border:2px solid #4CAF50;border-radius:8px;padding:12px;text-align:center\">";
    html += "<div style=\"font-size:2em\">&#x1F4E6;</div>";
    html += "<div style=\"font-weight:bold;color:#2E7D32\">";
    html += TR("Cards", "Kartes");
    html += "</div>";
    html += "<div style=\"font-size:0.85em;color:#555\">";
    html += TR("Different display modes like Weather, Clock, Bitcoin price", "Da\u017e\u0101di displeja re\u017e\u012bmi k\u0101 Laiks, Pulkstenis, Bitcoin cena");
    html += "</div>";
    html += "</div>";
    
    // Presets explanation
    html += "<div style=\"background:#fff;border:2px solid #FF9800;border-radius:8px;padding:12px;text-align:center\">";
    html += "<div style=\"font-size:2em\">&#x1F3A8;</div>";
    html += "<div style=\"font-weight:bold;color:#E65100\">";
    html += TR("Presets", "Iestat\u012bjumi");
    html += "</div>";
    html += "<div style=\"font-size:0.85em;color:#555\">";
    html += TR("Visual styles within each card - different looks for the same info", "Vizu\u0101lie stili katr\u0101 kart\u0113 - da\u017e\u0101ds izskats vienai inform\u0101cijai");
    html += "</div>";
    html += "</div>";
    
    // Rotation explanation
    html += "<div style=\"background:#fff;border:2px solid #9C27B0;border-radius:8px;padding:12px;text-align:center\">";
    html += "<div style=\"font-size:2em\">&#x1F503;</div>";
    html += "<div style=\"font-weight:bold;color:#7B1FA2\">";
    html += TR("Auto-Rotate", "Automātiskā rotācija");
    html += "</div>";
    html += "<div style=\"font-size:0.85em;color:#555\">";
    html += TR("Automatically cycles through your checked favorites", "Automātiski pārslēdz jūsu atzīmētos favorītus");
    html += "</div>";
    html += "</div>";
    
    html += "</div>"; // End grid
    sendChunk();
    
    // Step by step instructions
    html += "<div style=\"background:#fff;border-radius:8px;padding:15px;border:2px dashed #999\">";
    html += "<div style=\"font-weight:bold;margin-bottom:10px\">&#x1F446; ";
    html += TR("Quick Guide:", "\u012as\u0101 pam\u0101c\u012bba:");
    html += "</div>";
    html += "<div style=\"display:flex;flex-wrap:wrap;gap:10px;font-size:0.9em\">";
    html += "<span style=\"background:#E3F2FD;padding:5px 10px;border-radius:15px\">&#x2630; ";
    html += TR("<b>Drag</b> cards to reorder", "<b>Velciet</b> kartes, lai p\u0101rk\u0101rtotu");
    html += "</span>";
    html += "<span style=\"background:#E8F5E9;padding:5px 10px;border-radius:15px\">&#x2611; ";
    html += TR("<b>Check</b> to include in rotation", "<b>Atz\u012bm\u0113jiet</b>, lai iek\u013cautu rota\u0161an\u0101");
    html += "</span>";
    html += "<span style=\"background:#FFF3E0;padding:5px 10px;border-radius:15px\">&#x25B6; ";
    html += TR("<b>Click preset</b> to preview", "<b>Noklik\u0161\u0137iniet</b> priek\u0161skat\u012bjumam");
    html += "</span>";
    html += "<span style=\"background:#FCE4EC;padding:5px 10px;border-radius:15px\">&#x1F500; ";
    html += TR("Use <b>buttons</b> to switch manually", "Izmantojiet <b>pogas</b> manu\u0101lai p\u0101rsl\u0113g\u0161anai");
    html += "</span>";
    html += "</div></div>";
    
    html += "</div>"; // End explainer block
    sendChunk();
    
    // Auto-cycle controls
    html += "<form method=\"POST\" action=\"/cards_config\" id=\"cardForm\">";
    html += "<div style=\"display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:10px;padding:15px;background:#f8f8f8;border:3px solid #000\">";
    html += "<label style=\"font-weight:bold;display:flex;align-items:center;gap:8px\"><input type=\"checkbox\" name=\"cycleOn\" value=\"1\"" + String(cfg.cycleEnabled ? " checked" : "") + " style=\"width:20px;height:20px\"> ";
    html += TR("Auto-Cycle", "Autom\u0101tisk\u0101 main\u012b\u0161ana");
    html += "</label>";
    html += "<div style=\"display:flex;align-items:center;gap:8px\"><span style=\"font-weight:bold\">";
    html += TR("Every", "Ik p\u0113c");
    html += "</span><input type=\"number\" name=\"cycleDur\" value=\"" + String(cfg.cycleDuration) + "\" min=\"3\" max=\"3600\" style=\"width:80px\"><span style=\"font-weight:bold\">";
    html += TR("sec", "sek");
    html += "</span></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary\" onclick=\"saveOrder()\" style=\"margin-left:auto\">&#x1F4BE; ";
    html += TR("Save Config", "Saglab\u0101t konfigur\u0101ciju");
    html += "</button>";
    html += "</div>";
    
    // Transition & Demo Mode controls
    html += "<div style=\"display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:20px;padding:15px;background:#e8f4e8;border:3px solid #000\">";
    html += "<span style=\"font-weight:bold\">&#x1F3AC; ";
    html += TR("Transitions:", "P\u0101rejas:");
    html += "</span>";
    html += "<label style=\"display:flex;align-items:center;gap:6px\"><input type=\"checkbox\" name=\"trTitle\" value=\"1\"" + String(cfg.showTransitionTitle ? " checked" : "") + " style=\"width:18px;height:18px\"> ";
    html += TR("Titles", "Virsraksti");
    html += "</label>";
    html += "<label style=\"display:flex;align-items:center;gap:6px\"><input type=\"checkbox\" name=\"trAnim\" value=\"1\"" + String(cfg.showTransitionAnim ? " checked" : "") + " style=\"width:18px;height:18px\"> ";
    html += TR("Animations", "Anim\u0101cijas");
    html += "</label>";
    html += "<span style=\"border-left:2px solid #999;height:24px;margin:0 8px\"></span>";
    html += "<span style=\"font-weight:bold;color:#d00\">&#x1F3A5; Demo:</span>";
    html += "<label style=\"display:flex;align-items:center;gap:6px\"><input type=\"checkbox\" name=\"demoOn\" value=\"1\"" + String(cfg.demoMode ? " checked" : "") + " style=\"width:18px;height:18px\"> ";
    html += TR("Enable", "Iesp\u0113jot");
    html += "</label>";
    html += "<span style=\"font-weight:bold\">";
    html += TR("Uses Auto-Cycle interval", "Izmanto autom\u0101tisk\u0101s mai\u0146as intervalu");
    html += "</span>";
    html += "</div>";
    
    // Touch Shortcut control
    html += "<div style=\"display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:20px;padding:15px;background:#fff3e0;border:3px solid #FF9800\">";
    html += "<span style=\"font-size:1.5em\">&#x1F446;</span>";
    html += "<span style=\"font-weight:bold\">";
    html += TR("Touch Shortcut:", "Pieskāriena sa\u012bsne:");
    html += "</span>";
    html += "<select name=\"touchShortcut\" style=\"padding:8px;min-width:200px;font-size:0.95em\">";
    
    // Disabled option
    html += "<option value=\"255_0\"";
    if (cfg.touchShortcutCard == 0xFF) html += " selected";
    html += ">&#x274C; ";
    html += TR("Disabled", "Atspējots");
    html += "</option>";
    
    // Weather presets - first half
    html += "<optgroup label=\"&#x1F321; Weather\">";
    const char* wxPresets[] = {"Classic", "Bar", "Corner", "Anim", "Minimal", "Day/Nite", "Term", "Big", "Forecast", "Pixel", "LCD", "Mood", "Type", "Waves", "Split", "Count", "Thermo", "Icon", "Rain", "Cyber", "Particle", "Wave", "TempBar", "Aurora", "Radar", "Glitch", "Horizon", "Frost", "Map", "Grid", "Heat", "Compass", "Gauge", "Stars", "Seasons", "Half", "Edge", "PCB", "Stripe", "Scan", "Cine"};
    for (int p = 0; p < 20; ++p) {
        html += "<option value=\"0_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 0 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(wxPresets[p]) + "</option>";
    }
    sendChunk();
    // Weather presets - second half
    for (int p = 20; p < 41; ++p) {
        html += "<option value=\"0_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 0 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(wxPresets[p]) + "</option>";
    }
    html += "</optgroup>";
    sendChunk();
    
    // Clock presets
    html += "<optgroup label=\"&#x1F552; Clock\">";
    const char* clkPresets[] = {"Digital", "Binary", "Minimal", "Bars", "Nixie", "Glitch", "Pong", "Word", "Bounce", "Matrix", "Radar", "Flip", "Cyber", "Analog", "Countdown", "DotMatrix", "Gradient", "Segments", "Orbit", "Tally", "Cutout", "Scan", "Duo", "Frame", "Date", "FullDate", "Weekday", "Nameday", "WeekNum", "Poland"};
    for (int p = 0; p < 15; ++p) {
        html += "<option value=\"1_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 1 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(clkPresets[p]) + "</option>";
    }
    sendChunk();
    for (int p = 15; p < 30; ++p) {
        html += "<option value=\"1_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 1 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(clkPresets[p]) + "</option>";
    }
    html += "</optgroup>";
    
    // VU Meter presets
    html += "<optgroup label=\"&#x1F3B5; VU Meter\">";
    const char* vuPresets[] = {"Spectrum", "Waveform", "Fire", "Pulse", "Waterfall", "Strobe", "Plasma", "Balls", "Matrix", "Rainbow", "Mirror", "Laser", "Dancer", "Heartbeat", "Traffic", "Pacman", "Vortex", "EQ", "Disco", "Fireworks", "PixelRain", "Nyan", "Ocean", "Tetris", "Starfield", "Lava", "Geometry", "Sparkle", "Aurora", "Lightning", "Ripple", "DNA", "Kaleidoscope", "Snake", "Lissajous", "Barcode", "Orbitals", "Checker", "Shards"};
    for (int p = 0; p < 20; ++p) {
        html += "<option value=\"5_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 5 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(vuPresets[p]) + "</option>";
    }
    sendChunk();
    for (int p = 20; p < 39; ++p) {
        html += "<option value=\"5_" + String(p) + "\"";
        if (cfg.touchShortcutCard == 5 && cfg.touchShortcutPreset == p) html += " selected";
        html += ">" + String(vuPresets[p]) + "</option>";
    }
    html += "</optgroup>";
    sendChunk();
    
    // Other cards (single preset each)
    html += "<optgroup label=\"&#x1F4B0; Finance\">";
    html += "<option value=\"2_0\"" + String(cfg.touchShortcutCard == 2 ? " selected" : "") + ">Bitcoin</option>";
    html += "<option value=\"3_0\"" + String(cfg.touchShortcutCard == 3 ? " selected" : "") + ">Stock</option>";
    html += "</optgroup>";
    
    html += "<optgroup label=\"&#x1F3AE; Other\">";
    html += "<option value=\"4_0\"" + String(cfg.touchShortcutCard == 4 ? " selected" : "") + ">Network Info</option>";
    html += "<option value=\"8_0\"" + String(cfg.touchShortcutCard == 8 ? " selected" : "") + ">Games</option>";
    html += "<option value=\"9_0\"" + String(cfg.touchShortcutCard == 9 ? " selected" : "") + ">MQTT</option>";
    html += "<option value=\"10_0\"" + String(cfg.touchShortcutCard == 10 ? " selected" : "") + ">RSS</option>";
    html += "<option value=\"11_0\"" + String(cfg.touchShortcutCard == 11 ? " selected" : "") + ">YouTube</option>";
    html += "</optgroup>";
    
    html += "<optgroup label=\"&#x23F1; Timers\">";
    html += "<option value=\"12_0\"" + String(cfg.touchShortcutCard == 12 ? " selected" : "") + ">Countdown</option>";
    html += "<option value=\"13_0\"" + String(cfg.touchShortcutCard == 13 ? " selected" : "") + ">Pomodoro</option>";
    html += "<option value=\"14_0\"" + String(cfg.touchShortcutCard == 14 ? " selected" : "") + ">Sunrise/Sunset</option>";
    html += "<option value=\"15_0\"" + String(cfg.touchShortcutCard == 15 ? " selected" : "") + ">Stopwatch</option>";
    html += "</optgroup>";
    
    html += "</select>";
    html += "<button type=\"button\" onclick=\"saveTouchShortcut()\" style=\"background:#4CAF50;color:#fff;border:2px solid #000;padding:8px 16px;font-weight:bold;cursor:pointer\">&#x1F4BE; ";
    html += TR("Save", "Saglabāt");
    html += "</button>";
    html += "<span style=\"font-size:0.85em;color:#666\">";
    html += TR("Quick access when you tap the touch sensor", "Ātra piekļuve, pieskaroties sensora pogai");
    html += "</span>";
    html += "</div>";
    sendChunk();
    
    // Card gallery grid
    html += "<p style=\"font-size:1em;margin-bottom:15px;padding:12px;background:#fffacd;border:3px solid #000;font-weight:bold\">&#x2630; ";
    html += TR("Drag cards to reorder", "Velc kartītes, lai pārkārtotu");
    html += " &bull; ";
    html += TR("Click preset to show on display", "Klikšķini uz stila, lai parādītu");
    html += " &bull; &#x1F503; ";
    html += TR("Toggle to include in auto-cycle", "Ieslēdz, lai iekļautu rotācijā");
    html += "</p>";
    html += "<div id=\"cardGallery\" style=\"display:flex;flex-direction:column;gap:15px\">";
    
    // Generate cards in order (skip Sparkle=6, Aurora=7 as VU-only)
    uint16_t seenCards = 0; // Bitmask to track which cards we've already shown
    for(int i=0; i<16; ++i) {
        uint8_t cardIdx = cfg.cardOrder[i];
        if(cardIdx > 15) continue; // Skip invalid entries
        if(cardIdx == 6 || cardIdx == 7) continue; // Skip Sparkle/Aurora (VU-only)
        if(seenCards & (1 << cardIdx)) continue; // Skip duplicates
        seenCards |= (1 << cardIdx); // Mark as seen
        bool enabled = cfg.cardEnabled[cardIdx];
        
        html += "<div class=\"gallery-card\" draggable=\"true\" data-idx=\"" + String(cardIdx) + "\" style=\"background:#fff;border:4px solid #000;padding:12px;cursor:grab;opacity:" + String(enabled ? "1" : "0.5") + "\">";
        
        // Header with icon, name, show button, and auto-rotate checkbox
        html += "<div style=\"display:flex;align-items:center;gap:10px;padding-bottom:10px;border-bottom:2px solid #000\" class=\"card-hdr\">";
        html += "<span style=\"font-size:1.5em;cursor:grab\" class=\"drag-handle\">&#x2630;</span>";
        html += "<span style=\"font-size:1.4em\">" + String(cardIcons[cardIdx]) + "</span>";
        html += "<span style=\"font-weight:900;font-size:1.1em;text-transform:uppercase;flex:1\">" + String(cardNames[cardIdx]) + "</span>";
        // Show button (visible when collapsed)
        html += "<button type=\"button\" class=\"show-btn\" onclick=\"showFirstPreset(this," + String(cardIdx) + ")\" style=\"background:#4ade80;border:2px solid #000;padding:4px 10px;font-weight:bold;cursor:pointer;display:none\">&#x25B6; ";
        html += TR("Show", "Rādīt");
        html += "</button>";
        // Prominent auto-rotate checkbox
        html += "<label style=\"display:flex;align-items:center;gap:4px;background:" + String(enabled ? "#4ade80" : "#ff6b6b") + ";padding:3px 8px;border:2px solid #000;font-size:0.7em;font-weight:bold;cursor:pointer\">";
        html += "<input type=\"checkbox\" name=\"en_" + String(cardIdx) + "\" value=\"1\"" + String(enabled ? " checked" : "") + " style=\"width:16px;height:16px\" onchange=\"this.closest('.gallery-card').style.opacity=this.checked?1:0.5;this.parentElement.style.background=this.checked?'#4ade80':'#ff6b6b'\">";
        html += "&#x1F503;</label>";
        // Collapse toggle
        html += "<button type=\"button\" class=\"collapse-btn\" onclick=\"toggleCollapse(this)\" style=\"background:#000;color:#fff;border:2px solid #000;padding:4px 8px;font-weight:bold;cursor:pointer\">&#x25BC;</button>";
        html += "</div>";
        
        // Collapsible content wrapper
        html += "<div class=\"card-body\" style=\"margin-top:10px\">";
        // Quick action row
        html += "<div style=\"display:flex;align-items:center;gap:10px;margin:6px 0 12px\">";
        html += "<button type=\"button\" onclick=\"clearPresets(" + String(cardIdx) + ",this)\" style=\"background:#fff;color:#000;border:2px solid #000;padding:6px 10px;font-weight:bold;box-shadow:3px 3px 0 #000;cursor:pointer\">&#x274C; ";
        html += TR("Uncheck all presets", "Noņemt atzīmes visiem stiliem");
        html += "</button>";
        html += "<span style=\"font-size:0.85em;color:#444;max-width:360px;line-height:1.4\">";
        html += TR("Clears every box so this card stays out of auto-rotation until you pick presets again.", "Noņem visas atzīmes, lai šī karte nerādītos rotācijā, līdz atkal izvēlēsies stilus.");
        html += "</span></div>";
        // Preset buttons with checkboxes for rotation inclusion
        html += "<div style=\"display:flex;flex-wrap:wrap;gap:8px\">";
        
        // Helper lambda to generate preset button with checkbox
        auto presetBtn = [&](uint8_t card, uint8_t preset, const char* label) {
            bool checked = (cfg.presetEnabled[card] & (1ULL << preset)) != 0;
            html += "<div class=\"preset-btn\" onclick=\"showCard(" + String(card) + "," + String(preset) + ",this)\" style=\"display:flex;align-items:center;gap:10px;background:#fffacd;border:4px solid #000;padding:16px 24px;font-size:1.2em;font-weight:bold;box-shadow:4px 4px 0 #000;transition:all 0.2s;cursor:pointer\">";
            html += "<input type=\"checkbox\" name=\"p" + String(card) + "_" + String(preset) + "\"" + String(checked ? " checked" : "") + " style=\"width:24px;height:24px;margin:0;cursor:pointer\" onclick=\"event.stopPropagation()\">";
            html += "<span>" + String(label) + "</span>";
            html += "</div>";
        };
        
        // Weather preset button with subtitle
        auto wxPreset = [&](uint8_t preset, const char* label, const char* sub) {
            bool checked = (cfg.presetEnabled[0] & (1ULL << preset)) != 0;
            html += "<div class=\"preset-btn\" onclick=\"showCard(0," + String(preset) + ",this)\" style=\"display:flex;flex-direction:column;align-items:flex-start;gap:2px;background:#fffacd;border:3px solid #000;padding:10px 14px;box-shadow:3px 3px 0 #000;transition:all 0.2s;cursor:pointer;min-width:90px\">";
            html += "<div style=\"display:flex;align-items:center;gap:6px;width:100%\">";
            html += "<input type=\"checkbox\" name=\"p0_" + String(preset) + "\"" + String(checked ? " checked" : "") + " style=\"width:18px;height:18px;margin:0;cursor:pointer\" onclick=\"event.stopPropagation()\">";
            html += "<span style=\"font-weight:bold;font-size:1em\">" + String(label) + "</span></div>";
            html += "<span style=\"font-size:0.7em;color:#666;margin-left:24px\">" + String(sub) + "</span>";
            html += "</div>";
        };
        
        switch(cardIdx) {
            case 0: { // Weather presets + location + display + simulator
                // All weather presets in one flat list
                wxPreset(0, "Classic", "Icon + temp");
                wxPreset(1, "Bar", "Horizontal bar");
                wxPreset(2, "Corner", "Tinted bg");
                wxPreset(3, "Anim", "Animated bg");
                wxPreset(4, "Minimal", "Big digits");
                wxPreset(5, "Day/Nite", "Sky cycle");
                wxPreset(6, "Term", "CLI style");
                wxPreset(7, "Big", "Large type");
                wxPreset(8, "Forecast", "3-hour bars");
                wxPreset(9, "Pixel", "Cute diorama");
                wxPreset(10, "LCD", "Retro green");
                wxPreset(11, "Mood", "Color waves");
                wxPreset(12, "Type", "Typewriter");
                wxPreset(13, "Waves", "Water scene");
                wxPreset(14, "Split", "Dual tone");
                wxPreset(15, "Count", "Countdown");
                wxPreset(16, "Thermo", "Mercury tube");
                wxPreset(17, "Icon", "Big symbol");
                wxPreset(18, "Rain", "Matrix drops");
                wxPreset(19, "Cyber", "Neon glow");
                wxPreset(20, "Particle", "Floating dots");
                wxPreset(21, "Wave", "Waveform");
                wxPreset(22, "TempBar", "Gradient fill");
                wxPreset(23, "Aurora", "Northern lights");
                wxPreset(24, "Radar", "Sweep anim");
                wxPreset(25, "Glitch", "Digital noise");
                wxPreset(26, "Horizon", "Sunrise/set");
                wxPreset(27, "Frost", "Ice crystals");
                wxPreset(28, "Radar", "RainViewer");
                wxPreset(29, "Grid", "Precip grid");
                wxPreset(30, "Heat", "Temp gradient");
                wxPreset(31, "Compass", "Wind dir");
                wxPreset(32, "Gauge", "Barometer");
                wxPreset(33, "Stars", "Night sky");
                wxPreset(34, "Seasons", "Color theme");
                wxPreset(35, "Half", "Dot shade");
                wxPreset(36, "Edge", "Outline");
                wxPreset(37, "PCB", "Circuit");
                wxPreset(38, "Stripe", "Stripes");
                wxPreset(39, "Scan", "Scanline");
                wxPreset(40, "Cine", "Ultra scene");
                
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">"; // Close preset buttons, open settings
                
                // Location settings
                float lat, lon;
                weather_get_location(&lat, &lon);
                html += "<details><summary style=\"cursor:pointer;font-weight:bold\">&#x1F4CD; ";
                html += TR("Location", "Atrašanās vieta");
                html += "</summary>";
                html += "<div style=\"padding:10px 0\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">";
                html += TR("Current:", "Pašreizējā:");
                html += " <b>" + String(lat, 4) + ", " + String(lon, 4) + "</b></p>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<input name=\"city\" placeholder=\"";
                html += TR("City name", "Pilsētas nosaukums");
                html += "\" style=\"flex:1;padding:6px\">";
                html += "</div>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"lat\" type=\"number\" step=\"0.01\" placeholder=\"";
                html += TR("Lat", "Plat.");
                html += "\" value=\"" + String(lat, 4) + "\" style=\"width:80px;padding:6px\">";
                html += "<input name=\"lon\" type=\"number\" step=\"0.01\" placeholder=\"";
                html += TR("Lon", "Gar.");
                html += "\" value=\"" + String(lon, 4) + "\" style=\"width:80px;padding:6px\">";
                html += "</div></div></details>";
                
                // Display settings
                html += "<details style=\"margin-top:8px\"><summary style=\"cursor:pointer;font-weight:bold\">&#x1F3A8; ";
                html += TR("Display", "Displejs");
                html += "</summary>";
                html += "<div style=\"padding:10px 0;display:flex;gap:10px;flex-wrap:wrap;align-items:center\">";
                html += "<label>";
                html += TR("Provider:", "Avots:");
                html += "</label><select name=\"wxProv\" style=\"padding:6px\">";
                html += "<option value=\"0\""; if (cfg.weatherProvider == 0) html += " selected"; html += ">";
                html += TR("Auto", "Auto");
                html += "</option>";
                html += "<option value=\"1\""; if (cfg.weatherProvider == 1) html += " selected"; html += ">Open-Meteo</option>";
                html += "<option value=\"2\""; if (cfg.weatherProvider == 2) html += " selected"; html += ">MET Norway</option>";
                html += "</select>";
                html += "<label>";
                html += TR("Palette:", "Palete:");
                html += "</label><select name=\"tempPalette\" style=\"padding:6px\">";
                html += "<option value=\"0\""; if (cfg.tempPalette == 0) html += " selected"; html += ">";
                html += TR("Default", "Noklusējums");
                html += "</option>";
                html += "<option value=\"1\""; if (cfg.tempPalette == 1) html += " selected"; html += ">";
                html += TR("Cool", "Vēss");
                html += "</option>";
                html += "<option value=\"2\""; if (cfg.tempPalette == 2) html += " selected"; html += ">";
                html += TR("Warm", "Silts");
                html += "</option>";
                html += "</select>";
                html += "<label>";
                html += TR("Forecast:", "Prognoze:");
                html += "</label><select name=\"forecastHours\" style=\"padding:6px\">";
                html += "<option value=\"12\""; if (cfg.forecastHours == 12) html += " selected"; html += ">12h</option>";
                html += "<option value=\"24\""; if (cfg.forecastHours == 24) html += " selected"; html += ">24h</option>";
                html += "<option value=\"48\""; if (cfg.forecastHours == 48) html += " selected"; html += ">48h</option>";
                html += "</select></div>";
                html += "<div style=\"padding:8px 0;display:flex;gap:20px;flex-wrap:wrap;align-items:center\">";
                html += "<span style=\"font-weight:bold\">&#x1F3B5; ";
                html += TR("Audio Reactive:", "Audio reaktīvs:");
                html += "</span>";
                html += "<label><input type=\"checkbox\" name=\"wxAudHue\""; if (cfg.wxAudioHue) html += " checked"; html += "> ";
                html += TR("Pulse Brightness", "Pulsējoša spilgtums");
                html += "</label>";
                html += "<label><input type=\"checkbox\" name=\"wxAudSpd\""; if (cfg.wxAudioSpeed) html += " checked"; html += "> ";
                html += TR("Animation Speed", "Animācijas ātrums");
                html += "</label>";
                html += "</div>";
                html += "<div style=\"padding:10px 0;border-top:1px solid #ddd;margin-top:8px\">";
                html += "<details><summary style=\"cursor:pointer;font-weight:bold\">";
                html += TR("Timeline Colors", "Laika joslas krāsas");
                html += "</summary>";
                html += "<div style=\"padding:10px 0;display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;align-items:center\">";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Sunny", "Saulaini");
                html += "<input type=\"color\" name=\"wxTlSun\" value=\"" + colorHex(cfg.wxTimelineSunny) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Cloudy", "Mākoņaini");
                html += "<input type=\"color\" name=\"wxTlCld\" value=\"" + colorHex(cfg.wxTimelineCloudy) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Rain", "Lietus");
                html += "<input type=\"color\" name=\"wxTlRai\" value=\"" + colorHex(cfg.wxTimelineRain) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Storm", "Vētra");
                html += "<input type=\"color\" name=\"wxTlSto\" value=\"" + colorHex(cfg.wxTimelineStorm) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Snow", "Sniegs");
                html += "<input type=\"color\" name=\"wxTlSno\" value=\"" + colorHex(cfg.wxTimelineSnow) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">";
                html += TR("Wind", "Vējš");
                html += "<input type=\"color\" name=\"wxTlWin\" value=\"" + colorHex(cfg.wxTimelineWind) + "\" style=\"width:54px;height:36px;padding:0;border:2px solid #000\"></label>";
                html += "</div></details></div>";
                // Map settings
                html += "<div style=\"padding:10px 0;border-top:1px solid #ddd;margin-top:8px;display:flex;gap:10px;flex-wrap:wrap;align-items:center\">";
                html += "<label>&#x1F5FA; ";
                html += TR("Map Zoom:", "Kartes tālummaiņa:");
                html += "</label><select name=\"mapZoom\" style=\"padding:6px\">";
                html += "<option value=\"2\""; if (cfg.mapZoom == 2) html += " selected"; html += ">";
                html += TR("Wide (region)", "Plašs (reģions)");
                html += "</option>";
                html += "<option value=\"3\""; if (cfg.mapZoom == 3) html += " selected"; html += ">";
                html += TR("Medium", "Vidējs");
                html += "</option>";
                html += "<option value=\"4\""; if (cfg.mapZoom == 4) html += " selected"; html += ">";
                html += TR("City", "Pilsēta");
                html += "</option>";
                html += "<option value=\"5\""; if (cfg.mapZoom == 5) html += " selected"; html += ">";
                html += TR("Close", "Tuvu");
                html += "</option>";
                html += "</select>";
                html += "<label>";
                html += TR("Style:", "Stils:");
                html += "</label><select name=\"mapStyle\" style=\"padding:6px\">";
                html += "<option value=\"0\""; if (cfg.mapStyle == 0) html += " selected"; html += ">";
                html += TR("Precipitation", "Nokrišņi");
                html += "</option>";
                html += "<option value=\"1\""; if (cfg.mapStyle == 1) html += " selected"; html += ">";
                html += TR("Cloud Focus", "Mākoņu fokuss");
                html += "</option>";
                html += "<option value=\"2\""; if (cfg.mapStyle == 2) html += " selected"; html += ">";
                html += TR("Radar Sweep", "Radara skenēšana");
                html += "</option>";
                html += "</select></div></details>";
                // Save button for Weather settings
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='margin-top:10px;padding:8px 16px'>&#x1F4BE; ";
                html += TR("Save Weather Settings", "Saglabāt laika iestatījumus");
                html += "</button>";
                
                // Simulator
                html += "<details style=\"margin-top:8px\"><summary style=\"cursor:pointer;font-weight:bold\">&#x1F9EA; ";
                html += TR("Simulator", "Simulators");
                html += "</summary>";
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
                html += "<label>";
                html += TR("Temp:", "Temp.:");
                html += "</label><input id=\"simTemp\" type=\"number\" value=\"22\" min=\"-50\" max=\"50\" style=\"width:60px;padding:6px\"><span>&deg;C</span>";
                html += "</div></div></details>";
                } break;
            case 1: // Clock presets + timezone
                presetBtn(1, 0, "Digital"); presetBtn(1, 1, "Binary"); presetBtn(1, 2, "Minimal");
                presetBtn(1, 3, "Bars"); presetBtn(1, 4, "Nixie"); presetBtn(1, 5, "Glitch");
                presetBtn(1, 6, "Pong"); presetBtn(1, 7, "Word"); presetBtn(1, 8, "Bounce");
                presetBtn(1, 9, "Matrix"); presetBtn(1, 10, "Radar"); presetBtn(1, 11, "Flip");
                presetBtn(1, 12, "Cyber"); presetBtn(1, 13, "Analog"); presetBtn(1, 14, "Countdown");
                presetBtn(1, 15, "DotMtx"); presetBtn(1, 16, "Gradient"); presetBtn(1, 17, "Segment");
                presetBtn(1, 18, "Orbit");
                presetBtn(1, 19, "Tally"); presetBtn(1, 20, "Cutout"); presetBtn(1, 21, "Scan");
                presetBtn(1, 22, "Duo"); presetBtn(1, 23, "Frame");
                presetBtn(1, 24, "Date"); presetBtn(1, 25, "FullDate"); presetBtn(1, 26, "Weekday");
                presetBtn(1, 27, "Nameday"); presetBtn(1, 28, "WeekNum"); presetBtn(1, 29, "Poland");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Timezone:", "Laika josla:");
                html += "</label>";
                html += "<select name=\"tz\" style=\"padding:6px\">";
                for (int8_t tz = -12; tz <= 14; ++tz) {
                    html += "<option value=\"" + String(tz) + "\"";
                    if (cfg.tzOffset == tz) html += " selected";
                    html += ">UTC" + String(tz >= 0 ? "+" : "") + String(tz) + "</option>";
                }
                html += "</select>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='padding:6px 12px'>&#x1F4BE; ";
                html += TR("Save", "Saglabāt");
                html += "</button>";
                html += "</div>"; // Close flex container
                break;
            case 2: // BTC / Crypto
                presetBtn(2, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; ";
                html += TR("Uses free CoinGecko API - no key needed! Leave crypto empty for BTC.", "Izmanto bezmaksas CoinGecko API - nav nepieciešama atslēga! Atstājiet tukšu priekš BTC.");
                html += "</p>";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<input name=\"crypto\" type=\"text\" maxlength=\"10\" placeholder=\"BTC\" value=\"" + String(cfg.cryptoSymbol) + "\" style=\"width:80px;text-transform:uppercase;padding:6px\">";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Update:", "Atjaunināt:");
                html += "</label>";
                html += "<select name=\"btcMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {1, 2, 5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 7; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.btcUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + " min</option>";
                }}
                html += "</select>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='padding:6px 12px'>&#x1F4BE; ";
                html += TR("Save", "Saglabāt");
                html += "</button>";
                html += "</div>"; // Close flex container
                break;
            case 3: // Stocks
                presetBtn(3, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; ";
                html += TR("Uses free Yahoo Finance - no key needed! Enter any stock ticker symbol.", "Izmanto bezmaksas Yahoo Finance - nav nepieciešama atslēga! Ievadiet jebkuru akciju simbolu.");
                html += "</p>";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<input name=\"stock\" type=\"text\" maxlength=\"10\" placeholder=\"AAPL\" value=\"" + String(cfg.stockSymbol) + "\" style=\"width:80px;text-transform:uppercase;padding:6px\">";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Update:", "Atjaunināt:");
                html += "</label>";
                html += "<select name=\"stockMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {1, 2, 5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 7; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.stockUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + " min</option>";
                }}
                html += "</select>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='padding:6px 12px'>&#x1F4BE; ";
                html += TR("Save", "Saglabāt");
                html += "</button>";
                html += "</div>"; // Close flex container
                break;
            case 5: // Audio VU presets
                presetBtn(5, 0, "Spectrum"); presetBtn(5, 1, "Wave"); presetBtn(5, 2, "Fire");
                presetBtn(5, 3, "Pulse"); presetBtn(5, 4, "Waterfall"); presetBtn(5, 5, "Strobe");
                sendChunk();
                presetBtn(5, 6, "Plasma"); presetBtn(5, 7, "Balls"); presetBtn(5, 8, "Matrix");
                sendChunk(); // Flush buffer
                presetBtn(5, 9, "Rainbow"); presetBtn(5, 10, "Mirror"); presetBtn(5, 11, "Laser");
                presetBtn(5, 12, "Dancer"); presetBtn(5, 13, "Heart"); presetBtn(5, 14, "Traffic");
                sendChunk();
                presetBtn(5, 15, "Pacman"); presetBtn(5, 16, "Vortex"); presetBtn(5, 17, "EQ");
                sendChunk(); // Flush buffer
                presetBtn(5, 18, "Disco"); presetBtn(5, 19, "Firework"); presetBtn(5, 20, "Rain");
                sendChunk();
                presetBtn(5, 21, "Nyan"); presetBtn(5, 22, "Ocean"); presetBtn(5, 23, "Tetris");
                sendChunk();
                presetBtn(5, 24, "Stars"); presetBtn(5, 25, "Lava"); presetBtn(5, 26, "Geo");
                presetBtn(5, 27, "Sparkle"); presetBtn(5, 28, "Aurora");
                sendChunk();
                presetBtn(5, 29, "Lightning"); presetBtn(5, 30, "Ripple"); presetBtn(5, 31, "DNA");
                presetBtn(5, 32, "Kaleid"); presetBtn(5, 33, "Snake");
                presetBtn(5, 34, "Liss2"); presetBtn(5, 35, "Code"); presetBtn(5, 36, "Orbit");
                presetBtn(5, 37, "Check"); presetBtn(5, 38, "Shard");
                html += "</div>"; // Close preset buttons wrapper
                sendChunk(); // Flush buffer after massive list of presets
                html += "<div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<div style=\"display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Palette:", "Palete:");
                html += "</label>";
                html += "<select name=\"palette\" style=\"padding:6px\">";
                for (uint8_t i = 0; i < PALETTE_COUNT; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.vuPalette == i) html += " selected";
                    html += ">" + String(settings_palette_name(i)) + "</option>";
                }
                html += "</select>";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Speed:", "Ātrums:");
                html += "</label>";
                html += "<select name=\"speed\" style=\"padding:6px\">";
                for (uint8_t i = 1; i <= 10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.animSpeed == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select></div>";
                html += "<div style=\"font-size:0.8em;color:#666;margin:-4px 0 10px 0\">";
                html += TR("Palette = the colors. Speed = how fast the animation moves.", "Palete = krāsas. Ātrums = cik ātri kustas animācija.");
                html += "</div>";
                html += "<div style=\"display:flex;align-items:center;gap:8px;flex-wrap:wrap\">";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Gain:", "Pastiprinājums:");
                html += "</label>";
                html += "<select name=\"micGain\" style=\"padding:6px\">";
                for (uint8_t i = 1; i <= 10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.micGain == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select>";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Boost:", "Papildu:");
                html += "</label>";
                html += "<select name=\"micBoost\" style=\"padding:6px\">";
                for (uint8_t i = 0; i <= 10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.micBoost == i) html += " selected";
                    html += ">" + String(i) + (i == 0 ? " (off)" : i == 10 ? " (max)" : "") + "</option>";
                }
                html += "</select>";
                html += "<label style=\"display:flex;align-items:center;gap:4px;font-weight:bold;cursor:pointer\"><input type=\"checkbox\" name=\"agcOn\" value=\"1\"";
                if (cfg.agcEnabled) html += " checked";
                html += " style=\"width:16px;height:16px\">AGC</label>";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Gate:", "Vārti:");
                html += "</label>";
                html += "<select name=\"noiseGate\" style=\"padding:6px\">";
                { const char* gates[] = {"Off", "Low", "Med", "High"};
                  const uint8_t gateVals[] = {0, 50, 100, 150};
                  for (uint8_t i = 0; i < 4; ++i) {
                    html += "<option value=\"" + String(gateVals[i]) + "\"";
                    if (cfg.vuNoiseGate >= gateVals[i] && (i == 3 || cfg.vuNoiseGate < gateVals[i+1])) html += " selected";
                    html += ">" + String(gates[i]) + "</option>";
                }}
                html += "</select>";
                html += "<label style=\"font-weight:bold\">";
                html += TR("Silence:", "Klusēšana:");
                html += "</label>";
                html += "<select name=\"silenceMs\" style=\"padding:6px\">";
                { const char* silNames[] = {"Off", "250ms", "500ms", "1s", "2s"};
                  const uint16_t silVals[] = {0, 250, 500, 1000, 2000};
                  for (uint8_t i = 0; i < 5; ++i) {
                    html += "<option value=\"" + String(silVals[i]) + "\"";
                    if (cfg.vuSilenceMs == silVals[i]) html += " selected";
                    html += ">" + String(silNames[i]) + "</option>";
                }}
                html += "</select>";
                html += "<label style=\"display:flex;align-items:center;gap:6px;font-weight:bold;cursor:pointer\"><input type=\"checkbox\" name=\"micInvert\" value=\"1\"";
                if (cfg.vuInvert) html += " checked";
                html += " style=\"width:18px;height:18px\">";
                html += TR("Invert", "Apgriezt");
                html += "</label>";
                html += "</div>";
                html += "<div style=\"font-size:0.8em;color:#666;margin:6px 0 0 0\">";
                html += TR("Gain/Boost make the mic signal bigger (if nothing reacts, increase these). AGC auto-adjusts so quiet and loud music both look OK. Gate ignores tiny background noise. Silence turns the mic off after it stays quiet for a while. Invert flips the VU direction.", "Pastiprinājums/Papildu padara mikrofona signālu lielāku (ja nekas nereaģē, palieliniet šos). AGC automātiski pielāgojas, lai klusai un skaļai mūzikai izskatītos labi. Vārti ignorē sīkus fona trokšņus. Klusēšana izslēdz mikrofonu pēc kāda laika. Apgriezt maina VU virzienu.");
                html += "</div>";
                html += "<details style=\"margin-top:10px\"><summary style=\"cursor:pointer;font-weight:bold\">";
                html += TR("Advanced Audio", "Papildu audio iestatījumi");
                html += "</summary>";
                html += "<div style=\"margin-top:10px;display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;align-items:center\">";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">AGC Min<input name=\"agcMin\" type=\"number\" min=\"20\" max=\"2000\" value=\"" + String(cfg.agcMin) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">AGC Min = the minimum loudness the auto-gain will assume. Lower = more sensitive. Too low = more jitter/noise.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">AGC Max<input name=\"agcMax\" type=\"number\" min=\"200\" max=\"65000\" value=\"" + String(cfg.agcMax) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">AGC Max = the maximum loudness the auto-gain will expect. Lower = reacts more at normal volume. Too low = clips/always maxed.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">AGC Attack<input name=\"agcAtk\" type=\"number\" min=\"1\" max=\"64\" value=\"" + String(cfg.agcAttack) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">AGC Attack = how fast the auto-gain reacts when things suddenly get loud. Lower number = faster reaction.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">AGC Decay<input name=\"agcDcy\" type=\"number\" min=\"2\" max=\"128\" value=\"" + String(cfg.agcDecay) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">AGC Decay = how fast the auto-gain becomes sensitive again after it was loud. Lower number = becomes sensitive again sooner.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">Env Attack<input name=\"envAtk\" type=\"number\" min=\"1\" max=\"32\" value=\"" + String(cfg.envAttack) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">Env Attack = how quickly the visuals jump up when sound gets louder. Lower = snappier.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">Env Decay<input name=\"envDcy\" type=\"number\" min=\"2\" max=\"128\" value=\"" + String(cfg.envDecay) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">Env Decay = how quickly the visuals fall back down after a hit. Lower = more bouncy, higher = smoother.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">Beat Thresh<input name=\"beatThr\" type=\"number\" min=\"10\" max=\"250\" value=\"" + String(cfg.beatThreshold) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">Beat Thresh = how loud it must be to count as a beat. Lower = more beats. Higher = only big hits.</div>";
                html += "<label style=\"display:flex;justify-content:space-between;align-items:center;gap:10px\">Beat Hold<input name=\"beatHld\" type=\"number\" min=\"1\" max=\"60\" value=\"" + String(cfg.beatHold) + "\" style=\"width:90px;padding:6px\"></label>";
                html += "<div style=\"grid-column:1 / -1;font-size:0.8em;color:#666;margin-top:-6px\">Beat Hold = how long (in frames) the beat effect stays on. Higher = longer flash.</div>";
                html += "</div></details>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='margin-top:10px;padding:8px 16px'>&#x1F4BE; ";
                html += TR("Save Audio Settings", "Saglabāt audio iestatījumus");
                html += "</button>";
                break;
            case 8: // Game presets + controls
                presetBtn(8, 0, "Flappy"); presetBtn(8, 1, "Snake"); presetBtn(8, 2, "Breakout"); presetBtn(8, 3, "Pong");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x1F3AE; ";
                html += TR("Controls:", "Vadība:");
                html += "</b><br>";
                html += "&#x2022; <b>BTN1</b> &rarr; ";
                html += TR("Next / Right", "Nākamais / Pa labi");
                html += "<br>";
                html += "&#x2022; <b>BTN2</b> &rarr; ";
                html += TR("Prev / Left", "Iepriekšējais / Pa kreisi");
                html += "<br>";
                html += "&#x2022; <b>Touch</b> &rarr; ";
                html += TR("Jump / Action", "Lēciens / Darbība");
                html += "<br>";
                html += "&#x2022; <b>";
                html += TR("Hold both 1s", "Turiet abas 1s");
                html += "</b> &rarr; ";
                html += TR("Exit game", "Iziet no spēles");
                html += "</p>";
                break;
            case 9: // MQTT
                presetBtn(9, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>";
                html += TR("Setup:", "Iestatīšana:");
                html += "</b> ";
                html += TR("Enter your MQTT broker address (e.g. Home Assistant IP). Device auto-registers via MQTT discovery.", "Ievadiet MQTT brokera adresi (piem., Home Assistant IP). Ierīce automātiski reģistrējas caur MQTT discovery.");
                html += "</p>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px\">";
                html += "<input name=\"mqttServer\" placeholder=\"192.168.1.x\" value=\"" + String(cfg.mqttServer) + "\" style=\"flex:1;min-width:120px;padding:6px\">";
                html += "<input name=\"mqttPort\" type=\"number\" value=\"" + String(cfg.mqttPort) + "\" style=\"width:70px;padding:6px\">";
                html += "</div>";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"mqttUser\" placeholder=\"";
                html += TR("user (optional)", "lietotājs (neobligāti)");
                html += "\" value=\"" + String(cfg.mqttUser) + "\" style=\"flex:1;padding:6px\">";
                html += "<input name=\"mqttPass\" type=\"password\" placeholder=\"";
                html += TR("pass", "parole");
                html += "\" value=\"" + String(cfg.mqttPass) + "\" style=\"flex:1;padding:6px\">";
                html += "</div>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='margin-top:10px;padding:8px 16px'>&#x1F4BE; ";
                html += TR("Save MQTT Settings", "Saglabāt MQTT iestatījumus");
                html += "</button>";
                break;
            case 10: // RSS
                presetBtn(10, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>";
                html += TR("Find RSS feeds:", "Atrast RSS plūsmas:");
                html += "</b> ";
                html += TR("Most news sites have RSS. Try adding <code>/rss</code> or <code>/feed</code> to any site URL, or search \"[site name] RSS feed\".", "Lielākajai daļai ziņu vietņu ir RSS. Mēģiniet pievienot <code>/rss</code> vai <code>/feed</code> jebkurai vietnes adresei.");
                html += "</p>";
                html += "<input name=\"rssUrl\" placeholder=\"https://feeds.bbci.co.uk/news/rss.xml\" value=\"" + String(cfg.rssUrl) + "\" style=\"width:100%;padding:6px;margin-bottom:8px\">";
                html += "<div style=\"display:flex;align-items:center;gap:10px;flex-wrap:wrap\">";
                html += "<label>";
                html += TR("Update:", "Atjaunināt:");
                html += "</label><select name=\"rssMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 5; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.rssUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + "m</option>";
                }}
                html += "</select>";
                html += "<label>";
                html += TR("Speed:", "Ātrums:");
                html += "</label><select name=\"rssSpd\" style=\"padding:6px\">";
                for(int i=1; i<=10; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.rssSpeed == i) html += " selected";
                    html += ">" + String(i) + "</option>";
                }
                html += "</select>";
                
                html += "<label>";
                html += TR("Palette:", "Palete:");
                html += "</label><select name=\"rssPal\" style=\"padding:6px\">";
                for (uint8_t i = 0; i < PALETTE_COUNT; ++i) {
                    html += "<option value=\"" + String(i) + "\"";
                    if (cfg.rssPalette == i) html += " selected";
                    html += ">" + String(settings_palette_name(i)) + "</option>";
                }
                html += "</select>";
                
                html += "<label>";
                html += TR("Items:", "Vienības:");
                html += "</label><input type=\"number\" name=\"rssCnt\" min=\"1\" max=\"10\" value=\"" + String(cfg.rssItemCount) + "\" style=\"width:60px;padding:6px\">";
                
                html += "<label>";
                html += TR("Show:", "Rādīt:");
                html += "</label><select name=\"rssFmt\" style=\"padding:6px\">";
                html += "<option value=\"0\"" + String(cfg.rssFormat == 0 ? " selected" : "") + ">";
                html += TR("Titles Only", "Tikai virsraksti");
                html += "</option>";
                html += "<option value=\"1\"" + String(cfg.rssFormat == 1 ? " selected" : "") + ">";
                html += TR("Title + Text", "Virsraksts + teksts");
                html += "</option>";
                html += "</select>";

                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='padding:6px 12px'>&#x1F4BE; ";
                html += TR("Save", "Saglabāt");
                html += "</button>";
                html += "</div>"; // Close flex container
                break;
            case 11: // YouTube
                presetBtn(11, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.8em;color:#666;margin-bottom:8px\">&#x1F4A1; <b>";
                html += TR("Setup:", "Iestatīšana:");
                html += "</b> ";
                html += TR("1) Go to <a href='https://console.cloud.google.com' target='_blank'>console.cloud.google.com</a> 2) Create project 3) Enable 'YouTube Data API v3' 4) Create API Key under Credentials", "1) Dodieties uz <a href='https://console.cloud.google.com' target='_blank'>console.cloud.google.com</a> 2) Izveidojiet projektu 3) Iespējojiet 'YouTube Data API v3' 4) Izveidojiet API atslēgu");
                html += "</p>";
                html += "<input name=\"ytChan\" placeholder=\"";
                html += TR("Channel ID (UCxxxxx from URL)", "Kanāla ID (UCxxxxx no URL)");
                html += "\" value=\"" + String(cfg.ytChannelId) + "\" style=\"width:100%;padding:6px;margin-bottom:8px\">";
                html += "<div style=\"display:flex;gap:8px;flex-wrap:wrap\">";
                html += "<input name=\"ytKey\" type=\"password\" placeholder=\"";
                html += TR("API Key (AIza...)", "API atslēga (AIza...)");
                html += "\" value=\"" + String(cfg.ytApiKey) + "\" style=\"flex:1;padding:6px\">";
                html += "<select name=\"socMins\" style=\"padding:6px\">";
                { const uint8_t intervals[] = {5, 10, 15, 30, 60};
                for (uint8_t i = 0; i < 5; ++i) {
                    html += "<option value=\"" + String(intervals[i]) + "\"";
                    if (cfg.socialUpdateMins == intervals[i]) html += " selected";
                    html += ">" + String(intervals[i]) + "m</option>";
                }}
                html += "</select>";
                html += "<button type='submit' class='btn btn-accent' onclick='saveOrder()' style='padding:6px 12px'>&#x1F4BE; ";
                html += TR("Save", "Saglabāt");
                html += "</button>";
                html += "</div>"; // Close flex container
                break;
            case 12: // Countdown timer
                presetBtn(12, 0, "1 min"); presetBtn(12, 1, "5 min"); presetBtn(12, 2, "15 min"); presetBtn(12, 3, "30 min");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x23F1; ";
                html += TR("Controls:", "Vadība:");
                html += "</b> ";
                html += TR("Tap to start/pause. Long touch (1.2s) to reset.", "Pieskarieties, lai sāktu/apturētu. Ilgs pieskāriens (1.2s), lai atiestatītu.");
                html += "</p>";
                break;
            case 13: // Pomodoro timer
                presetBtn(13, 0, "25/5"); presetBtn(13, 1, "50/10"); presetBtn(13, 2, "15/3");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x1F345; ";
                html += TR("Controls:", "Vadība:");
                html += "</b> ";
                html += TR("Tap to start/pause. Long touch to reset. Red=Work, Blue=Break.", "Pieskarieties, lai sāktu/apturētu. Ilgs pieskāriens, lai atiestatītu. Sarkans=Darbs, Zils=Pārtraukums.");
                html += "</p>";
                break;
            case 14: // Sun position
                presetBtn(14, 0, "Yellow"); presetBtn(14, 1, "Orange");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x2600; ";
                html += TR("Info:", "Info:");
                html += "</b> ";
                html += TR("Shows sun arc position based on time of day with digital clock.", "Rāda saules loka pozīciju atkarībā no dienas laika ar digitālo pulksteni.");
                html += "</p>";
                break;
            case 15: // Stopwatch
                presetBtn(15, 0, "SHOW");
                html += "</div><div style=\"margin-top:12px;padding-top:12px;border-top:2px dashed #ccc\">";
                html += "<p style=\"font-size:0.85em;line-height:1.6;color:#333\">";
                html += "<b>&#x23F1; ";
                html += TR("Controls:", "Vadība:");
                html += "</b> ";
                html += TR("Tap to start/pause. Long touch (1.2s) to reset.", "Pieskarieties, lai sāktu/apturētu. Ilgs pieskāriens (1.2s), lai atiestatītu.");
                html += "</p>";
                break;
            default: // Single preset cards (Network, etc)
                presetBtn(cardIdx, 0, "SHOW");
                break;
        }
        html += "</div>"; // Close preset buttons
        html += "</div>"; // Close card-body
        html += "</div>"; // Close gallery-card
        sendChunk(); // Flush buffer after each card to prevent overflow
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

function clearPresets(card, btn) {
    const cardEl = btn.closest('.gallery-card');
    if (!cardEl) return;
    cardEl.querySelectorAll('input[type=checkbox][name^=\"p' + card + '_\"]').forEach(cb => cb.checked = false);
    btn.textContent = '✓ ' + (btn.textContent.includes('Noņemt') ? 'Notīrīts' : 'Cleared');
    setTimeout(() => { btn.textContent = '✖ ' + (btn.textContent.includes('Notīrīts') ? 'Noņemt atzīmes visiem stiliem' : 'Uncheck all presets'); }, 1200);
}

// Collapse/expand cards
function toggleCollapse(btn) {
    const card = btn.closest('.gallery-card');
    const body = card.querySelector('.card-body');
    const showBtn = card.querySelector('.show-btn');
    if (body.style.display === 'none') {
        body.style.display = '';
        btn.innerHTML = '&#x25BC;';
        showBtn.style.display = 'none';
    } else {
        body.style.display = 'none';
        btn.innerHTML = '&#x25B6;';
        showBtn.style.display = '';
    }
}

</script>)";
    
    html += "</div>"; // End Card Gallery
    sendChunk(); // Flush buffer after card gallery
    
    html += "<div class=\"grid\">"; // Reopen grid
    sendChunk();
    
    // System Configuration Card (Brightness only now)
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x2699;</span><span class=\"card-title\">";
    html += TR("System Configuration", "Sist\u0113mas konfigur\u0101cija");
    html += "</span></div>";
    
    // Brightness Section
    html += "<details><summary>";
    html += TR("Brightness & Power", "Spilgtums un jauda");
    html += "</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>";
    html += TR("Mode", "Re\u017e\u012bms");
    html += "</label>";
    html += "<select name=\"brightMode\">";
    html += "<option value=\"0\"";
    if (cfg.brightMode == 0) html += " selected";
    html += ">";
    html += TR("Auto (light sensor)", "Autom\u0101tiski (gaismas sensors)");
    html += "</option>";
    html += "<option value=\"1\"";
    if (cfg.brightMode == 1) html += " selected";
    html += ">";
    html += TR("Manual (fixed)", "Manu\u0101ls (fiks\u0113ts)");
    html += "</option>";
    html += "</select></div>";
    uint8_t maxBright = cfg.highPowerMode ? 255 : 127;
    html += "<div class=\"form-group\"><label>";
    html += TR("Manual Brightness", "Manu\u0101ls spilgtums");
    html += " (" + String(cfg.brightManual) + ")</label>";
    html += "<input type=\"range\" name=\"brightManual\" min=\"5\" max=\"" + String(maxBright) + "\" value=\"" + String(cfg.brightManual) + "\"></div>";
    html += "<div class=\"form-group\"><label>";
    html += TR("Auto Min (dark room)", "Autom\u0101tiskais min (tum\u0161a istaba)");
    html += ": " + String(cfg.brightMin) + "</label>";
    html += "<input type=\"range\" name=\"brightMin\" min=\"5\" max=\"40\" value=\"" + String(cfg.brightMin) + "\"></div>";
    html += "<div class=\"form-group\"><label>";
    html += TR("Auto Max (bright room)", "Autom\u0101tiskais maks (gai\u0161a istaba)");
    html += ": " + String(cfg.brightMax) + "</label>";
    html += "<input type=\"range\" name=\"brightMax\" min=\"20\" max=\"" + String(maxBright) + "\" value=\"" + String(cfg.brightMax) + "\"></div>";
    html += "<div class=\"form-group\"><label><input type=\"checkbox\" name=\"brightBlank\" value=\"1\"";
    if (cfg.brightBlanking) html += " checked";
    html += "> ";
    html += TR("Use blanking for cleaner readings", "Izmantot tuk\u0161umu prec\u012bz\u0101kiem nolas\u012bjumiem");
    html += "</label></div>";
    html += "<div class=\"form-group\" style=\"margin-top:12px;padding:10px;background:#fff3cd;border:1px solid #ffc107;border-radius:4px\">";
    html += "<label style=\"color:#856404\"><input type=\"checkbox\" name=\"highPower\" value=\"1\"";
    if (cfg.highPowerMode) html += " checked";
    html += "> &#x26A0; ";
    html += TR("High Power Mode (128-255)", "Augstas jaudas re\u017e\u012bms (128-255)");
    html += "</label>";
    html += "<p style=\"font-size:0.75em;color:#856404;margin:6px 0 0\"><b>";
    html += TR("WARNING:", "BR\u012aDIN\u0100JUMS:");
    html += "</b> ";
    html += TR("High brightness causes heat! Use only with bare PCB, no plastic enclosure. May trip USB power protection.", "Augsts spilgtums rada siltumu! Izmantojiet tikai ar kailu PCB, bez plastmasas korpusa. Var izrais\u012bt USB baro\u0161anas aizsardz\u012bbu.");
    html += "</p></div>";
    html += "<div class=\"form-group\"><label>";
    html += TR("Blanking Interval", "Tuk\u0161uma interv\u0101ls");
    html += ": " + String(cfg.brightBlankSecs) + " ";
    html += TR("sec", "sek");
    html += "</label>";
    html += "<input type=\"range\" name=\"blankSec\" min=\"10\" max=\"120\" step=\"10\" value=\"" + String(cfg.brightBlankSecs) + "\"></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; ";
    html += TR("Save Brightness", "Saglab\u0101t spilgtumu");
    html += "</button>";
    html += "</form></details>";
    
    html += "</div>"; // End card

    // Tools Card
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F6E0;</span><span class=\"card-title\">";
    html += TR("Tools & Customization", "R\u012bki un piel\u0101go\u0161ana");
    html += "</span></div>";
    html += "<p style=\"color:var(--muted);margin-bottom:16px;font-size:0.9em\">";
    html += TR("Customize the display and edit pixel sprites", "Piel\u0101gojiet displeju un redi\u0123\u0113jiet pikse\u013cu spraitus");
    html += "</p>";
    html += "<a href=\"/editor\" class=\"btn btn-secondary btn-full\" style=\"margin-bottom:10px\">&#x1F3A8; ";
    html += TR("Open Sprite Editor", "Atv\u0113rt spraitu redaktoru");
    html += "</a>";
    html += "<div class=\"quick-actions\">";
    html += "<a href=\"/\" class=\"btn btn-secondary\">&#x1F504; ";
    html += TR("Refresh", "Atsvaidzin\u0101t");
    html += "</a>";
    html += "</div></div>";

    String fwLine = wt_fw_git_sha_short();
    if (wt_fw_git_dirty()) fwLine += "+dirty";
    String fwDate = wt_fw_build_date();

    html += R"(</div></div>
<div class="footer">
<p><strong>WeatherThing</strong></p>
<p>)";
    html += TR("Firmware", "Programmat\u016bra");
    html += ": ";
    html += fwLine;
    html += " \u2022 ";
    html += fwDate;
    html += R"(</p>
<p id="diagInfo">)";
    html += TR("Diagnostics: loading\u2026", "Diagnostika: iel\u0101d\u0113...");
    html += R"(</p>
<p>)";
    html += TR("by", "no");
    html += R"( <a href="https://github.com/makeriga">Makeriga</a> • <a href="https://github.com/makeriga/weatherthing-firmware">GitHub</a> • <a href="/editor">)";
    html += TR("Sprite Editor", "Spraitu redaktors");
    html += R"(</a> • <a href="https://makeriga.github.io/weatherthing-firmware/?lang=)";
    html += (cfg.uiLang == 1) ? "lv" : "en";
    html += R"(" target="_blank">)";
    html += TR("Firmware Updater", "Programmat\u016bras atjaunin\u0101t\u0101js");
    html += R"(</a></p>
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

async function checkUpdate(){
  try{
    const r=await fetch('/api/check_update');
    if(!r.ok) return;
    const d=await r.json();
    if(d.update_available){
      const el=document.getElementById('updateBubble');
      if(el) el.classList.add('show');
    }
  }catch(e){}
}

async function loadDiag(){
  try{
    const r=await fetch('/api/diag');
    if(!r.ok) return;
    const d=await r.json();
    const el=document.getElementById('diagInfo');
    if(!el) return;
    el.textContent='Diagnostics: heap '+d.free_heap+'/'+d.min_free_heap+' free/min • maxAlloc '+d.max_alloc_heap+' • largest '+d.largest_free_block+' • frag '+d.frag_pct+'% • httpq '+d.httpq_waiting+'/'+d.httpq_free+'/'+d.httpq_cap+' enq '+d.httpq_enq_ok+'/'+d.httpq_enq_fail+' • wifi '+(d.ap_mode?'AP':'STA')+' st '+d.wifi_status+' rssi '+d.wifi_rssi+' • up '+Math.floor(d.uptime_ms/1000)+'s';
  }catch(e){
  }
}

function ajaxifyForm(form){
  form.addEventListener('submit', async function(ev){
    ev.preventDefault();
    try{
      if(typeof saveOrder==='function') saveOrder();
      const fd=new FormData(form);
      fd.append('ajax','1');
      const params=new URLSearchParams();
      for(const kv of fd.entries()) params.append(kv[0], kv[1]);
      const btn=form.querySelector('button[type=submit]');
      if(btn){btn.disabled=true;btn.dataset._bg=btn.style.background;btn.style.background='#ffa500';}
      const resp=await fetch(form.getAttribute('action')||'/',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
      if(!resp.ok) throw new Error('HTTP '+resp.status);
      if(btn){btn.style.background='#4ade80';setTimeout(function(){btn.style.background=btn.dataset._bg||'';btn.disabled=false;},600);}
    }catch(e){
      const btn=form.querySelector('button[type=submit]');
      if(btn){btn.style.background='#ff6b6b';btn.disabled=false;}
    }
  });
}

window.addEventListener('DOMContentLoaded', function(){
  loadDiag();
  setInterval(loadDiag, 2000);
  checkUpdate();
  document.querySelectorAll('form[method="POST"]').forEach(function(f){
    const act=f.getAttribute('action');
    if(act==='/settings' || act==='/cards_config' || act==='/simulate' || act==='/card') ajaxifyForm(f);
  });
});
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
    server.on("/api/version", HTTP_GET, handleApiVersion);
    server.on("/api/diag", HTTP_GET, handleApiDiag);
    server.on("/api/check_update", HTTP_GET, handleApiCheckUpdate);
    server.on("/editor", handleEditor);
    server.on("/api/sprites", HTTP_GET, handleApiSprites);
    server.on("/api/sprite", HTTP_GET, handleApiSpriteGet);
    server.on("/api/sprite", HTTP_POST, handleApiSpriteSave);
    server.on("/api/sprite/reset", HTTP_POST, handleApiSpriteReset);
    server.on("/card", handleCardSwitch);
    server.on("/api/card", HTTP_GET, handleApiCardSwitch);
    server.on("/api/simulate", HTTP_GET, handleApiSimulate);
    server.on("/api/touch_shortcut", HTTP_GET, handleApiTouchShortcut);
    server.on("/cards_config", HTTP_POST, handleCardsConfigPost);

    server.on("/api/overlay", HTTP_GET, handleApiOverlayGet);
    server.on("/api/overlay/clear", HTTP_POST, handleApiOverlayClear);
    server.on("/api/overlay/matrix", HTTP_POST, handleApiOverlayMatrix);
    server.on("/api/overlay/timeline", HTTP_POST, handleApiOverlayTimeline);
    server.on("/api/overlay/text", HTTP_POST, handleApiOverlayText);
    server.on("/api/lang", HTTP_GET, handleApiLang);
    server.begin();
}

static void handleApiOverlayGet()
{
    uint32_t now = millis();
    String json;
    json.reserve(256);
    json += "{";
    json += "\"ok\":true";
    json += ",\"matrix_active\":";
    json += custom_overlay_matrix_active(now) ? "true" : "false";
    json += ",\"matrix_remaining_ms\":";
    json += String(custom_overlay_matrix_remaining_ms(now));
    json += ",\"timeline_active\":";
    json += custom_overlay_timeline_active(now) ? "true" : "false";
    json += ",\"timeline_remaining_ms\":";
    json += String(custom_overlay_timeline_remaining_ms(now));
    json += ",\"text_active\":";
    json += custom_overlay_text_active(now) ? "true" : "false";
    json += ",\"text_remaining_ms\":";
    json += String(custom_overlay_text_remaining_ms(now));
    json += "}";
    server.send(200, "application/json", json);
}

static void handleApiOverlayClear()
{
    String body = server.arg("plain");
    String target = "all";

    if (body.length() > 0)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (!err)
        {
            target = (const char*)(doc["target"] | "all");
        }
    }

    if (server.hasArg("target"))
    {
        target = server.arg("target");
    }

    if (target == "matrix") custom_overlay_clear_matrix();
    else if (target == "timeline") custom_overlay_clear_timeline();
    else if (target == "text") custom_overlay_clear_text();
    else custom_overlay_clear_all();

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiOverlayMatrix()
{
    uint32_t now = millis();
    String body = server.arg("plain");
    uint32_t timeoutMs = 0;
    bool clear = false;
    bool clearUnder = false;

    if (server.hasArg("timeout_ms")) timeoutMs = (uint32_t)server.arg("timeout_ms").toInt();
    if (server.hasArg("clear")) clear = server.arg("clear").toInt() != 0;
    if (server.hasArg("clear_under")) clearUnder = server.arg("clear_under").toInt() != 0;

    if (body.length() > 0)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
            apiSendError(400, "invalid json");
            return;
        }

        timeoutMs = (uint32_t)(doc["timeout_ms"] | (uint32_t)timeoutMs);
        clear = (bool)(doc["clear"] | clear);
        clearUnder = (bool)(doc["clear_under"] | clearUnder);

        if (clear) custom_overlay_clear_matrix();
        if (clearUnder) custom_overlay_set_matrix_clear_under(true);

        JsonArrayConst pixels = doc["pixels"].as<JsonArrayConst>();
        if (!pixels.isNull())
        {
            for (JsonObjectConst p : pixels)
            {
                int x = p["x"] | -1;
                int yTop = p["y"] | -1;
                uint32_t col;
                if (x < 0 || yTop < 0) continue;
                if (!parseJsonColor(p["color"], &col)) continue;

                uint8_t y = (uint8_t)(WT_MATRIX_HEIGHT - 1 - (uint8_t)yTop);
                custom_overlay_set_matrix_pixel((uint8_t)x, y, col);
            }
        }
        else
        {
            int x = doc["x"] | (server.hasArg("x") ? server.arg("x").toInt() : -1);
            int yTop = doc["y"] | (server.hasArg("y") ? server.arg("y").toInt() : -1);
            uint32_t col;
            if (x >= 0 && yTop >= 0 && parseJsonColor(doc["color"], &col))
            {
                uint8_t y = (uint8_t)(WT_MATRIX_HEIGHT - 1 - (uint8_t)yTop);
                custom_overlay_set_matrix_pixel((uint8_t)x, y, col);
            }
        }
    }
    else
    {
        if (clear) custom_overlay_clear_matrix();

        if (clearUnder) custom_overlay_set_matrix_clear_under(true);

        if (server.hasArg("x") && server.hasArg("y") && server.hasArg("color"))
        {
            int x = server.arg("x").toInt();
            int yTop = server.arg("y").toInt();
            uint32_t col;
            if (!parseHexColor(server.arg("color"), &col))
            {
                apiSendError(400, "invalid color");
                return;
            }
            uint8_t y = (uint8_t)(WT_MATRIX_HEIGHT - 1 - (uint8_t)yTop);
            custom_overlay_set_matrix_pixel((uint8_t)x, y, col);
        }
        else
        {
            apiSendError(400, "missing pixels" );
            return;
        }
    }

    custom_overlay_set_matrix_timeout_ms(now, timeoutMs);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiOverlayTimeline()
{
    uint32_t now = millis();
    String body = server.arg("plain");
    uint32_t timeoutMs = 0;
    bool clear = false;
    bool clearUnder = false;

    if (server.hasArg("timeout_ms")) timeoutMs = (uint32_t)server.arg("timeout_ms").toInt();
    if (server.hasArg("clear")) clear = server.arg("clear").toInt() != 0;
    if (server.hasArg("clear_under")) clearUnder = server.arg("clear_under").toInt() != 0;

    if (body.length() > 0)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
            apiSendError(400, "invalid json");
            return;
        }

        timeoutMs = (uint32_t)(doc["timeout_ms"] | (uint32_t)timeoutMs);
        clear = (bool)(doc["clear"] | clear);
        clearUnder = (bool)(doc["clear_under"] | clearUnder);
        if (clear) custom_overlay_clear_timeline();
        if (clearUnder) custom_overlay_set_timeline_clear_under(true);

        JsonArrayConst colors = doc["colors"].as<JsonArrayConst>();
        if (!colors.isNull())
        {
            uint8_t i = 0;
            for (JsonVariantConst v : colors)
            {
                if (i >= WT_TIMELINE_PIXELS) break;
                uint32_t col;
                if (parseJsonColor(v, &col))
                {
                    custom_overlay_set_timeline_pixel(i, col);
                }
                i++;
            }
        }

        JsonArrayConst pixels = doc["pixels"].as<JsonArrayConst>();
        if (!pixels.isNull())
        {
            for (JsonObjectConst p : pixels)
            {
                int idx = p["i"] | p["index"] | -1;
                uint32_t col;
                if (idx < 0) continue;
                if (!parseJsonColor(p["color"], &col)) continue;
                custom_overlay_set_timeline_pixel((uint8_t)idx, col);
            }
        }
        else if (colors.isNull())
        {
            int idx = doc["i"] | doc["index"] | (server.hasArg("i") ? server.arg("i").toInt() : -1);
            uint32_t col;
            if (idx >= 0 && parseJsonColor(doc["color"], &col))
            {
                custom_overlay_set_timeline_pixel((uint8_t)idx, col);
            }
        }
    }
    else
    {
        if (clear) custom_overlay_clear_timeline();

        if (clearUnder) custom_overlay_set_timeline_clear_under(true);

        if (server.hasArg("i") && server.hasArg("color"))
        {
            int idx = server.arg("i").toInt();
            uint32_t col;
            if (!parseHexColor(server.arg("color"), &col))
            {
                apiSendError(400, "invalid color");
                return;
            }
            custom_overlay_set_timeline_pixel((uint8_t)idx, col);
        }
        else
        {
            apiSendError(400, "missing pixels" );
            return;
        }
    }

    custom_overlay_set_timeline_timeout_ms(now, timeoutMs);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiOverlayText()
{
    uint32_t now = millis();
    String body = server.arg("plain");
    body.trim();

    bool gotJson = body.length() > 0;
    JsonDocument doc;
    DeserializationError err = gotJson ? deserializeJson(doc, body) : DeserializationError::EmptyInput;

    if (gotJson && err)
    {
        int first = -1;
        for (int i = 0; i < (int)body.length(); ++i)
        {
            char c = body[i];
            if (c == '{' || c == '[') { first = i; break; }
        }
        if (first > 0)
        {
            String body2 = body.substring(first);
            body2.trim();

            if (body2.length() >= 2 && ((body2[0] == '\'' && body2[body2.length() - 1] == '\'') || (body2[0] == '"' && body2[body2.length() - 1] == '"')))
            {
                body2 = body2.substring(1, body2.length() - 1);
                body2.trim();
            }

            err = deserializeJson(doc, body2);
            if (!err) {
                body = body2;
            }
        }
    }

    // If JSON didn't parse, allow query/form parameters as a fallback:
    // /api/overlay/text?text=HELLO%20WORLD&x=0&y=0&color=%2300FF00&timeout_ms=10000&scroll=0
    if (err)
    {
        if (server.hasArg("text") || server.hasArg("msg"))
        {
            String textS = server.hasArg("text") ? server.arg("text") : server.arg("msg");
            bool scroll = server.hasArg("scroll") ? (server.arg("scroll").toInt() != 0) : false;
            bool clearUnder = server.hasArg("clear_under") ? (server.arg("clear_under").toInt() != 0) : false;

            int16_t x = scroll ? (int16_t)WT_MATRIX_WIDTH : 0;
            if (server.hasArg("x")) x = (int16_t)server.arg("x").toInt();
            int16_t yTop = server.hasArg("y") ? (int16_t)server.arg("y").toInt() : 0;
            uint32_t timeoutMs = server.hasArg("timeout_ms") ? (uint32_t)server.arg("timeout_ms").toInt() : 0;
            uint16_t scrollSpeedMs = server.hasArg("scroll_speed_ms") ? (uint16_t)server.arg("scroll_speed_ms").toInt() : (uint16_t)50;

            uint32_t col = wt_color(255, 255, 255);
            if (server.hasArg("color"))
            {
                if (!parseHexColor(server.arg("color"), &col))
                {
                    apiSendError(400, "invalid color");
                    return;
                }
            }

            if (clearUnder) custom_overlay_set_matrix_clear_under(true);
            custom_overlay_set_text(now, textS.c_str(), x, yTop, col, timeoutMs, scroll, scrollSpeedMs, nullptr, 0);
            server.send(200, "application/json", "{\"ok\":true}");
            return;
        }

        String preview = body;
        if (preview.length() > 80) preview = preview.substring(0, 80);
        preview.replace("\\", "\\\\");
        preview.replace("\"", "\\\"");
        preview.replace("\r", "\\r");
        preview.replace("\n", "\\n");

        String json;
        json.reserve(220);
        json += "{\"error\":\"invalid json: ";
        json += err.c_str();
        json += "\",\"len\":";
        json += String(body.length());
        json += ",\"preview\":\"";
        json += preview;
        json += "\"}";
        server.send(400, "application/json", json);
        return;
    }

    const char* text = doc["text"] | doc["msg"] | "";
    if (!text || text[0] == '\0')
    {
        apiSendError(400, "missing text");
        return;
    }

    bool scroll = (bool)(doc["scroll"] | false);
    bool clearUnder = (bool)(doc["clear_under"] | false);

    int16_t x;
    if (doc["x"].is<int>() || doc["x"].is<long>()) x = (int16_t)doc["x"].as<int>();
    else x = scroll ? (int16_t)WT_MATRIX_WIDTH : 0;

    int16_t yTop = (int16_t)(doc["y"] | 0);
    uint32_t timeoutMs = (uint32_t)(doc["timeout_ms"] | (uint32_t)0);
    uint16_t scrollSpeedMs = (uint16_t)(doc["scroll_speed_ms"] | (uint16_t)50);

    uint32_t col = wt_color(255, 255, 255);
    if (doc["color"].is<const char*>() || doc["color"].is<uint32_t>() || doc["color"].is<long>())
    {
        uint32_t parsed;
        if (!parseJsonColor(doc["color"], &parsed))
        {
            apiSendError(400, "invalid color");
            return;
        }
        col = parsed;
    }

    uint32_t perCharColors[96];
    size_t perCharColorsLen = 0;
    JsonArrayConst colors = doc["colors"].as<JsonArrayConst>();
    if (!colors.isNull())
    {
        for (JsonVariantConst v : colors)
        {
            if (perCharColorsLen >= 96) break;
            uint32_t c2;
            if (parseJsonColor(v, &c2))
            {
                perCharColors[perCharColorsLen++] = c2;
            }
            else
            {
                perCharColors[perCharColorsLen++] = col;
            }
        }
    }

    if (clearUnder) custom_overlay_set_matrix_clear_under(true);

    custom_overlay_set_text(now, text, x, yTop, col, timeoutMs, scroll, scrollSpeedMs, perCharColorsLen ? perCharColors : nullptr, perCharColorsLen);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiLang()
{
    Settings& cfg = settings_get();
    
    if (server.hasArg("lang")) {
        String lang = server.arg("lang");
        lang.toLowerCase();
        
        if (lang == "lv" || lang == "1") {
            cfg.uiLang = 1;
        } else if (lang == "en" || lang == "0") {
            cfg.uiLang = 0;
        } else {
            server.send(400, "application/json", "{\"error\":\"invalid lang, use 'en' or 'lv'\"}");
            return;
        }
        settings_save();
    }
    
    String json;
    json.reserve(64);
    json += "{\"ok\":true,\"lang\":\"";
    json += (cfg.uiLang == 1) ? "lv" : "en";
    json += "\"}";
    server.send(200, "application/json", json);
}

static void handleApiVersion()
{
    String json;
    json.reserve(256);
    json += "{";
    json += "\"app\":\"WeatherThing\"";
    json += ",\"sw_version\":\"0.1\"";
    json += ",\"git_sha\":\"";
    json += wt_fw_git_sha();
    json += "\"";
    json += ",\"git_sha_short\":\"";
    json += wt_fw_git_sha_short();
    json += "\"";
    json += ",\"build_date\":\"";
    json += wt_fw_build_date();
    json += "\"";
    json += ",\"dirty\":";
    json += wt_fw_git_dirty() ? "1" : "0";
    json += "}";
    server.send(200, "application/json", json);
}

static void handleApiDiag()
{
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t fragPct = 0;
    if (freeHeap > 0 && maxAllocHeap <= freeHeap) {
        fragPct = (uint32_t)(((uint64_t)(freeHeap - maxAllocHeap) * 100ULL) / (uint64_t)freeHeap);
    }

    uint32_t up = millis();
    uint8_t qWaiting = http_worker_queue_waiting();
    uint8_t qFree = http_worker_queue_free();
    uint8_t qCap = http_worker_queue_capacity();
    uint32_t enqOk = http_worker_enqueue_ok_count();
    uint32_t enqFail = http_worker_enqueue_fail_count();

    uint8_t wifiStatus = (uint8_t)WiFi.status();
    int32_t wifiRssi = 0;
    if (wifiStatus == (uint8_t)WL_CONNECTED) {
        wifiRssi = (int32_t)WiFi.RSSI();
    }

    String json;
    json.reserve(256);
    json += "{";
    json += "\"uptime_ms\":";
    json += String(up);
    json += ",\"free_heap\":";
    json += String(freeHeap);
    json += ",\"min_free_heap\":";
    json += String(minFreeHeap);
    json += ",\"max_alloc_heap\":";
    json += String(maxAllocHeap);
    json += ",\"largest_free_block\":";
    json += String(largestBlock);
    json += ",\"frag_pct\":";
    json += String(fragPct);
    json += ",\"httpq_waiting\":";
    json += String(qWaiting);
    json += ",\"httpq_free\":";
    json += String(qFree);
    json += ",\"httpq_cap\":";
    json += String(qCap);
    json += ",\"httpq_enq_ok\":";
    json += String(enqOk);
    json += ",\"httpq_enq_fail\":";
    json += String(enqFail);
    json += ",\"ap_mode\":";
    json += g_isApMode ? "1" : "0";
    json += ",\"wifi_status\":";
    json += String(wifiStatus);
    json += ",\"wifi_rssi\":";
    json += String(wifiRssi);
    json += "}";
    server.send(200, "application/json", json);
}

static String g_latestSha = "";
static String g_latestDate = "";
static uint32_t g_lastUpdateCheck = 0;
static bool g_updateAvailable = false;
static bool g_updateCheckInFlight = false;

// Background task to check for firmware updates (runs on http_worker thread)
static void updateCheckJob(void* ctx)
{
    (void)ctx;
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);
    
    http.begin(client, "https://makeriga.github.io/weatherthing-firmware/builds/index.json");
    http.setTimeout(10000);
    http.setConnectTimeout(10000);
    
    int code = http.GET();
    
    if (code == 200) {
        String payload = http.getString();
        const char* currentSha = wt_fw_git_sha_short();
        
        int shaIdx = payload.indexOf("\"sha_short\"");
        if (shaIdx >= 0) {
            int start = payload.indexOf("\"", shaIdx + 12) + 1;
            int end = payload.indexOf("\"", start);
            if (start > 0 && end > start) {
                g_latestSha = payload.substring(start, end);
            }
        }
        int dateIdx = payload.indexOf("\"build_date\"");
        if (dateIdx >= 0) {
            int start = payload.indexOf("\"", dateIdx + 13) + 1;
            int end = payload.indexOf("\"", start);
            if (start > 0 && end > start) {
                g_latestDate = payload.substring(start, end);
            }
        }
        if (g_latestSha.length() > 0 && strcmp(g_latestSha.c_str(), currentSha) != 0 && strcmp(currentSha, "unknown") != 0) {
            g_updateAvailable = true;
        }
    }
    http.end();
    g_updateCheckInFlight = false;
}

static void handleApiCheckUpdate()
{
    uint32_t now = millis();
    const char* currentSha = wt_fw_git_sha_short();
    
    // Trigger background update check every 5 minutes
    if (!g_updateCheckInFlight && (now - g_lastUpdateCheck > 300000 || g_lastUpdateCheck == 0)) {
        if (WiFi.status() == WL_CONNECTED && !g_isApMode) {
            g_lastUpdateCheck = now;
            g_updateCheckInFlight = true;
            http_worker_enqueue(updateCheckJob, nullptr);
        }
    }
    
    String json;
    json.reserve(256);
    json += "{";
    json += "\"update_available\":";
    json += g_updateAvailable ? "true" : "false";
    json += ",\"current_sha\":\"";
    json += currentSha;
    json += "\"";
    json += ",\"latest_sha\":\"";
    json += g_latestSha;
    json += "\"";
    json += ",\"latest_date\":\"";
    json += g_latestDate;
    json += "\"";
    json += "}";
    server.send(200, "application/json", json);
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
        server.send(400, "text/plain", TR("SSID required", "SSID nepiecie\u0161ams"));
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
    html += "<h1>&#x2705; ";
    html += TR("WiFi Saved!", "WiFi saglab\u0101ts!");
    html += "</h1>";
    html += "<p>";
    html += TR("Network", "T\u012bkls");
    html += ": <strong>";
    html += ssid;
    html += "</strong></p>";
    html += "<div class='spinner'></div>";
    html += "<p>";
    html += TR("Rebooting to connect...", "P\u0101rstart\u0113 savienojumam...");
    html += "</p>";
    html += "<p style='color:#888;font-size:0.9em'>";
    html += TR("Device will restart in 3 seconds", "Ier\u012bce restart\u0113sies p\u0113c 3 sekund\u0113m");
    html += "</p>";
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
    html += "<h1>";
    html += TR("Location saved", "Atra\u0161an\u0101s vieta saglab\u0101ta");
    html += "</h1>";
    html += "<p>";
    html += TR("Latitude", "Platums");
    html += ": ";
    html += String(lat, 4);
    html += "</p>";
    html += "<p>";
    html += TR("Longitude", "Garums");
    html += ": ";
    html += String(lon, 4);
    html += "</p>";
    html += "<p>";
    html += TR("Weather will refresh shortly.", "Laika apst\u0101k\u013ci dr\u012bz atjaunin\u0101sies.");
    html += "</p>";
    html += "<p><a href='/'>";
    html += TR("Back to setup", "Atpaka\u013c uz iestat\u012b\u0161anu");
    html += "</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

static void handleCityPost()
{
    String city = server.hasArg("city") ? server.arg("city") : "";
    city.trim();

    if (city.length() < 2)
    {
        server.send(400, "text/plain", TR("City name required", "Nepiecie\u0161ams pils\u0113tas nosaukums"));
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
        html += "<h1>";
        html += TR("City found!", "Pils\u0113ta atrasta!");
        html += "</h1>";
        html += "<p>";
        html += TR("City", "Pils\u0113ta");
        html += ": ";
        html += city;
        html += "</p>";
        html += "<p>";
        html += TR("Coordinates", "Koordin\u0101tas");
        html += ": ";
        html += String(lat, 4);
        html += ", ";
        html += String(lon, 4);
        html += "</p>";
        html += "<p>";
        html += TR("Weather will refresh shortly.", "Laika apst\u0101k\u013ci dr\u012bz atjaunin\u0101sies.");
        html += "</p>";
    }
    else
    {
        html += "<h1>";
        html += TR("City not found", "Pils\u0113ta nav atrasta");
        html += "</h1>";
        html += "<p>";
        html += TR("Could not find", "Nevar\u0113ja atrast");
        html += ": ";
        html += city;
        html += "</p>";
        html += "<p>";
        html += TR("Try a different spelling or use coordinates.", "M\u0113\u0123iniet citu rakst\u012bbu vai izmantojiet koordin\u0101tas.");
        html += "</p>";
    }
    
    html += "<p><a href='/'>";
    html += TR("Back to setup", "Atpaka\u013c uz iestat\u012b\u0161anu");
    html += "</a></p>";
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

    if (server.hasArg("ajax") && server.arg("ajax") == "1") {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

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
    
    if (server.hasArg("micBoost")) {
        cfg.micBoost = (uint8_t)server.arg("micBoost").toInt();
        if (cfg.micBoost > 10) cfg.micBoost = 10;
    }
    
    if (server.hasArg("silenceMs")) {
        cfg.vuSilenceMs = (uint16_t)server.arg("silenceMs").toInt();
        if (cfg.vuSilenceMs > 2000) cfg.vuSilenceMs = 2000;
    }

    if (server.hasArg("agcMin")) cfg.agcMin = (uint16_t)server.arg("agcMin").toInt();
    if (server.hasArg("agcMax")) cfg.agcMax = (uint16_t)server.arg("agcMax").toInt();
    if (server.hasArg("agcAtk")) cfg.agcAttack = (uint8_t)server.arg("agcAtk").toInt();
    if (server.hasArg("agcDcy")) cfg.agcDecay = (uint8_t)server.arg("agcDcy").toInt();
    if (server.hasArg("envAtk")) cfg.envAttack = (uint8_t)server.arg("envAtk").toInt();
    if (server.hasArg("envDcy")) cfg.envDecay = (uint8_t)server.arg("envDcy").toInt();
    if (server.hasArg("beatThr")) cfg.beatThreshold = (uint8_t)server.arg("beatThr").toInt();
    if (server.hasArg("beatHld")) cfg.beatHold = (uint8_t)server.arg("beatHld").toInt();
    
    // Checkbox settings (true if present in form)
    cfg.vuInvert = server.hasArg("micInvert");
    cfg.agcEnabled = server.hasArg("agcOn");

    if (server.hasArg("tempPalette")) {
        cfg.tempPalette = (uint8_t)server.arg("tempPalette").toInt();
        if (cfg.tempPalette > 2) cfg.tempPalette = 0;
    }
    cfg.wxAudioHue = server.hasArg("wxAudHue");
    cfg.wxAudioSpeed = server.hasArg("wxAudSpd");
    
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
        if (cfg.brightManual > 255) cfg.brightManual = 255;
    }
    if (server.hasArg("brightMin")) {
        cfg.brightMin = (uint8_t)server.arg("brightMin").toInt();
        if (cfg.brightMin < 1) cfg.brightMin = 1;
        if (cfg.brightMin > 40) cfg.brightMin = 40;
    }
    // High power mode checkbox (must be checked before brightness limits)
    cfg.highPowerMode = server.hasArg("highPower");
    uint8_t maxAllowed = cfg.highPowerMode ? 255 : 127;
    if (server.hasArg("brightMax")) {
        cfg.brightMax = (uint8_t)server.arg("brightMax").toInt();
        if (cfg.brightMax < 10) cfg.brightMax = 10;
        if (cfg.brightMax > maxAllowed) cfg.brightMax = maxAllowed;
    }
    if (server.hasArg("brightManual")) {
        cfg.brightManual = (uint8_t)server.arg("brightManual").toInt();
        if (cfg.brightManual < 5) cfg.brightManual = 5;
        if (cfg.brightManual > maxAllowed) cfg.brightManual = maxAllowed;
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
    if (server.hasArg("rssCnt")) {
        uint8_t cnt = (uint8_t)server.arg("rssCnt").toInt();
        if (cnt >= 1 && cnt <= 10) cfg.rssItemCount = cnt;
    }
    if (server.hasArg("rssFmt")) {
        cfg.rssFormat = (uint8_t)server.arg("rssFmt").toInt();
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

    if (server.hasArg("ajax") && server.arg("ajax") == "1") {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
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
    json += "\"weatherPreset\":" + String(cfg.weatherPreset) + ",";
    json += "\"weatherProvider\":" + String(cfg.weatherProvider) + ",";
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
    if (server.hasArg("ajax") && server.arg("ajax") == "1") {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
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

// Save touch shortcut setting
static void handleApiTouchShortcut()
{
    if (server.hasArg("val")) {
        String val = server.arg("val");
        int underscore = val.indexOf('_');
        if (underscore > 0) {
            Settings& cfg = settings_get();
            cfg.touchShortcutCard = (uint8_t)val.substring(0, underscore).toInt();
            cfg.touchShortcutPreset = (uint8_t)val.substring(underscore + 1).toInt();
            settings_save();
        }
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
            if (card >= 0 && card < 16 && preset >= 0 && preset < 64) {
                cfg.presetEnabled[card] |= (1ULL << preset);
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
    
    // 4. Transition Settings
    cfg.showTransitionTitle = server.hasArg("trTitle");
    cfg.showTransitionAnim = server.hasArg("trAnim");
    
    // 5. Demo Mode Settings
    cfg.demoMode = server.hasArg("demoOn");
    
    // 6. Touch Shortcut Settings (format: "card_preset")
    if (server.hasArg("touchShortcut")) {
        String val = server.arg("touchShortcut");
        int underscore = val.indexOf('_');
        if (underscore > 0) {
            cfg.touchShortcutCard = (uint8_t)val.substring(0, underscore).toInt();
            cfg.touchShortcutPreset = (uint8_t)val.substring(underscore + 1).toInt();
        }
    }
    
    // 7. Audio settings (from Audio card in gallery)
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
    if (server.hasArg("micBoost")) {
        cfg.micBoost = (uint8_t)server.arg("micBoost").toInt();
        if (cfg.micBoost > 10) cfg.micBoost = 10;
    }
    if (server.hasArg("silenceMs")) {
        cfg.vuSilenceMs = (uint16_t)server.arg("silenceMs").toInt();
        if (cfg.vuSilenceMs > 2000) cfg.vuSilenceMs = 2000;
    }
    if (server.hasArg("agcMin")) cfg.agcMin = (uint16_t)server.arg("agcMin").toInt();
    if (server.hasArg("agcMax")) cfg.agcMax = (uint16_t)server.arg("agcMax").toInt();
    if (server.hasArg("agcAtk")) cfg.agcAttack = (uint8_t)server.arg("agcAtk").toInt();
    if (server.hasArg("agcDcy")) cfg.agcDecay = (uint8_t)server.arg("agcDcy").toInt();
    if (server.hasArg("envAtk")) cfg.envAttack = (uint8_t)server.arg("envAtk").toInt();
    if (server.hasArg("envDcy")) cfg.envDecay = (uint8_t)server.arg("envDcy").toInt();
    if (server.hasArg("beatThr")) cfg.beatThreshold = (uint8_t)server.arg("beatThr").toInt();
    if (server.hasArg("beatHld")) cfg.beatHold = (uint8_t)server.arg("beatHld").toInt();
    cfg.vuInvert = server.hasArg("micInvert");
    cfg.agcEnabled = server.hasArg("agcOn");
    
    // 5. Weather settings (from Weather card in gallery)
    String city = server.hasArg("city") ? server.arg("city") : "";
    city.trim();
    if (city.length() >= 2) {
        weather_set_city(city.c_str());
    } else if (server.hasArg("lat") && server.hasArg("lon")) {
        float newLat = server.arg("lat").toFloat();
        float newLon = server.arg("lon").toFloat();
        if (newLat >= -90.0f && newLat <= 90.0f && newLon >= -180.0f && newLon <= 180.0f) {
            float curLat, curLon;
            weather_get_location(&curLat, &curLon);
            if (fabsf(newLat - curLat) > 0.0005f || fabsf(newLon - curLon) > 0.0005f) {
                weather_set_location(newLat, newLon);
            }
        }
    }
    if (server.hasArg("wxProv")) {
        cfg.weatherProvider = (uint8_t)server.arg("wxProv").toInt();
        if (cfg.weatherProvider > 2) cfg.weatherProvider = 0;
    }
    if (server.hasArg("tempPalette")) {
        cfg.tempPalette = (uint8_t)server.arg("tempPalette").toInt();
        if (cfg.tempPalette > 2) cfg.tempPalette = 0;
    }
    cfg.wxAudioHue = server.hasArg("wxAudHue");
    cfg.wxAudioSpeed = server.hasArg("wxAudSpd");
    if (server.hasArg("forecastHours")) {
        cfg.forecastHours = (uint8_t)server.arg("forecastHours").toInt();
    }
    if (server.hasArg("mapZoom")) {
        uint8_t z = (uint8_t)server.arg("mapZoom").toInt();
        if (z >= 2 && z <= 6) cfg.mapZoom = z;
    }
    if (server.hasArg("mapStyle")) {
        uint8_t s = (uint8_t)server.arg("mapStyle").toInt();
        if (s <= 2) cfg.mapStyle = s;
    }

    if (server.hasArg("wxTlSun")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlSun"), &c)) cfg.wxTimelineSunny = c;
    }
    if (server.hasArg("wxTlCld")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlCld"), &c)) cfg.wxTimelineCloudy = c;
    }
    if (server.hasArg("wxTlRai")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlRai"), &c)) cfg.wxTimelineRain = c;
    }
    if (server.hasArg("wxTlSto")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlSto"), &c)) cfg.wxTimelineStorm = c;
    }
    if (server.hasArg("wxTlSno")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlSno"), &c)) cfg.wxTimelineSnow = c;
    }
    if (server.hasArg("wxTlWin")) {
        uint32_t c;
        if (parseHexColor(server.arg("wxTlWin"), &c)) cfg.wxTimelineWind = c;
    }
    
    // 6. Clock settings
    if (server.hasArg("tz")) {
        int8_t tz = (int8_t)server.arg("tz").toInt();
        if (tz >= -12 && tz <= 14) cfg.tzOffset = tz;
    }
    
    // 7. BTC/Crypto settings
    if (server.hasArg("crypto")) {
        String sym = server.arg("crypto");
        sym.trim();
        sym.toUpperCase();
        strncpy(cfg.cryptoSymbol, sym.c_str(), sizeof(cfg.cryptoSymbol) - 1);
        cfg.cryptoSymbol[sizeof(cfg.cryptoSymbol) - 1] = '\0';
    }
    if (server.hasArg("btcMins")) {
        uint8_t mins = (uint8_t)server.arg("btcMins").toInt();
        if (mins >= 1 && mins <= 60) cfg.btcUpdateMins = mins;
    }
    
    // 8. Stock settings
    if (server.hasArg("stock")) {
        String sym = server.arg("stock");
        sym.trim();
        sym.toUpperCase();
        strncpy(cfg.stockSymbol, sym.c_str(), sizeof(cfg.stockSymbol) - 1);
        cfg.stockSymbol[sizeof(cfg.stockSymbol) - 1] = '\0';
        cfg.stockEnabled = sym.length() > 0;
    }
    if (server.hasArg("stockMins")) {
        uint8_t mins = (uint8_t)server.arg("stockMins").toInt();
        if (mins >= 1 && mins <= 60) cfg.stockUpdateMins = mins;
    }
    
    // 9. MQTT settings
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
            if (cfg.mqttPort != port) { cfg.mqttPort = port; mqttChanged = true; }
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
        if (pwd.length() > 0) {
            strncpy(cfg.mqttPass, pwd.c_str(), sizeof(cfg.mqttPass) - 1);
            cfg.mqttPass[sizeof(cfg.mqttPass) - 1] = '\0';
        }
    }
    cfg.mqttEnabled = (cfg.mqttServer[0] != '\0');
    
    // 10. RSS settings
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
    
    // 11. YouTube settings
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

    if (server.hasArg("ajax") && server.arg("ajax") == "1") {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

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
    Settings& cfg = settings_get();
    html += "<div class=\"header\">";
    html += "<h1>&#x1F3A8; ";
    html += TR("Sprite Editor", "Spraitu redaktors");
    html += "</h1>";
    html += "<a href=\"/\" class=\"back-link\">&larr; ";
    html += TR("Back to Control Panel", "Atpaka\u013c uz vad\u012bbas paneli");
    html += "</a>";
    html += "</div>";
    html += "<div class=\"container\">";
    html += "<div class=\"layout\">";
    html += "<div class=\"main-panel\">";
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">";
    html += TR("Select Sprite to Edit", "Izv\u0113lieties spraitu redi\u0123\u0113\u0161anai");
    html += "</span></div>";
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
    html += "<p class=\"info\">";
    html += TR("Click to toggle pixels - Drag to paint", "Noklik\u0161\u0137iniet, lai p\u0101rsl\u0113gtu pikse\u013cus - Velciet, lai z\u012bm\u0113tu");
    html += "</p>";
    html += "</div>";
    html += "<div class=\"btn-group\">";
    html += "<button class=\"btn btn-primary\" onclick=\"saveSprite()\">&#x1F4BE; ";
    html += TR("Save Changes", "Saglab\u0101t izmai\u0146as");
    html += "</button>";
    html += "<button class=\"btn btn-secondary\" onclick=\"clearGrid()\">";
    html += TR("Clear", "Not\u012br\u012bt");
    html += "</button>";
    html += "<button class=\"btn btn-secondary\" onclick=\"invertGrid()\">";
    html += TR("Invert", "Invert\u0113t");
    html += "</button>";
    html += "<button class=\"btn btn-danger\" onclick=\"resetSprite()\">";
    html += TR("Reset", "Atiestat\u012bt");
    html += "</button>";
    html += "</div>";
    html += "</div>";
    html += "</div>";

    html += "<div class=\"side-panel\">";
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">";
    html += TR("Preview", "Priek\u0161skat\u012bjums");
    html += "</span></div>";
    html += "<div class=\"preview-section\">";
    html += "<div class=\"preview-box\">";
    html += "<div class=\"mini-grid\" id=\"preview\"></div>";
    html += "</div>";
    html += "<p class=\"preview-label\">";
    html += TR("Actual Size", "\u012astais izm\u0113rs");
    html += "</p>";
    html += "</div>";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-title\">";
    html += TR("Quick Select", "\u0100tr\u0101 izv\u0113le");
    html += "</span></div>";
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
