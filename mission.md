We are building firmware for a thing called "weatherthing", which is a neat small PCB, with 7x20px WS2812 led matrix display, data in goes from left row and snakes towards right side (ie leftmost row top led continues on second row top led, the second row led bottom continues in third row bottom, etc). This display is used to display various information using "cards" or "states", for example fetch weather for a particular location, with an animated icon on the left side, temperature on the right. Alterntively we can show various other things, like price of some stock ticker, bitcoin price, play flappy bird, show music visualizations and display custom text, time and date and whatnot.
The display should be clearly understandable (ie. no weather is green, more like sunny is yellow, sunset is orange, storm is dark blue with some neat cloud animation or blue falling star background, etc. Only render colors which a WS2812B RGB led can display). Between each pixel is a plastic cross bleeding reduction spacer, in front of it is a milky acrylic sheet. 

Keep in mind that this is a crapload of LEDs so current limiting calculations must be happening to not allow total drive current above 2A total.

Above sits 12 WS2812B LEDs for a "timeline" display.
This is used mostly for showing selectable 12-24-48 or more hours of weather forecast for the defined location, say you can see that tomorrow is going to be sunny but afternoon may get some light rain. This can also be used in musical mode to show some night rider KITT center out musical amplitude or beat detection animation. Unlike the led matrix - these sit in a horizontal cavity, so the bar can crrossbleed colors to some degree, which should make a neat "timeline" effect.

We have two buttons, for which no particular usage is yet decided, most likely use one to go to next view/card and other to go to previous one. Same goes for capacitive touch button, idea is that if this gets put inside of a picture frame - buttons can not be pressed, so user could put an electrode behind the glass to have a "button" acessible from the ouside. 

The microphone is biased around 1.65V, so ADC value should sit still when silent, and jump around both directions with higher and higher amplitude depending on the noise level. Create some BPM/VU meter wrapper for this. Maybe a "secret knock" detector for switching states



First each device must spawn an AP with unique SSID in form of "WEATHERTHING_xxxx" + a unique but rememberable password. For each device we first must flash the base firmware and then deploy the custom serial/wifi creds. For testing purposes we are using a predefined password, but before release we must attend this. 

If device has no wifi client connection data - it must spawn AP, if it has wifi credentials - it must connect as a client to fetch time/weather other data. Only publically acessable APIs must be used, as there is no server infrastructure for this project.

When user connects to the wifi access point it should be reachable on weatherthing.local and 192.168.1.4 (or what was default esp32 IP), same information should be printed over usb serial console.

On this page the user must be able to configure web access (enter wifi hotspot SSID/PW credentials), set default location for weather forecasts preferably with some spelling check or dynamic search.

For all other cards we must provide various knobs and fields for user to be able to change on the web page (i.e. stock ticker name or timezone if not default for clock). 

For main settings (wifi saved credentials, button role, ambient light measurement, global brightness mapping according to ambient light, microphone live readout/sensitivity sliders, etc) there must be a separate section. 

If the board is plugged in power with one of the buttons held down - it must reset to factory settings (default wifi AP SSDI/PW, but everything else dropped).

