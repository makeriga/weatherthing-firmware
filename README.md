# WeatherThing Firmware

ESP32-C3 firmware for the WeatherThing LED matrix display.

## Features

### Display Cards

| Card | Description | Presets |
|------|-------------|---------|
| **Weather** | Current conditions, forecast timeline | 35 presets including Classic, Animated, Terminal, Cyber, Radar/Grid Maps, Heat Map, Compass, Gauge, Starfield, Seasons |
| **Clock** | Time display with effects | 30 presets: Digital, Binary, Minimal, Bars, Nixie, Glitch, Pong, Word, Bounce, Matrix, Radar, Flip, Cyber, Analog, Countdown, DotMatrix, Gradient, Segment, Orbit, Tally, Cutout, Scan, Duo, Frame, Date, FullDate, Weekday, Nameday, WeekNum, Poland |
| **Bitcoin** | Live BTC price with trend since last update | CoinGecko (BTC/USD) - *disabled by default* |
| **Stocks** | Stock ticker with price tracking | Yahoo Finance symbol (no API key) - *disabled by default* |
| **Network** | WiFi status, IP address display | - |
| **Audio VU** | Microphone-reactive visualizer | 39 presets including Spectrum, Fire, Plasma, Matrix, Disco, Fireworks, Nyan, Ocean, Aurora, Lightning, Ripple, DNA, Kaleidoscope, Snake |
| **Sparkle** | Audio-reactive particle effects | Sound-reactive mode |
| **Aurora** | Flowing plasma animation | Sound-reactive mode |
| **Games** | Interactive games | Flappy Bird, Snake, Breakout, Pong - *disabled by default* |
| **MQTT** | Home Assistant notifications | - *disabled by default* |
| **RSS** | RSS feed ticker | - *disabled by default* |
| **YouTube** | Subscriber count display | Requires API key - *disabled by default* |
| **Countdown** | Countdown timer with touch controls | 4 presets: 1min, 5min, 15min, 30min |
| **Pomodoro** | Pomodoro productivity timer | 3 presets: 25/5, 50/10, 15/3 (work/break minutes) |
| **Sun** | Sun position arc with digital clock | 2 presets: Yellow, Orange |
| **Stopwatch** | Stopwatch with touch controls | Tap to start/pause, long touch to reset |

### Hardware

- **Display**: 20x7 RGB LED matrix (140 LEDs, WS2812 5050)
- **Timeline**: 12 RGB LED strip for forecast/status
- **Input**: 2 physical buttons + capacitive touch sensor
- **Audio**: Built-in analog microphone (ADC input)
- **Light sensor**: Ambient light for auto-brightness
- **MCU**: ESP32-C3 (WiFi, BLE capable)

### Power & Brightness

- **FET rating**: 2A continuous, 3A burst
- **Max theoretical draw**: ~5.5A (all white at 255) - don't do this
- **Defaults**: Auto-brightness, `brightMin=8`, `brightMax=80`
- **Safe cap (normal mode)**: Brightness up to 127
- **High Power Mode**: Unlocks 128-255 (requires checkbox, bare PCB only - causes heat!)
- **Auto-brightness**: Adjusts based on ambient light sensor
- **Manual mode**: Full user control within limit

## Flashing / Updating Firmware

Use the GitHub Pages web flasher (Chrome/Edge desktop with WebSerial) to install or update firmware over USB:

https://makeriga.github.io/weatherthing-firmware/

Notes:
- Requires HTTPS (GitHub Pages is fine)
- Connect the device via USB, click **Install**, and choose the correct serial port

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run -e esp32c3

# Upload
pio run -e esp32c3 --target upload

# Serial monitor
pio device monitor
```

## Configuration

1. First boot (or if WiFi connection fails) starts an access point `WEATHERTHING_XXXX` (password: `weatherthing`)
2. Connect and go to `http://192.168.1.4`
3. Enter WiFi credentials
4. Device restarts and connects to your network
5. Access web UI at `http://weatherthing.local` or the device IP

Factory reset (clear WiFi credentials):
1. Power off / unplug the device
2. Hold **Button 1 (KEY1 / GPIO6)**
3. Power on while still holding the button
4. Release after boot; saved WiFi credentials are cleared and the device will start the setup AP again

## Factory Test Mode

On first boot (or after erasing flash/NVS), the firmware enters **Factory Test Mode** instead of normal operation.

What it tests:
- **LEDs**: full-screen color fills (RGB/white) + rainbow sweep
- **Buttons**: prompts you to press **Button 1**, then **Button 2**
- **Cap touch**: prompts you to touch the capacitive sensor
- **Microphone**: shows a simple VU-style level indicator

