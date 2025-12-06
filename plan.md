## WeatherThing Firmware Plan

### 0. Current status
- **Hardware bring-up** 
  - 7x20 WS2812B matrix: working, correct serpentine column-major order.
  - 12-LED timeline bar: working, left-to-right.
  - **Inputs**: both buttons work (GPIO2, GPIO6), cap touch, light sensor, mic ADC all working.
- **Platform** 
  - PlatformIO + Arduino framework on ESP32-C3.
  - USB CDC serial enabled for debug.
- **Cards implemented**
  - Diagnostics card (LED test + input logging)
  - RGB test card (color channel verification)
  - Clock card (NTP time, WiFi error icon)
  - Weather card (Open-Meteo API, 7x7 icons, 12h forecast)
  - Ticker card (Bitcoin price from CoinGecko, scrolling text)
  - VU meter card (mic-reactive bars + peak markers)

---

### 1. Core hardware abstraction
- **Pin mapping**
  - Confirm and document both buttons (e.g. KEY1, KEY2) and their GPIOs.
  - Keep GPIO8/9 I2C and GPIO2/10 header usage in mind (strap pins etc.).
- **Hardware API layer** (`weatherthing_hw`)
  - Matrix + timeline: clear/fill/pixel-by-XY helpers, shared `show()`.
  - Buttons: two debounced buttons with pressed/held events.
  - Capacitive touch: digital input with simple filtering.
  - Mic: raw ADC read + simple moving-average/smoothing.
  - Light sensor: ADC read + normalization to 0–1 brightness level.
- **Diagnostics mode**
  - Simple pattern + live input logging over serial for production testing.

Outcome: stable, well-documented HW layer that hides Neopixel, ADC details and exposes simple helpers.

---

### 2. Display and card framework
- **Rendering primitives**
  - 2D coordinate API for matrix (0,0 = left-bottom) + color helpers.
  - Optional lightweight backbuffer for flicker-free updates.
  - Timeline helpers: set segment ranges, gradients, progress bars.
- **Brightness + current limiting**
  - Map ambient light reading -> global brightness curve.
  - Simple current guard: cap max brightness based on LED count and 2A budget.
- **Card (state) system**
  - Define a `Card` interface (e.g. `setup()`, `update(dt)`, `render()`).
  - Global state machine to switch between cards.
  - Navigation: two buttons (next/prev) + optional cap-touch action.

Outcome: easy-to-implement cards that only care about drawing and input, not low-level hardware.

---

### 3. Input/events & factory reset
- **Button/gesture handling**
  - Debounce + classify short/long/very-long presses.
  - Map default roles: next card, previous card, special actions.
- **Capacitive touch**
  - Treat as another button (e.g. play/pause, favorite card, or context action).
- **Factory reset behavior**
  - On boot, if a specific button is held, clear config (WiFi, card settings).
  - Restore default AP SSID/password and default card set/order.

Outcome: robust, user-friendly physical interaction model with clear reset path.

---

### 4. WiFi, AP, and configuration portal
- **WiFi state machine**
  - If no saved WiFi: start AP with SSID `WEATHERTHING_xxxx` (from MAC) and default password.
  - If saved WiFi: try client mode; on repeated failure, fall back to AP.
- **Networking details**
  - AP mode: static IP `192.168.1.4` and mDNS `weatherthing.local`.
  - Client mode: mDNS `weatherthing.local` on LAN.
- **Config storage**
  - Use SPIFFS/LittleFS for persistent settings (WiFi, location, per-card config).
- **Web UI skeleton**
  - Landing page with device summary and links to:
    - WiFi setup (scan networks, enter password).
    - Location/timezone settings.
    - Global settings (brightness curve, mic gain, button roles).
    - Per-card settings.

Outcome: device is self-contained; first-time setup via captive AP-style experience.

---

### 5. Weather card and timeline forecast 
- **Weather data source** 
  - Using Open-Meteo API (free, no key required).
  - Fetches current conditions + 12-hour hourly forecast.
- **Mapping forecast to visuals** 
  - 7x7 icons: sun, cloud, rain, storm.
  - Left side: weather icon with condition color.
  - Right side: temperature in digits.
  - Timeline bar: 12-hour forecast, color-coded by condition.
- **Configuration** 
  - Web UI: latitude/longitude input with persistence.
  - Default location: London (51.5074, -0.1278).

Outcome: flagship `weather` card demonstrating full matrix + timeline usage.

---

### 6. Additional cards
- **Clock / date card** 
  - NTP sync in client mode.
  - Shows HH:MM with WiFi error icon when disconnected.
  - Timeline shows seconds progress.
- **Stock / crypto ticker card** 
  - CoinGecko API for Bitcoin price.
  - Static display: Bitcoin icon + price in K (e.g., 97.5K).
  - Fetches every 5 minutes.
- **Sparkle/particle card** 
  - Rainbow particle animations.
  - 3 modes: random sparkle, wave, rain.
  - Cap touch to cycle modes.
- **Text/notification card** (TODO)
  - Static or scrolling custom text from config.

Outcome: a small library of showcase cards that stress-test the rendering API.

---

### 7. Mic-driven visuals and secret knock
- **Mic/VU layer** 
  - 128-sample burst with DC offset removal.
  - AGC (automatic gain control) adapts to volume.
  - Strong noise gate with hysteresis (10 frames).
  - Peak markers with 20-frame hold.
- **Visualizations** 
  - Classic green/yellow/red vertical bars.
  - Simple low/high frequency separation (bass left, treble right).
  - Timeline: left-to-right level meter.
- **Secret knock / pattern detection** (TODO)
  - Detect sequences of loud taps / knocks on the frame.
  - Map a recognized pattern to a configurable action.

Outcome: fun audio-reactive modes and an unobtrusive `hidden control` channel.

---

### 8. Settings UX and polishing
- **Web UI improvements**
  - Better layout, grouping of settings, live previews where possible.
  - Input validation and helpful error messages.
- **On-device feedback**
  - Use matrix to provide feedback for WiFi connection attempts, errors, and factory reset.
  - Optional simple OSD-style overlays for status.
- **Performance and robustness**
  - Watchdog-friendly main loop, non-blocking animations.
  - Graceful handling of API/network failures.

Outcome: production-ready feel, resilient behavior, and understandable feedback.

---

### 9. Nice-to-haves / future work
- OTA firmware updates from web UI.
- Integration with additional public APIs (air quality, calendars, etc.).
- Simple plugin-like structure for adding new cards.