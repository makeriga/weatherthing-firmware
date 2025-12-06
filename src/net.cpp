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
static void handleEditor();
static void handleApiSprites();
static void handleApiSpriteGet();
static void handleApiSpriteSave();
static void handleApiSpriteReset();

static void handleRoot()
{
    String html;
    html.reserve(6000);
    
    html += R"(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WeatherThing Control Panel</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#f0f0f0;--card:#ffffff;--border:#000000;--text:#000000;--accent:#ffcc00;--success:#4ade80;--danger:#ff6b6b}
body{font-family:'Courier New', Courier, monospace;background:var(--bg);color:var(--text);min-height:100vh;line-height:1.5;padding-bottom:50px}
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
.weather-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:15px}
.weather-btn{padding:15px;border:3px solid #000;background:#fff;cursor:pointer;transition:all 0.2s;font-size:1.5em;box-shadow:3px 3px 0 #000}
.weather-btn:hover{transform:translate(-2px,-2px);box-shadow:6px 6px 0 #000;background:#ffffcc}
.temp-input{display:flex;align-items:center;gap:10px}
.temp-input input{width:100px;text-align:center;font-size:1.5em}
.footer{text-align:center;margin-top:40px;font-weight:bold;text-transform:uppercase}
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

    // Display Control Card - Organized by section
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F4FA;</span><span class=\"card-title\">Display Control</span></div>";
    
    // WEATHER section
    html += "<div style=\"margin-bottom:16px\">";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x26C5; Weather</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap;margin-bottom:8px\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"0\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Classic</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"1\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Big Temp</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"2\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Corner</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"3\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Animated</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"4\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Minimal</button></form>";
    html += "</div>";
    // Playful weather styles
    html += "<p style=\"color:var(--muted);font-size:0.75em;margin-bottom:4px\">&#x1F3A8; Playful &amp; Artistic</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"5\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F305; Day/Night</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"6\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4BB; Terminal</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"7\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F170; Big Type</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"0\"><input type=\"hidden\" name=\"preset\" value=\"8\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4CA; Forecast</button></form>";
    html += "</div></div>";
    
    // CLOCK section
    html += "<div style=\"margin-bottom:16px\">";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x1F551; Clock</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"0\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Digital</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"1\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Binary</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"2\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Minimal</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"3\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Bars</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"4\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4A1; Nixie</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"5\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F47E; Glitch</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"1\"><input type=\"hidden\" name=\"preset\" value=\"6\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F3D3; Pong</button></form>";
    html += "</div></div>";
    
    // FINANCIAL section
    html += "<div style=\"margin-bottom:16px\">";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x1F4B0; Financial</p>";
    html += "<div style=\"display:flex;gap:6px\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"2\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x20BF; Bitcoin</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"3\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4C8; Stock</button></form>";
    html += "</div></div>";
    
    // AUDIO section  
    html += "<div style=\"margin-bottom:16px\">";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x1F3B5; Audio Reactive</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap;margin-bottom:8px\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"0\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Spectrum</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"2\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F525; Fire</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"6\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Plasma</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"8\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4BB; Matrix</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"11\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F4A5; Laser</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"6\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x2728; Sparkle</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"7\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F308; Aurora</button></form>";
    html += "</div>";
    // Playful audio styles
    html += "<p style=\"color:var(--muted);font-size:0.75em;margin-bottom:4px\">&#x1F3A8; Playful</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"12\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F57A; Dancer</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"13\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F497; Heartbeat</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"14\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F697; Traffic</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"15\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F47B; Pacman</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"5\"><input type=\"hidden\" name=\"preset\" value=\"16\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F300; Vortex</button></form>";
    html += "</div></div>";
    
    // OTHER section
    html += "<div style=\"margin-bottom:16px\">";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x2699; Other</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"4\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F310; Network</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"9\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">&#x1F3E0; MQTT</button></form>";
    html += "</div></div>";
    
    // GAMES section
    html += "<div>";
    html += "<p style=\"color:var(--accent);font-weight:600;margin-bottom:8px\">&#x1F3AE; Games</p>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"8\"><input type=\"hidden\" name=\"preset\" value=\"0\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Flappy</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"8\"><input type=\"hidden\" name=\"preset\" value=\"1\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Snake</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"8\"><input type=\"hidden\" name=\"preset\" value=\"2\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Breakout</button></form>";
    html += "<form method=\"POST\" action=\"/card\" style=\"margin:0\"><input type=\"hidden\" name=\"card\" value=\"8\"><input type=\"hidden\" name=\"preset\" value=\"3\"><button type=\"submit\" class=\"btn btn-secondary\" style=\"padding:6px 10px;font-size:0.8em\">Pong</button></form>";
    html += "</div></div>";
    
    // Quick Controls Reference
    html += "<div style=\"margin-top:16px;padding:12px;background:rgba(255,255,255,0.05);border-radius:8px\">";
    html += "<h4 style=\"margin:0 0 8px;font-size:0.9em;color:var(--primary)\">&#x1F3AE; Button Controls</h4>";
    html += "<div style=\"font-size:0.8em;color:var(--muted);line-height:1.6\">";
    html += "<p style=\"margin:4px 0\"><strong>BTN1</strong> &#8594; Next card/style</p>";
    html += "<p style=\"margin:4px 0\"><strong>BTN2</strong> &#8594; Previous card/style</p>";
    html += "<p style=\"margin:4px 0\"><strong>Touch</strong> &#8594; Game action / Confirm</p>";
    html += "<p style=\"margin:4px 0\"><strong>Games:</strong> BTN1/BTN2 move paddle, Touch to jump/restart. Hold both buttons to exit games.</p>";
    html += "</div></div>";
    
    html += "</div>";

    // Location Card
    float lat, lon;
    weather_get_location(&lat, &lon);
    
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F4CD;</span><span class=\"card-title\">Location</span></div>";
    html += "<div class=\"info-box\">Current: <strong>";
    html += String(lat, 4) + ", " + String(lon, 4);
    html += "</strong></div>";
    html += "<form method=\"POST\" action=\"/city\">";
    html += "<div class=\"form-group\"><label>Search by City</label>";
    html += "<div class=\"row\"><input name=\"city\" placeholder=\"e.g. London, Tokyo, New York\">";
    html += "<button type=\"submit\" class=\"btn btn-secondary\">&#x1F50D;</button></div></div>";
    html += "</form>";
    html += "<form method=\"POST\" action=\"/location\">";
    html += "<div class=\"row\">";
    html += "<div class=\"form-group\"><label>Latitude</label><input name=\"lat\" type=\"number\" step=\"0.0001\" value=\"";
    html += String(lat, 4);
    html += "\"></div>";
    html += "<div class=\"form-group\"><label>Longitude</label><input name=\"lon\" type=\"number\" step=\"0.0001\" value=\"";
    html += String(lon, 4);
    html += "\"></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary\">&#x1F4CC;</button>";
    html += "</div></form></div>";

    // Weather Simulation Card - Expanded types
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F9EA;</span><span class=\"card-title\">Weather Simulation</span></div>";
    html += "<form method=\"POST\" action=\"/simulate\" id=\"simForm\">";
    html += "<div class=\"form-group\"><label>Weather Type</label>";
    html += "<div class=\"weather-grid\" style=\"grid-template-columns:repeat(6,1fr)\">";
    // Row 1: Sunny, Partly Cloudy, Cloudy, Fog, Drizzle, Rain
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"0\" onclick=\"selectWeather(this)\" title=\"Sunny\">&#x2600;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"1\" onclick=\"selectWeather(this)\" title=\"Partly Cloudy\">&#x26C5;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"2\" onclick=\"selectWeather(this)\" title=\"Cloudy\">&#x2601;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"3\" onclick=\"selectWeather(this)\" title=\"Fog\">&#x1F32B;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"4\" onclick=\"selectWeather(this)\" title=\"Drizzle\">&#x1F326;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"5\" onclick=\"selectWeather(this)\" title=\"Rain\">&#x1F327;</button>";
    // Row 2: Heavy Rain, Storm, Snow, Sleet, Wind, Clear Night
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"6\" onclick=\"selectWeather(this)\" title=\"Heavy Rain\">&#x1F4A7;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"7\" onclick=\"selectWeather(this)\" title=\"Storm\">&#x26C8;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"8\" onclick=\"selectWeather(this)\" title=\"Snow\">&#x2744;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"9\" onclick=\"selectWeather(this)\" title=\"Sleet\">&#x1F328;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"10\" onclick=\"selectWeather(this)\" title=\"Wind\">&#x1F4A8;</button>";
    html += "<button type=\"button\" class=\"weather-btn\" data-val=\"11\" onclick=\"selectWeather(this)\" title=\"Clear Night\">&#x1F319;</button>";
    html += "</div></div>";
    html += "<input type=\"hidden\" name=\"type\" id=\"weatherType\" value=\"0\">";
    html += "<div class=\"form-group\"><label>Temperature</label>";
    html += "<div class=\"temp-input\">";
    html += "<input name=\"temp\" type=\"number\" value=\"22\" min=\"-50\" max=\"50\">";
    html += "<span>&deg;C</span>";
    html += "</div></div>";
    html += "<button type=\"submit\" class=\"btn btn-accent btn-full\">&#x25B6; Simulate Weather</button>";
    html += "</form></div>";

    // Unified Configuration Card
    Settings& cfg = settings_get();
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x2699;</span><span class=\"card-title\">System Configuration</span></div>";
    
    // Weather Display Section
    html += "<details><summary>Weather Display</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>Temperature Color Palette</label>";
    html += "<select name=\"tempPalette\" style=\"width:100%\">";
    const char* tempPals[] = {"Default (Blue to Red)", "Cool Tones (Purple-Blue)", "Warm Tones (Yellow-Red)"};
    for (uint8_t i = 0; i < 3; ++i) {
        html += "<option value=\"" + String(i) + "\"";
        if (cfg.tempPalette == i) html += " selected";
        html += ">" + String(tempPals[i]) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Timeline Forecast Range</label>";
    html += "<select name=\"forecastHours\">";
    html += "<option value=\"12\""; if (cfg.forecastHours == 12) html += " selected"; html += ">12 hours (1h per LED)</option>";
    html += "<option value=\"24\""; if (cfg.forecastHours == 24) html += " selected"; html += ">24 hours (2h per LED)</option>";
    html += "<option value=\"48\""; if (cfg.forecastHours == 48) html += " selected"; html += ">48 hours (4h per LED)</option>";
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Simulation Timeout (seconds)</label>";
    html += "<select name=\"simTimeout\">";
    html += "<option value=\"0\""; if (cfg.simTimeoutSecs == 0) html += " selected"; html += ">Never (manual stop)</option>";
    html += "<option value=\"30\""; if (cfg.simTimeoutSecs == 30) html += " selected"; html += ">30 sec</option>";
    html += "<option value=\"60\""; if (cfg.simTimeoutSecs == 60) html += " selected"; html += ">1 min</option>";
    html += "<option value=\"120\""; if (cfg.simTimeoutSecs == 120) html += " selected"; html += ">2 min</option>";
    html += "<option value=\"300\""; if (cfg.simTimeoutSecs == 300) html += " selected"; html += ">5 min</option>";
    html += "</select></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Weather Settings</button>";
    html += "</form></details>";
    
    // Audio Visualizer Section
    html += "<details><summary>Audio Visualizer</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>Color Palette</label>";
    html += "<select name=\"palette\" style=\"width:100%\">";
    for (uint8_t i = 0; i < PALETTE_COUNT; ++i) {
        html += "<option value=\"" + String(i) + "\"";
        if (cfg.vuPalette == i) html += " selected";
        html += ">" + String(settings_palette_name(i)) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Animation Speed</label>";
    html += "<select name=\"speed\" style=\"width:100%\">";
    const char* speeds[] = {"Very Slow", "Slow", "Medium-Slow", "Medium", "Normal", "Medium-Fast", "Fast", "Very Fast", "Ultra", "Maximum"};
    for (uint8_t i = 1; i <= 10; ++i) {
        html += "<option value=\"" + String(i) + "\"";
        if (cfg.animSpeed == i) html += " selected";
        html += ">" + String(speeds[i-1]) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Mic Gain</label>";
    html += "<select name=\"micGain\" style=\"width:100%\">";
    const char* gains[] = {"Very Low", "Low", "Low-Med", "Medium-Low", "Normal", "Medium-High", "High", "Very High", "Ultra", "Maximum"};
    for (uint8_t i = 1; i <= 10; ++i) {
        html += "<option value=\"" + String(i) + "\"";
        if (cfg.micGain == i) html += " selected";
        html += ">" + String(gains[i-1]) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Noise Gate</label>";
    html += "<select name=\"noiseGate\" style=\"width:100%\">";
    const char* gates[] = {"Off", "Very Low", "Low", "Medium-Low", "Medium", "Medium-High", "High", "Very High", "Ultra", "Maximum"};
    for (uint8_t i = 0; i <= 9; ++i) {
        uint8_t val = i * 25;
        html += "<option value=\"" + String(val) + "\"";
        if (cfg.vuNoiseGate >= val && cfg.vuNoiseGate < val + 25) html += " selected";
        html += ">" + String(gates[i]) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label><input type=\"checkbox\" name=\"vuInv\" value=\"1\"";
    if (cfg.vuInvert) html += " checked";
    html += "> Invert Response (fix reversed mic)</label></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Audio Settings</button>";
    html += "</form></details>";

    // Clock Section
    html += "<details><summary>Clock & Time</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>Timezone Offset (UTC)</label>";
    html += "<select name=\"tz\">";
    for (int8_t tz = -12; tz <= 14; ++tz) {
        html += "<option value=\"" + String(tz) + "\"";
        if (cfg.tzOffset == tz) html += " selected";
        html += ">UTC";
        if (tz >= 0) html += "+";
        html += String(tz) + "</option>";
    }
    html += "</select></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Timezone</button>";
    html += "</form></details>";

    // Stocks Section
    html += "<details><summary>Stocks & Crypto</summary>";
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>Stock Symbol</label>";
    html += "<input name=\"stock\" type=\"text\" maxlength=\"10\" placeholder=\"AAPL, TSLA, MSFT...\" value=\"";
    html += cfg.stockSymbol;
    html += "\" style=\"text-transform:uppercase\">";
    html += "</div>";
    html += "<div class=\"row\">";
    html += "<div class=\"form-group\"><label>BTC Update (min)</label>";
    html += "<select name=\"btcMins\">";
    const uint8_t intervals[] = {1, 2, 5, 10, 15, 30, 60};
    for (uint8_t i = 0; i < 7; ++i) {
        html += "<option value=\"" + String(intervals[i]) + "\"";
        if (cfg.btcUpdateMins == intervals[i]) html += " selected";
        html += ">" + String(intervals[i]) + "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-group\"><label>Stock Update (min)</label>";
    html += "<select name=\"stockMins\">";
    for (uint8_t i = 0; i < 7; ++i) {
        html += "<option value=\"" + String(intervals[i]) + "\"";
        if (cfg.stockUpdateMins == intervals[i]) html += " selected";
        html += ">" + String(intervals[i]) + "</option>";
    }
    html += "</select></div>";
    html += "</div>"; // row
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Financial Settings</button>";
    html += "</form></details>";
    
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
    html += "<input type=\"range\" name=\"brightManual\" min=\"5\" max=\"80\" value=\"" + String(cfg.brightManual) + "\"></div>";
    html += "<div class=\"form-group\"><label>Auto Min (dark room): " + String(cfg.brightMin) + "</label>";
    html += "<input type=\"range\" name=\"brightMin\" min=\"5\" max=\"40\" value=\"" + String(cfg.brightMin) + "\"></div>";
    html += "<div class=\"form-group\"><label>Auto Max (bright room): " + String(cfg.brightMax) + "</label>";
    html += "<input type=\"range\" name=\"brightMax\" min=\"20\" max=\"80\" value=\"" + String(cfg.brightMax) + "\"></div>";
    html += "<div class=\"form-group\"><label><input type=\"checkbox\" name=\"brightBlank\" value=\"1\"";
    if (cfg.brightBlanking) html += " checked";
    html += "> Use blanking for cleaner readings</label></div>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save Brightness</button>";
    html += "</form></details>";

    // MQTT Section
    html += "<details><summary>MQTT & Smart Home</summary>";
    // Connection status indicator
    if (mqtt_is_connected()) {
        html += "<div class='info-box' style='background:rgba(35,134,54,0.2);border-color:rgba(35,134,54,0.4)'>";
        html += "<strong style='color:#3fb950'>&#x2713; Connected to MQTT</strong></div>";
    } else if (cfg.mqttServer[0] != '\0') {
        html += "<div class='info-box' style='background:rgba(218,54,51,0.2);border-color:rgba(218,54,51,0.4)'>";
        html += "<strong style='color:#da3633'>&#x2717; MQTT Disconnected</strong></div>";
    }
    
    html += "<form method=\"POST\" action=\"/settings\">";
    html += "<div class=\"form-group\"><label>MQTT Broker</label>";
    html += "<input name=\"mqttServer\" placeholder=\"192.168.1.100 or mqtt.local\" value=\"";
    html += cfg.mqttServer;
    html += "\"></div>";
    html += "<div class=\"row\">";
    html += "<div class=\"form-group\"><label>Port</label><input name=\"mqttPort\" type=\"number\" value=\"";
    html += String(cfg.mqttPort);
    html += "\"></div>";
    html += "<div class=\"form-group\"><label>Username</label><input name=\"mqttUser\" placeholder=\"(optional)\" value=\"";
    html += cfg.mqttUser;
    html += "\"></div>";
    html += "</div>";
    html += "<div class=\"form-group\"><label>Password</label><input name=\"mqttPass\" type=\"password\" placeholder=\"(optional)\" value=\"";
    html += cfg.mqttPass;
    html += "\"></div>";
    html += "<div class=\"form-group\"><label>Custom Topic (subscribe)</label><input name=\"mqttTopic\" placeholder=\"home/notifications\" value=\"";
    html += cfg.mqttTopic;
    html += "\"></div>";
    html += "<p style=\"color:var(--muted);font-size:0.8em;margin-bottom:12px\">";
    html += "Device auto-registers with Home Assistant via MQTT discovery.<br>";
    html += "Send notifications to: <code>weatherthing/wt_XXXX/notify</code></p>";
    html += "<button type=\"submit\" class=\"btn btn-primary btn-full\">&#x1F4BE; Save MQTT Settings</button>";
    html += "</form></details>";
    
    html += "</div>"; // End card

    // Tools Card
    html += "<div class=\"card\">";
    html += "<div class=\"card-header\"><span class=\"card-icon\">&#x1F6E0;</span><span class=\"card-title\">Tools &amp; Customization</span></div>";
    html += "<p style=\"color:var(--muted);margin-bottom:16px;font-size:0.9em\">Customize the display and edit pixel sprites</p>";
    html += "<a href=\"/editor\" class=\"btn btn-accent btn-full\" style=\"margin-bottom:10px\">&#x1F3A8; Open Sprite Editor</a>";
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
function selectWeather(btn){
document.querySelectorAll('.weather-btn').forEach(b=>b.classList.remove('selected'));
btn.classList.add('selected');
document.getElementById('weatherType').value=btn.dataset.val;
}
document.querySelector('.weather-btn').classList.add('selected');
</script>
</body></html>)";

    server.send(200, "text/html", html);
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
    server.on("/card", HTTP_POST, handleCardSwitch);
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
    html.reserve(512);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>WeatherThing WiFi</title></head><body>";
    html += "<h1>WiFi settings saved</h1>";
    html += "<p>SSID: ";
    html += ssid;
    html += "</p>";
    html += "<p>Device will use these credentials on the next restart.</p>";
    html += "<p><a href='/'>Back to setup</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
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
    
    // Checkbox: if not present, it means unchecked
    cfg.vuInvert = server.hasArg("vuInv");

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
        if (cfg.brightManual < 5) cfg.brightManual = 5;
        if (cfg.brightManual > 80) cfg.brightManual = 80;
    }
    if (server.hasArg("brightMin")) {
        cfg.brightMin = (uint8_t)server.arg("brightMin").toInt();
        if (cfg.brightMin < 5) cfg.brightMin = 5;
        if (cfg.brightMin > 40) cfg.brightMin = 40;
    }
    if (server.hasArg("brightMax")) {
        cfg.brightMax = (uint8_t)server.arg("brightMax").toInt();
        if (cfg.brightMax < 20) cfg.brightMax = 20;
        if (cfg.brightMax > 80) cfg.brightMax = 80;
    }
    // Checkbox: if not present, it means unchecked
    cfg.brightBlanking = server.hasArg("brightBlank");
    
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
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);min-height:100vh}
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
