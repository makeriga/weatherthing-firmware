#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

// MQTT module for WeatherThing
// Provides MQTT connectivity, Home Assistant auto-discovery, and notification display

// Initialize MQTT (call after WiFi connected)
void mqtt_begin();

// Call in main loop
void mqtt_loop();

// Check connection status
bool mqtt_is_connected();

// Get current display message (for MQTT card to render)
const char* mqtt_get_message();
const char* mqtt_get_title();
uint32_t mqtt_get_color();
uint8_t mqtt_get_icon();  // 0=none, 1=bell, 2=home, 3=alert, 4=info, 5=check

// Get message age for display timeout
uint32_t mqtt_get_message_age_ms();

// Check if there's a new message (resets flag when called)
bool mqtt_has_new_message();

// Clear current message
void mqtt_clear_message();

// Publish device state to Home Assistant
void mqtt_publish_state(const char* card_name, uint8_t brightness);

// Publish button event
void mqtt_publish_button(uint8_t button_id, bool pressed);

// Icon types for display
enum MqttIcon : uint8_t {
    MQTT_ICON_NONE = 0,
    MQTT_ICON_BELL = 1,      // Notification
    MQTT_ICON_HOME = 2,      // Home Assistant
    MQTT_ICON_ALERT = 3,     // Warning/Alert  
    MQTT_ICON_INFO = 4,      // Info
    MQTT_ICON_CHECK = 5,     // Success/OK
    MQTT_ICON_TEMP = 6,      // Temperature
    MQTT_ICON_HUMIDITY = 7,  // Humidity
    MQTT_ICON_POWER = 8,     // Power/Energy
    MQTT_ICON_MAIL = 9,      // Message/Mail
    MQTT_ICON_COUNT = 10
};

#endif
