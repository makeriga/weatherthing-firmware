#include <Arduino.h>
#include "weatherthing_hw.h"
#include "cards.h"
#include "net.h"
#include "weather.h"
#include "sprites.h"
#include "settings.h"
#include "mqtt.h"

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("WeatherThing firmware starting...");

    wt_hw_begin();
    sprites_begin();
    settings_begin();

    if (wt_button1_pressed())
    {
        Serial.println("Factory reset requested (KEY1 held at boot) - clearing network config");
        net_factory_reset();
    }

    net_begin();
    weather_begin();
    cards_begin();
}

void loop()
{
    net_loop();
    mqtt_loop();
    weather_update();
    cards_loop();
}
