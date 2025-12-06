# WeatherThing Firmware v0.1

ESP32-C3 firmware for the WeatherThing LED matrix display.

## Features

### Display Cards
- **Weather** - Current conditions with animated icons and 12-hour forecast timeline
  - 5 display styles: Classic, Big Temp, Corner, Animated, Minimal
- **Clock** - Digital time display with gradient colors
- **Bitcoin** - Live BTC price with 24h trend on timeline
- **Stock Ticker** - Custom stock symbol tracking
- **Audio Visualizer** - Microphone-reactive VU meter with 12 effect styles
- **Sparkle** - Audio-reactive particle effects
- **Aurora** - Flowing plasma animation
- **Games** - Flappy Bird, Snake, Breakout, Pong
- **MQTT** - Home Assistant integration and custom notifications

### Hardware
- 20x7 RGB LED matrix (main display)
- 12 RGB LED timeline strip (forecast/status)
- 2 physical buttons + capacitive touch
- Built-in microphone for audio effects

### Web Interface
- WiFi configuration
- Location settings (city search or coordinates)
- Weather simulation for testing
- Display control and card switching
- Sprite editor for custom icons
- Audio/visualizer settings

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

## Configuration

1. On first boot, device creates WiFi hotspot "WeatherThing-XXXX"
2. Connect and navigate to http://192.168.4.1
3. Enter your WiFi credentials
4. Device will restart and connect to your network
5. Access web UI at http://weatherthing.local or device IP

## Controls

- **Button 1** - Next card/style
- **Button 2** - Previous card/style
- **Touch** - Game action / confirm

## MQTT / Home Assistant

WeatherThing supports MQTT for Home Assistant integration and custom notifications.

### Setup
1. Configure MQTT broker in the web UI (Settings → MQTT / Home Assistant)
2. Device auto-registers with Home Assistant via MQTT Discovery
3. Entities appear automatically in Home Assistant

### MQTT Topics
- `weatherthing/{device_id}/notify` - Send notifications (JSON or plain text)
- `weatherthing/{device_id}/cmd` - Send commands (card switch, brightness)
- `weatherthing/{device_id}/data` - Send sensor data to display
- `weatherthing/{device_id}/state` - Device state (published by device)

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

### Home Assistant Example
```yaml
# Send notification to WeatherThing
service: mqtt.publish
data:
  topic: "weatherthing/wt_XXXX/notify"
  payload: '{"title":"Doorbell","text":"Someone at the door!","icon":1,"color":"#00FF00"}'
```

## API

Weather data from [Open-Meteo](https://open-meteo.com/) (free, no API key required)
Crypto prices from [CoinGecko](https://www.coingecko.com/)
Stock data from [Finnhub](https://finnhub.io/)

## License

MIT License - See LICENSE file

## Links

- [WeatherThing.com](https://weatherthing.com)
- [Makeriga](https://github.com/makeriga)
