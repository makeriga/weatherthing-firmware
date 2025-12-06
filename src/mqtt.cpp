#include "mqtt.h"
#include "settings.h"
#include "cards.h"
#include "weatherthing_hw.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// MQTT client
static WiFiClient g_wifiClient;
static PubSubClient g_mqtt(g_wifiClient);

// Connection state
static bool g_mqttEnabled = false;
static uint32_t g_lastConnectAttempt = 0;
static uint32_t g_lastHaDiscovery = 0;
static bool g_haDiscoverySent = false;

// Current message for display
static char g_msgTitle[32] = "";
static char g_msgText[64] = "";
static uint32_t g_msgColor = 0xFFFFFF;
static uint8_t g_msgIcon = MQTT_ICON_NONE;
static uint32_t g_msgTimestamp = 0;
static bool g_hasNewMessage = false;

// Device ID for Home Assistant
static char g_deviceId[16] = "";

// Topic buffers
static char g_topicState[64] = "";
static char g_topicCmd[64] = "";
static char g_topicNotify[64] = "";
static char g_topicData[64] = "";

// Forward declarations
static void mqtt_callback(char* topic, byte* payload, unsigned int length);
static void mqtt_send_ha_discovery();
static void mqtt_publish_availability(bool online);

void mqtt_begin()
{
    Settings& cfg = settings_get();
    
    // Check if MQTT is configured
    if (cfg.mqttServer[0] == '\0') {
        g_mqttEnabled = false;
        Serial.println("MQTT: Not configured");
        return;
    }
    
    g_mqttEnabled = true;
    
    // Generate device ID from MAC
    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_deviceId, sizeof(g_deviceId), "wt_%04X", (unsigned int)(mac & 0xFFFF));
    
    // Build topic names
    snprintf(g_topicState, sizeof(g_topicState), "weatherthing/%s/state", g_deviceId);
    snprintf(g_topicCmd, sizeof(g_topicCmd), "weatherthing/%s/cmd", g_deviceId);
    snprintf(g_topicNotify, sizeof(g_topicNotify), "weatherthing/%s/notify", g_deviceId);
    snprintf(g_topicData, sizeof(g_topicData), "weatherthing/%s/data", g_deviceId);
    
    // Configure MQTT client
    g_mqtt.setServer(cfg.mqttServer, cfg.mqttPort);
    g_mqtt.setCallback(mqtt_callback);
    g_mqtt.setBufferSize(512);
    
    Serial.printf("MQTT: Configured for %s:%d\n", cfg.mqttServer, cfg.mqttPort);
    Serial.printf("MQTT: Device ID: %s\n", g_deviceId);
}

