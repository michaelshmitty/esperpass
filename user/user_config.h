#ifndef _USER_CONFIG_
#define _USER_CONFIG_

#define ESPERPASS_VERSION "V0.0.6"

#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "password"

#define WIFI_AP_SSID "attwifi"

#define MAX_CLIENTS 8
#define MAX_DHCP 8

//
// Size of the console buffers
//
#define MAX_CON_SEND_SIZE 1024
#define MAX_CON_CMD_SIZE 80

//
// Define this if you have a status LED connected to a GPIO pin
//
#define STATUS_LED_GPIO	2

//
// Define this to support the setting of the WiFi PHY mode
//
#define PHY_MODE 1

//
// Define this if you want to have ACLs for the SoftAP.
//
#define ACLS 1

// Internal
typedef enum {SIG_DO_NOTHING=0, SIG_START_SERVER=1, SIG_SEND_DATA, SIG_UART0, SIG_CONSOLE_RX, SIG_CONSOLE_TX, SIG_CONSOLE_TX_RAW, SIG_GPIO_INT} USER_SIGNALS;

#endif
