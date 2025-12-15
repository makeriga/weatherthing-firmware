#include <Arduino.h>
#include "weatherthing_hw.h"
#include "cards.h"
#include "net.h"
#include "weather.h"
#include "sprites.h"
#include "settings.h"
#include "mqtt.h"
#include "factory_test.h"
#include "http_worker.h"

static bool g_factoryTestMode = false;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("WeatherThing firmware starting...");

    wt_hw_begin();
    sprites_begin();
    settings_begin();
    http_worker_begin();

    // Check if factory test needs to run (first boot)
    if (!factory_test_completed())
    {
        Serial.println("Factory test not completed - entering test mode");
        g_factoryTestMode = true;
        return; // Skip normal init, factory test will handle display
    }

    // Hold BTN2 at boot to force factory test re-run
    if (wt_button2_is_down())
    {
        Serial.println("BTN2 held at boot - forcing factory test");
        factory_test_reset();
        g_factoryTestMode = true;
        return;
    }

    if (wt_button1_is_down())
    {
        Serial.println("Factory reset requested (KEY1 held at boot) - clearing WiFi credentials");
        net_factory_reset();
    }

    net_begin();
    weather_begin();
    cards_begin();
}

void loop()
{
    // Factory test mode - run test sequence
    if (g_factoryTestMode)
    {
        if (factory_test_run())
        {
            // Test complete, restart to normal mode
            Serial.println("Factory test complete - restarting...");
            delay(1000);
            ESP.restart();
        }
        return;
    }

    net_loop();
    mqtt_loop();
    weather_update();
    cards_loop();
}