static bool mqtt_connect()
{
    Settings& cfg = settings_get();
    
    if (!g_mqttEnabled || WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    Serial.print("MQTT: Connecting to ");
    Serial.println(cfg.mqttServer);
    
    // Build client ID
    char clientId[24];
    snprintf(clientId, sizeof(clientId), "WeatherThing-%s", g_deviceId);
    
    // LWT (Last Will and Testament) for availability
    char availTopic[64];
    snprintf(availTopic, sizeof(availTopic), "weatherthing/%s/available", g_deviceId);
    
    bool connected = false;
    if (cfg.mqttUser[0] != '\0') {
        connected = g_mqtt.connect(clientId, cfg.mqttUser, cfg.mqttPass, 
                                   availTopic, 0, true, "offline");
    } else {
        connected = g_mqtt.connect(clientId, availTopic, 0, true, "offline");
    }
    
    if (connected) {
        Serial.println("MQTT: Connected!");
        
        // Subscribe to command and notification topics
        g_mqtt.subscribe(g_topicCmd);
        g_mqtt.subscribe(g_topicNotify);
        g_mqtt.subscribe(g_topicData);
        
        // Also subscribe to custom topic if configured
        if (cfg.mqttTopic[0] != '\0') {
            g_mqtt.subscribe(cfg.mqttTopic);
            Serial.printf("MQTT: Subscribed to custom topic: %s\n", cfg.mqttTopic);
        }
        
        // Publish online status
        mqtt_publish_availability(true);
        
        // Send HA discovery (will be done in loop after short delay)
        g_haDiscoverySent = false;
        
        return true;
    } else {
        Serial.printf("MQTT: Connection failed, rc=%d\n", g_mqtt.state());
        return false;
    }
}

void mqtt_loop()
{
    if (!g_mqttEnabled) {
        return;
    }
    
    uint32_t now = millis();
    
    if (!g_mqtt.connected()) {
        // Reconnect every 10 seconds
        if (now - g_lastConnectAttempt > 10000) {
            g_lastConnectAttempt = now;
            mqtt_connect();
        }
        return;
    }
    
    g_mqtt.loop();
    
    // Send HA discovery after connection stabilizes
    if (!g_haDiscoverySent && now - g_lastConnectAttempt > 2000) {
        mqtt_send_ha_discovery();
        g_haDiscoverySent = true;
    }
}

bool mqtt_is_connected()
{
    return g_mqttEnabled && g_mqtt.connected();
}

const char* mqtt_get_message()
{
    return g_msgText;
}

const char* mqtt_get_title()
{
    return g_msgTitle;
}

uint32_t mqtt_get_color()
{
    return g_msgColor;
}

uint8_t mqtt_get_icon()
{
    return g_msgIcon;
}

uint32_t mqtt_get_message_age_ms()
{
    if (g_msgTimestamp == 0) return UINT32_MAX;
    return millis() - g_msgTimestamp;
}

bool mqtt_has_new_message()
{
    bool result = g_hasNewMessage;
    g_hasNewMessage = false;
    return result;
}

void mqtt_clear_message()
{
    g_msgTitle[0] = '\0';
    g_msgText[0] = '\0';
    g_msgTimestamp = 0;
    g_msgIcon = MQTT_ICON_NONE;
}

// Parse incoming MQTT messages
static void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    // Null-terminate payload
    char msg[256];
    size_t len = min((size_t)length, sizeof(msg) - 1);
    memcpy(msg, payload, len);
    msg[len] = '\0';
    
    Serial.printf("MQTT: Received [%s]: %s\n", topic, msg);
    
    Settings& cfg = settings_get();
    
    // Check if it's a notification message
    if (strcmp(topic, g_topicNotify) == 0 || 
        (cfg.mqttTopic[0] != '\0' && strcmp(topic, cfg.mqttTopic) == 0)) {
        
        // Try to parse as JSON first
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msg);
        
        if (!err) {
            // JSON format: {"title":"App","text":"Message","icon":1,"color":"#FF0000"}
            const char* title = doc["title"] | "";
            const char* text = doc["text"] | doc["message"] | msg;
            
            strncpy(g_msgTitle, title, sizeof(g_msgTitle) - 1);
            g_msgTitle[sizeof(g_msgTitle) - 1] = '\0';
            
            strncpy(g_msgText, text, sizeof(g_msgText) - 1);
            g_msgText[sizeof(g_msgText) - 1] = '\0';
            
            // Parse icon
            g_msgIcon = doc["icon"] | MQTT_ICON_BELL;
            
            // Parse color (hex string or number)
            if (doc["color"].is<const char*>()) {
                const char* colorStr = doc["color"];
                if (colorStr[0] == '#') colorStr++;
                g_msgColor = strtoul(colorStr, NULL, 16);
            } else if (doc["color"].is<uint32_t>()) {
                g_msgColor = doc["color"];
            } else {
                g_msgColor = 0x00AAFF;  // Default cyan
            }
        } else {
            // Plain text message
            strncpy(g_msgTitle, "Message", sizeof(g_msgTitle) - 1);
            strncpy(g_msgText, msg, sizeof(g_msgText) - 1);
            g_msgText[sizeof(g_msgText) - 1] = '\0';
            g_msgIcon = MQTT_ICON_BELL;
            g_msgColor = 0x00AAFF;
        }
        
        g_msgTimestamp = millis();
        g_hasNewMessage = true;
        
        // Switch to MQTT card when notification arrives
        // cards_switch_to(9);  // Uncomment if you want auto-switch
    }
    
    // Check if it's a command
    if (strcmp(topic, g_topicCmd) == 0) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msg);
        
        if (!err) {
            // Handle card switch: {"card": 0}
            if (doc["card"].is<uint8_t>()) {
                uint8_t card = doc["card"];
                cards_switch_to(card);
            }
            
            // Handle preset: {"preset": 0}
            if (doc["preset"].is<uint8_t>()) {
                uint8_t preset = doc["preset"];
                cards_set_preset(preset);
            }
            
            // Handle brightness: {"brightness": 50}
            if (doc["brightness"].is<uint8_t>()) {
                Settings& cfg = settings_get();
                cfg.brightManual = doc["brightness"];
                cfg.brightMode = 1;  // Switch to manual
                settings_save();
            }
        }
    }
    
    // Data topic for generic sensor display
    if (strcmp(topic, g_topicData) == 0) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msg);
        
        if (!err) {
            // Format: {"value":"23.5°C","label":"Temp","icon":6}
            const char* value = doc["value"] | "";
            const char* label = doc["label"] | "";
            
            strncpy(g_msgTitle, label, sizeof(g_msgTitle) - 1);
            g_msgTitle[sizeof(g_msgTitle) - 1] = '\0';
            
            strncpy(g_msgText, value, sizeof(g_msgText) - 1);
            g_msgText[sizeof(g_msgText) - 1] = '\0';
            
            g_msgIcon = doc["icon"] | MQTT_ICON_INFO;
            
            if (doc["color"].is<const char*>()) {
                const char* colorStr = doc["color"];
                if (colorStr[0] == '#') colorStr++;
                g_msgColor = strtoul(colorStr, NULL, 16);
            } else {
                g_msgColor = 0x00FF88;  // Default green
            }
            
            g_msgTimestamp = millis();
            g_hasNewMessage = true;
        }
    }
}

