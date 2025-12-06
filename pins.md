The device consists of a PCB with a ESP32-C3-Wroom-02 module, a horizontal 7x20px WS2812B LED matrix display, a 12LED WS2812B "timeline", 2 user programmable buttons, a photoresistor in a voltage divider for ambient level light measurement, AF223 based capacitive touch button and an elecret microphone with transistor amplifier. For user convenience additionally GPIO8 and GPIO9 are broken out as I2C with pullups (both are strapping pins, so they can not be low at boot). Additionally GPIO2 and GPIO10 are broken out on a header.

PINS
----
GPIO20 - external uart RXD header (not used, but can be used to add some sensors and whatnot)
GPIO21 - edternal uart TXD header (not used, but can be used to add some sensors and whatnot)
GPIO18 - USB D-
GPIO19 - USB D+
GPIO2 - User programmable button "KEY2", also external header (a strap pin, use carefully)
GPIO3 - LED Matrix data out
GPIO4 - MIC ADC in (mic amp transistor biased around raw 1.65V on ADC approx with no sound, when sound comes in it swings about both directions as AC through a dc block cap)
GPIO5 - CAPACITIVE BUTTON INPUT
GPIO6 - User programmable button "KEY1"
GPIO8 - I2C SDA (strapping pin, can not be low at boot)
GPIO9 - I2C SCL (strapping pin, can not be low at boot)
GPIO0 - brightness measurement photoresistor (voltage divder - 20-30k GL5537-1 photoresistor to 3v3, 10k to gnd)
GPIO1 - LED timeline data out
