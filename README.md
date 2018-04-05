# PLEASE NOTE
It seems like Nintendo has quietly shut down the servers that manage the StreetPass data on March, 28th 2018. ESPerPass and other similar HomePass implementations have always relied on these servers. Without them it's impossible to exchange tags. This is very unfortunate and very likely spells the end of HomePass. I will keep [this project on GitHub](https://github.com/michaelshmitty/esperpass) for historical purposes and future reference but unless StreetPass comes back (through Nintendo, which is very unlikely or through a homebrew solution) this project is no longer maintained. See [Issue #9](https://github.com/michaelshmitty/esperpass/issues/9) for more information and links to other resources. Thanks for using ESPerPass!


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
After trying to implement firewall rules to limit the outbound connections we noticed a significant drop in Streetpass tags. Not only does the list we got from the Raspipass project seem out of date, it also appeared that the list of Streetpass addresses may be dynamic.
So firewall functionality is not present but instead the "attwifi" network is configured to only stay up for 90 seconds every 15 minutes. After 15 minutes have passed the system will restart with a new random Streetpass mac address and the cycle will start over.

## Instructions
Ok so getting this to work can be a bit of a pain. But I assure you, once it's set up you don't need to worry about it anymore. I did the development on MacOS but I'm going to assume most will be using Windows to flash the firmware and those who don't have Windows have a way to get it, either through a virtual machine or another computer.
So here goes:

1. Get an inexpensive ESP8266 based WiFi module (just a couple of bucks on Aliexpress). I use a [Wemos D1 mini](https://www.aliexpress.com/item/ESP8266-ESP12-ESP-12-WeMos-D1-Mini-WIFI-Dev-Kit-Development-Board-NodeMCU-Lua/32653918483.html). But a [nodemcu](https://www.aliexpress.com/item/1pcs-NodeMCU-V3-Lua-WIFI-module-integration-of-ESP8266-extra-memory-32M-Flash-USB-serial-CH340G/32819683968.html) will work just fine too. You can even use an [ESP-01](https://www.aliexpress.com/item/Free-shipping-Upgraded-version-ESP-01-ESP8266-serial-WIFI-wireless-module-wireless-transceiver-ESP01/32845672436.html) if you're feeling adventurous but ymmv and you're on your own.

2. Get the firmware. You need to download these two binary files: [0x00000.bin](https://github.com/michaelshmitty/esperpass/raw/master/firmware/0x00000.bin) and [0x10000.bin](https://github.com/michaelshmitty/esperpass/raw/master/firmware/0x10000.bin). You can also find them in the firmware folder of this repository. Put them on your Desktop somewhere for easy access later.

3. Get the driver for the usb-to-serial converter on your ESP8266 board. The Wemos D1 Mini and the Nodemcu both use the CH340G chip. [Download it here](https://wiki.wemos.cc/downloads). Install the driver before you connect your WiFi module to your computer. I got a message that the software may not have been installed correctly after the install finished. Just ignore that message, it did install correctly. Plug in your ESP8266 board with a USB cable and Windows should detect it and assign it an open COM port. On my computer it assigned it to COM4. This may be different on yours. Note this port number as you'll need to set this in the flash program.

4. Now that your module is properly recognized by Windows it's time to flash the firmware you downloaded in step 2. To do this you need the [Nodemcu flasher tool](https://github.com/nodemcu/nodemcu-flasher). Depending on your system download either the [32-bit](https://github.com/nodemcu/nodemcu-flasher/raw/master/Win32/Release/ESP8266Flasher.exe) or [64-bit](https://github.com/nodemcu/nodemcu-flasher/raw/master/Win64/Release/ESP8266Flasher.exe) executable. Make sure your ESP8266 module is plugged into the computer and run the flasher tool.
Under "Operation" select the COM port that you're wifi module is connected to. On my system it was COM4, yours may differ. Go to the "Config" tab and click on the GEAR icon of the first row. Select the 0x00000.bin file from your desktop. Then make sure the "Offset" in the last column is set to "0x00000". Then click on the GEAR icon of the second row and select the 0x10000.bin file from your desktop or wherever you've saved those earlier. Make sure the Offset in the last column of the second row is set to "0x10000".
Now finally make sure both checkboxes before the first and the second row are **checked**. Go back to the first tab "Operation" and hit "Flash". This should now successfully flash the firmware onto the chip. It takes about 30 seconds. When it's done the indicator in the lower left corner should be green. Congratulations, your WiFi module now has the ESPerPass firmware on it.
5. Configure ESPerPass. All ESPerpass needs to know now is how to access your WiFi network at home so it can connect to the Streetpass servers. So you'll need to tell your WiFi module the name of your WiFi network at home and its password (you are using a password to protect your WiFi network, right?). For security purposes the only way to configure ESPerPass is through a serial console that you can only access with your computer when the module is connected via USB. You do not need to have the module connected to your computer as soon as it's configured for your home WiFi network. You can just plug it into any USB charger and tuck it away behind a closet or something.
In order to connect to the module's console you'll need a terminal program. [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html) is often a popular choice. Download, install and run it. Make sure your WiFi module is connected to your computer via USB. In the "Session" category select "Serial" under "Connection type" and make sure you enter the correct COM port. The "Speed" field (baudrate) should be set to 115200. Next go to the "Terminal" category and switch on "Implicit CR in every LF". Then hit "Open" at the bottom right to open a terminal. Now hit the reset button on your ESP module and you should see some text scroll by. If everything went well you should see instructions on how to configure your home wifi network. Basically you issue 4 commands:
* set ssid yournetworkssid
* set password yournetworkpassword
* save
* reset

When this is done you should see a new wifi network appear called "attwifi". Now your 3DS should start receiving Streepass tags.
The ESP module will remember the settings so you can now safely unplug it from your computer and hook it up to a USB charger and tuck it away somewhere. Happy Streetpassing!

**NOTE**: If something about these instructions is unclear or you can't seem to make it work, just open an issue and I'll try to help out as much as I can. This initial setup is a pain, but once it's done you never need to touch it again. And for just a few $$ and some effort you have a reliable, secure Streetpass relay at home!

## TODO
* Review / update list of Streetpass mac addresses.