void mqtt_publish_state(const char* card_name, uint8_t brightness)
{
    if (!mqtt_is_connected()) return;
    
    JsonDocument doc;
    doc["card"] = card_name;
    doc["brightness"] = brightness;
    doc["uptime"] = millis() / 1000;
    doc["rssi"] = WiFi.RSSI();
    
    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));
    
    g_mqtt.publish(g_topicState, buffer, true);
}

void mqtt_publish_button(uint8_t button_id, bool pressed)
{
    if (!mqtt_is_connected()) return;
    
    char topic[64];
    snprintf(topic, sizeof(topic), "weatherthing/%s/button/%d", g_deviceId, button_id);
    
    g_mqtt.publish(topic, pressed ? "pressed" : "released");
}

static void mqtt_publish_availability(bool online)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "weatherthing/%s/available", g_deviceId);
    g_mqtt.publish(topic, online ? "online" : "offline", true);
}

// Send Home Assistant MQTT Discovery messages
static void mqtt_send_ha_discovery()
{
    Serial.println("MQTT: Sending Home Assistant discovery...");
    
    char topic[128];
    char payload[512];
    JsonDocument doc;
    
    // Device info (shared by all entities)
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = g_deviceId;
    device["name"] = "WeatherThing";
    device["model"] = "WeatherThing LED Matrix";
    device["manufacturer"] = "Makeriga";
    device["sw_version"] = "0.1";
    
    // Availability
    char availTopic[64];
    snprintf(availTopic, sizeof(availTopic), "weatherthing/%s/available", g_deviceId);
    doc["availability_topic"] = availTopic;
    
    // 1. Light entity (for brightness control)
    doc.clear();
    device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = g_deviceId;
    device["name"] = "WeatherThing";
    device["model"] = "WeatherThing LED Matrix";
    device["manufacturer"] = "Makeriga";
    
    doc["name"] = "Display";
    doc["unique_id"] = String(g_deviceId) + "_light";
    doc["availability_topic"] = availTopic;
    doc["state_topic"] = g_topicState;
    doc["command_topic"] = g_topicCmd;
    doc["brightness"] = true;
    doc["brightness_scale"] = 80;
    doc["schema"] = "json";
    doc["icon"] = "mdi:led-strip-variant";
    
    snprintf(topic, sizeof(topic), "homeassistant/light/%s/config", g_deviceId);
    serializeJson(doc, payload, sizeof(payload));
    g_mqtt.publish(topic, payload, true);
    
    // 2. Select entity (for card selection)
    doc.clear();
    device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = g_deviceId;
    device["name"] = "WeatherThing";
    
    doc["name"] = "Display Card";
    doc["unique_id"] = String(g_deviceId) + "_card";
    doc["availability_topic"] = availTopic;
    doc["state_topic"] = g_topicState;
    doc["command_topic"] = g_topicCmd;
    doc["value_template"] = "{{ value_json.card }}";
    doc["command_template"] = "{\"card\": \"{{ value }}\"}";
    doc["icon"] = "mdi:card-multiple-outline";
    
    JsonArray options = doc["options"].to<JsonArray>();
    options.add("Weather");
    options.add("Clock");
    options.add("Bitcoin");
    options.add("Stocks");
    options.add("Network");
    options.add("Audio");
    options.add("Sparkle");
    options.add("Aurora");
    options.add("Games");
    options.add("MQTT");
    
    snprintf(topic, sizeof(topic), "homeassistant/select/%s_card/config", g_deviceId);
    serializeJson(doc, payload, sizeof(payload));
    g_mqtt.publish(topic, payload, true);
    
    // 3. Sensor for RSSI
    doc.clear();
    device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = g_deviceId;
    device["name"] = "WeatherThing";
    
    doc["name"] = "Signal Strength";
    doc["unique_id"] = String(g_deviceId) + "_rssi";
    doc["availability_topic"] = availTopic;
    doc["state_topic"] = g_topicState;
    doc["value_template"] = "{{ value_json.rssi }}";
    doc["unit_of_measurement"] = "dBm";
    doc["device_class"] = "signal_strength";
    doc["state_class"] = "measurement";
    doc["icon"] = "mdi:wifi";
    
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_rssi/config", g_deviceId);
    serializeJson(doc, payload, sizeof(payload));
    g_mqtt.publish(topic, payload, true);
    
    // 4. Notify service (for sending messages to display)
    doc.clear();
    device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = g_deviceId;
    device["name"] = "WeatherThing";
    
    doc["name"] = "Notification";
    doc["unique_id"] = String(g_deviceId) + "_notify";
    doc["availability_topic"] = availTopic;
    doc["command_topic"] = g_topicNotify;
    doc["icon"] = "mdi:message-alert";
    
    snprintf(topic, sizeof(topic), "homeassistant/text/%s_notify/config", g_deviceId);
    serializeJson(doc, payload, sizeof(payload));
    g_mqtt.publish(topic, payload, true);
    
    // 5. Button events (as device triggers)
    for (int btn = 1; btn <= 2; btn++) {
        doc.clear();
        device = doc["device"].to<JsonObject>();
        device["identifiers"][0] = g_deviceId;
        device["name"] = "WeatherThing";
        
        doc["automation_type"] = "trigger";
        doc["type"] = "button_short_press";
        doc["subtype"] = String("button_") + btn;
        
        char btnTopic[64];
        snprintf(btnTopic, sizeof(btnTopic), "weatherthing/%s/button/%d", g_deviceId, btn);
        doc["topic"] = btnTopic;
        doc["payload"] = "pressed";
        
        snprintf(topic, sizeof(topic), "homeassistant/device_automation/%s_btn%d/config", 
                 g_deviceId, btn);
        serializeJson(doc, payload, sizeof(payload));
        g_mqtt.publish(topic, payload, true);
    }
    
    Serial.println("MQTT: HA discovery complete");
}