Completion behavior:
- When the test finishes, it stores a flag in NVS and restarts into normal mode.
- The completion flag is stored under Preferences namespace `fttest`, key `done`.

Re-running the test:
- Hold **Button 2** during boot to force Factory Test Mode again.

## Controls

| Input | Action |
|-------|--------|
| **Button 1** | Next preset (Weather/Clock/Audio/Games mode), then next enabled card |
| **Button 2** | Previous preset, then previous enabled card |
| **Touch** | Game action; toggles mode on some cards (e.g. MQTT) |
| **Touch (Timer cards)** | Tap to start/pause; hold 1.2s to reset |

In the Games card: hold **Button 1 + Button 2** for ~1 second to exit.

### Timer Card Controls

| Card | Tap | Long Touch (1.2s) |
|------|-----|-------------------|
| **Countdown** | Start/Pause (restarts if at 00:00) | Reset to preset duration |
| **Pomodoro** | Start/Pause | Reset to Work phase |
| **Stopwatch** | Start/Pause | Reset to 00:00 |
| **Sun** | - | - |

## Audio Settings

The VU meter and sound-reactive modes use the built-in microphone:

| Setting | Range | Description |
|---------|-------|-------------|
| **Gain** | 1-10 | Main amplification (exponential: 1=2x, 5=32x, 10=500x) |
| **Boost** | 0-10 | Additional multiplier for quiet sources (0=1x, 10=100x) |
| **Noise Gate** | 0-255 | Threshold to filter background noise |
| **AGC** | On/Off | Automatic gain control - adapts to volume |
| **Silence Ms** | 0-2000 | Blank display after silence (0=disabled) |
| **Invert** | On/Off | Flip VU meter direction |

**Tips for best results:**
- Start with Gain=5, Boost=3, Noise Gate=50
- For quiet sources: increase Boost first, then Gain
- Disable AGC for consistent levels with line-in audio
- Lower Noise Gate in quiet environments

## MQTT / Home Assistant

### Setup
1. Configure MQTT broker in web UI (Settings → MQTT)
2. Device auto-registers via MQTT Discovery
3. Entities appear in Home Assistant

### Topics
```
weatherthing/{device_id}/notify  - Send notifications
weatherthing/{device_id}/cmd     - Commands (card, brightness)
weatherthing/{device_id}/data    - Sensor data display
weatherthing/{device_id}/state   - Device state (published)
weatherthing/{device_id}/available - LWT availability (online/offline)
weatherthing/{device_id}/button/1  - Button 1 events (pressed/released)
weatherthing/{device_id}/button/2  - Button 2 events (pressed/released)
```

### Notification Format
```json
{
  "title": "App Name",
  "text": "Message to display",
  "icon": 1,
  "color": "#FF0000"
}
```
**Icons:** 0=none, 1=bell, 2=home, 3=alert, 4=info, 5=check

Additional icons supported by the firmware: 6=temp, 7=humidity, 8=power, 9=mail

### Example
```yaml
service: mqtt.publish
data:
  topic: "weatherthing/wt_XXXX/notify"
  payload: '{"title":"Door","text":"Someone at door!","icon":1}'
```

---

## Custom Overlay HTTP API (pixels + text)

This firmware exposes an HTTP API that lets you draw **custom content on top of the normal cards**.

Typical uses:
- Build server status (success/fail)
- Alerts from a script
- Temporary messages during demos/streams

The overlay supports:
- **Matrix pixels** (20x7) with per-pixel RGB color
- **Timeline pixels** (12 LEDs) with per-pixel RGB color
- **Text strings** (digits + letters) with a single color or **per-character colors**

All overlay types support a **timeout** (TTL). When the timeout expires, the overlay is automatically removed.

### Coordinate system

- Matrix `x`: 0..19 (left to right)
- Matrix `y`: 0..6 (top to bottom)
- Timeline `i` / `index`: 0..11

### Color format

Colors are accepted as:
- Hex strings: `"#RRGGBB"`
- 24-bit integers: `0xRRGGBB`

### Timeout behavior

- `timeout_ms` is a **TTL**, relative to when the request is received.
- If you omit `timeout_ms` or set it to `0`, the default is **10 seconds**.
- To keep an overlay permanently visible, re-send the same request periodically (for example, every 5 seconds).

### Endpoints

#### GET `/api/overlay`

Returns whether any overlay content is active and how long remains.

Example:
```bash
curl http://weatherthing.local/api/overlay
```

#### POST `/api/overlay/clear`

Clears overlays.

Body (optional):
```json
{ "target": "all" }
```

Valid `target` values:
- `all`
- `matrix`
- `timeline`
- `text`

Example:
```bash
curl -X POST http://weatherthing.local/api/overlay/clear \
  -H "Content-Type: application/json" \
  -d '{"target":"all"}'
```

