# WeatherThing Firmware

ESP32-C3 firmware for the WeatherThing LED matrix display.

## Features

### Display Cards

| Card | Description | Presets |
|------|-------------|---------|
| **Weather** | Current conditions, forecast timeline | 35 presets including Classic, Animated, Terminal, Cyber, Radar/Grid Maps, Heat Map, Compass, Gauge, Starfield, Seasons |
| **Clock** | Time display with effects | 19 presets: Digital, Binary, Minimal, Bars, Nixie, Glitch, Pong, Word, Bounce, Matrix, Radar, Flip, Cyber, Analog, Countdown, DotMatrix, Gradient, Segment, Orbit |
| **Bitcoin** | Live BTC/crypto price with 24h trend | Supports any CoinGecko crypto symbol |
| **Stocks** | Stock ticker with price tracking | Any Finnhub stock symbol |
| **Network** | WiFi status, IP address display | - |
| **Audio VU** | Microphone-reactive visualizer | 34 presets including Spectrum, Fire, Plasma, Matrix, Disco, Fireworks, Nyan, Ocean, Aurora, Lightning, Ripple, DNA, Kaleidoscope, Snake |
| **Sparkle** | Audio-reactive particle effects | Sound-reactive mode |
| **Aurora** | Flowing plasma animation | Sound-reactive mode |
| **Games** | Interactive games | Flappy Bird, Snake, Breakout, Pong |
| **MQTT** | Home Assistant notifications | - |
| **RSS** | RSS feed ticker | - |
| **YouTube** | Subscriber count display | Requires API key |
| **Twitch** | Live status display | - |
| **Twitter/X** | Follower count | Requires API key |
| **Instagram** | Follower count | Requires API key |
| **TikTok** | Follower count | - |

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
- **Default limit**: Brightness 1-127 (safe for enclosed use)
- **High Power Mode**: Unlocks 128-255 (requires checkbox, bare PCB only - causes heat!)
- **Auto-brightness**: Adjusts based on ambient light sensor
- **Manual mode**: Full user control within limit

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Upload
pio run --target upload

# Serial monitor
pio device monitor
```

## Configuration

1. First boot creates WiFi hotspot `WeatherThing-XXXX`
2. Connect and go to `http://192.168.4.1`
3. Enter WiFi credentials
4. Device restarts and connects to your network
5. Access web UI at `http://weatherthing.local` or device IP

## Controls

| Input | Action |
|-------|--------|
| **Button 1** | Next card / Next preset (hold) |
| **Button 2** | Previous card / Previous preset (hold) |
| **Touch** | Game action / Confirm |

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

### Example
```yaml
service: mqtt.publish
data:
  topic: "weatherthing/wt_XXXX/notify"
  payload: '{"title":"Door","text":"Someone at door!","icon":1}'
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
| Stocks | [Finnhub](https://finnhub.io/) | API key |
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
│   └── weatherthing_hw.cpp  # Hardware abstraction
├── include/
│   ├── cards.h           # Card system interface
│   ├── weather.h         # Weather types/functions
│   ├── settings.h        # Settings struct
│   └── weatherthing_hw.h # Hardware interface
└── platformio.ini        # Build configuration
```

## License

MIT License - See LICENSE file

## Links

- [WeatherThing.com](https://weatherthing.com)
- [Makeriga](https://github.com/makeriga)
