# ESPerPass
A 3DS HomePass implementation for the ESP8266 programmable WiFi chip.
Inspired by [this reddit post](https://www.reddit.com/r/3DS/comments/80k0qb/homepass_for_cheap_using_an_esp8266/)
and largely based on the great work done by [Martin Ger and his ESP WiFi Repeater](https://github.com/martin-ger/esp_wifi_repeater).

## What is this?
EsperPass creates a relay between your home WiFi Internet connection and your Streetpass capable device (e.g. Nintendo 3DS). It does this by setting up an insecure, open WiFi network with the name "attwifi". Anyone who connects to this network can access the Internet through your home WiFi connection. This is very insecure but it is however what a Streetpass enabled device expects. In addition it expects the MAC address of the "attwifi" network to rotate periodically through a list of known Streetpass addresses. This software does that.
So basically ESPerPass is a cheap way to have your own Streetpass relay at home.
A similar project using a Raspberry Pi 3 is [RaspiPass](https://github.com/Pinchie/RaspiPass).

## Security considerations
Martin Ger's original ESP WiFi Repeater software has been heavily modified to strip it of any unnecessary functionality for the purpose of this project. Convenience has given way to security by removing the web configuration interface. This makes it slightly more cumbersome to configure ESPerPass for your WiFi network, but at the same time prevents your home WiFi network configuration from potentially leaking through a web configuration page.
Currently there are no firewall rules in place yet because I don't know if there is a list of official Streetpass IP addresses. A soon as this is resolved, the software should be locked down to only allow connections to those addresses, making the usage of the open network otherwise useless.

## Instructions (needs improvement)
1. Get an ESP8266 based WiFi module. I use a [Wemos D1 mini](https://www.aliexpress.com/item/ESP8266-ESP12-ESP-12-WeMos-D1-Mini-WIFI-Dev-Kit-Development-Board-NodeMCU-Lua/32653918483.html).
2. Get the firmware. You need [this bin file](https://github.com/michaelshmitty/esperpass/raw/master/firmware/0x00000.bin) and [this one](https://github.com/michaelshmitty/esperpass/raw/master/firmware/0x00000.bin).
3. Flash the binary firmware files to the ESP8266 module with the esptool.py software. I use the following parameters: esptool.py --port /dev/tty.wchusbserial1420 write_flash -fs 32m -ff 80m -fm dio 0x00000 0x00000.bin 0x10000 0x10000.bin.
Replace /dev/tty.wchusbserial1420 with your ESP usb to serial.
4. Connect ESP8266 to USB and connect with a terminal program at 115200 baud.
5. Power cycle ESP8266.
6. Follow instructions in the terminal program to configure your WiFi Internet connection.

## TODO
* Implement firewall rules to restrict connections to official servers.
* Implement mac address list management through console.