#### POST `/api/overlay/matrix`

Sets one or more **matrix pixels**.

Body:
```json
{
  "timeout_ms": 15000,
  "clear": true,
  "clear_under": false,
  "pixels": [
    {"x": 0, "y": 0, "color": "#FF0000"},
    {"x": 1, "y": 0, "color": "#00FF00"},
    {"x": 2, "y": 0, "color": "#0000FF"}
  ]
}
```

Notes:
- If `clear` is `true`, the previous matrix overlay pixels are cleared first.
- If `clear_under` is `true`, the **normal card rendering is blanked** while the overlay is active (so your overlay has full control).

Example:
```bash
curl -X POST http://weatherthing.local/api/overlay/matrix \
  -H "Content-Type: application/json" \
  -d '{"timeout_ms":10000,"clear":true,"pixels":[{"x":10,"y":3,"color":"#FF00FF"}]}'
```

#### POST `/api/overlay/timeline`

Sets the **timeline LEDs**.

Option A: send the full bar as a color array (`colors[0]` goes to timeline LED 0):
```json
{
  "timeout_ms": 20000,
  "clear": true,
  "colors": [
    "#00FF00", "#00FF00", "#00FF00", "#00FF00",
    "#00FF00", "#00FF00", "#00FF00", "#00FF00",
    "#00FF00", "#00FF00", "#00FF00", "#00FF00"
  ]
}
```

Option B: set specific LEDs:
```json
{
  "timeout_ms": 10000,
  "pixels": [
    {"i": 0, "color": "#00FF00"},
    {"i": 1, "color": "#FFFF00"},
    {"i": 2, "color": "#FF0000"}
  ]
}
```

Notes:
- `clear_under` behaves the same as on the matrix: it blanks the normal timeline rendering while the timeline overlay is active.

#### POST `/api/overlay/text`

Draws a text string on the matrix.

Body:
```json
{
  "text": "BUILD OK",
  "x": 0,
  "y": 0,
  "color": "#00FF00",
  "timeout_ms": 15000,
  "scroll": false,
  "scroll_speed_ms": 60,
  "clear_under": true
}
```

Per-character colors (optional):
```json
{
  "text": "RGB",
  "x": 0,
  "y": 0,
  "color": "#FFFFFF",
  "colors": ["#FF0000", "#00FF00", "#0000FF"],
  "timeout_ms": 8000
}
```

If you have trouble sending JSON (some clients/shells), `/api/overlay/text` also accepts **query/form parameters** as a fallback:

- `text` (or `msg`)
- `x`, `y`
- `color` (hex like `#00FF00`)
- `timeout_ms`
- `scroll` (0/1)
- `scroll_speed_ms`
- `clear_under` (0/1)

Supported characters:
- `0-9`
- `A-Z` (lowercase is accepted too)
- `:` `.` `-` and space

Windows PowerShell examples (use `curl.exe`):

Clear all overlays:
```powershell
curl.exe -X POST http://weatherthing.local/api/overlay/clear `
  -H "Content-Type: application/json" `
  -d '{"target":"all"}'
```

Write a static message (top-left) for 10 seconds:
```powershell
$body = @{
  text = "HELLO WORLD"
  x = 0
  y = 0
  color = "#00FF00"
  timeout_ms = 10000
  scroll = $false
  clear_under = $true
} | ConvertTo-Json -Compress

curl.exe --noproxy "*" -X POST http://weatherthing.local/api/overlay/text `
  -H "Content-Type: application/json" `
  --data-binary $body
```

Scroll `HELLO WORLD` across the display for 15 seconds:
```powershell
$body = @{
  text = "HELLO WORLD"
  y = 0
  color = "#00FF00"
  timeout_ms = 15000
  scroll = $true
  scroll_speed_ms = 60
  clear_under = $true
} | ConvertTo-Json -Compress

curl.exe --noproxy "*" -X POST http://weatherthing.local/api/overlay/text `
  -H "Content-Type: application/json" `
  --data-binary $body
```

If JSON still fails on your setup, use the query-parameter fallback (note the URL must be quoted in PowerShell, and `#` must be encoded as `%23`):
```powershell
curl.exe --noproxy "*" -X POST "http://192.168.88.101/api/overlay/text?text=HELLO%20WORLD&x=0&y=0&color=%2300FF00&timeout_ms=10000&scroll=0&clear_under=1"
```

---

## Developer Guide: Adding Custom Cards

### Card Architecture

Each card is a struct with three function pointers:
```cpp
struct Card {
    void (*setup)();                      // Called once when card activates
    void (*update)(uint32_t now, uint32_t dt);  // Called every frame (logic)
    void (*render)();                     // Called every frame (drawing)
};
```

### Step 1: Declare Functions

In `src/cards.cpp`, add forward declarations near the top:

```cpp
static void mycard_setup();
static void mycard_update(uint32_t now, uint32_t dt);
static void mycard_render();
```

### Step 2: Register Card

Add to the `g_cards[]` array:

```cpp
static Card g_cards[] = {
    // ... existing cards ...
    {mycard_setup, mycard_update, mycard_render},  // Your new card
};
```

Add a card index constant:

```cpp
static const uint8_t CARD_MYCARD = 16;  // Next available index
```

Add card name to `g_cardNames[]`:

```cpp
static const char* g_cardNames[] = {
    // ... existing names ...
    "MyCard"
};
```

### Step 3: Implement Functions

```cpp
static void mycard_setup() {
    // Initialize card state, fetch data, etc.
}

static void mycard_update(uint32_t now, uint32_t dt) {
    // Update logic: animations, data fetching, input handling
    // now = millis(), dt = time since last frame in ms
}

static void mycard_render() {
    wt_display_clear();
    
    // Draw to the 20x7 matrix
    wt_display_set_pixel_xy(10, 3, wt_color(255, 0, 0));  // Red pixel at center
    
    // Draw to the 12-LED timeline
    wt_timeline_set_pixel(0, wt_color(0, 255, 0));  // Green on first timeline LED
}
```

### Step 4: Add Web UI Button

In `src/net.cpp`, find the card configuration section and add a preset button:

```cpp
// In the appropriate card case in handleCardsConfig()
presetBtn(CARD_MYCARD, 0, "MyPreset");
```

### Display API Reference

```cpp
// Matrix (20x7)
wt_display_clear();                           // Clear display
wt_display_set_pixel_xy(x, y, color);         // Set pixel (x:0-19, y:0-6)
wt_display_fill(color);                       // Fill entire display

// Timeline (12 LEDs)
wt_timeline_clear();                          // Clear timeline
wt_timeline_set_pixel(i, color);              // Set LED (i:0-11)

// Colors
wt_color(r, g, b);                            // RGB color (0-255 each)
wt_color_hsv(h, s, v);                        // HSV color (0-255 each)

// Input
wt_button1_pressed();                         // Button 1 just pressed
wt_button2_pressed();                         // Button 2 just pressed
wt_cap_touch_active();                        // Touch sensor active

// Audio
wt_mic_read_raw();                            // Raw ADC value (0-4095)

// Settings
Settings& cfg = settings_get();               // Access settings struct
```

### Adding Presets to Existing Cards

For cards with multiple presets (like Weather), add a preset variable and switch in render:

```cpp
static uint8_t g_myPreset = 0;
#define MY_PRESET_COUNT 3

static void mycard_render() {
    switch(g_myPreset) {
        case 0: render_style_a(); break;
        case 1: render_style_b(); break;
        case 2: render_style_c(); break;
    }
}
```

Handle preset switching in update:

```cpp
static void mycard_update(uint32_t now, uint32_t dt) {
    if (wt_button1_pressed()) {
        g_myPreset = (g_myPreset + 1) % MY_PRESET_COUNT;
    }
}
```

---

## API Sources

| Data | Provider | Auth |
|------|----------|------|
| Weather | [Open-Meteo](https://open-meteo.com/) | None (free) |
| Weather (fallback) | [MET Norway](https://api.met.no/) | None (free) |
| Crypto | [CoinGecko](https://www.coingecko.com/) | None (free) |
| Stocks | [Yahoo Finance](https://finance.yahoo.com/) | None (public endpoint) |
| Radar map | [RainViewer](https://www.rainviewer.com/api.html) | None (free) |

## File Structure

```
├── src/
│   ├── main.cpp          # Entry point, loop
│   ├── cards.cpp         # All card implementations
│   ├── weather.cpp       # Weather data fetching
│   ├── net.cpp           # Web server, WiFi, API
│   ├── settings.cpp      # Persistent settings
│   ├── mqtt.cpp          # MQTT/Home Assistant
│   ├── sprites.cpp       # Custom sprite storage (LittleFS)
│   └── weatherthing_hw.cpp  # Hardware abstraction
├── include/
│   ├── cards.h           # Card system interface
│   ├── mqtt.h            # MQTT interface
│   ├── net.h             # Networking interface
│   ├── weather.h         # Weather types/functions
│   ├── settings.h        # Settings struct
│   ├── sprites.h         # Sprite API
│   └── weatherthing_hw.h # Hardware interface
└── platformio.ini        # Build configuration
```

## License

MIT License - See LICENSE file

## Links

- [WeatherThing.com](https://weatherthing.com)
- [Firmware Web Flasher](https://makeriga.github.io/weatherthing-firmware/)
- [Makeriga](https://github.com/makeriga)

