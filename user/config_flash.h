#ifndef _CONFIG_FLASH_H_
#define _CONFIG_FLASH_H_

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "spi_flash.h"
#include "lwip/app/dhcpserver.h"

#include "user_config.h"
#include "acl.h"

#define FLASH_BLOCK_NO 0xc

#define MAGIC_NUMBER 0x112005fc

#define MAX_MAC_LIST_LENGTH 15

typedef struct
{
  // To check if the structure is initialized or not in flash
  uint32_t magic_number;

  // Length of the structure, since this is a evolving library,
  // the variant may change hence used for verification
  uint16_t length;

  /* Below variables are specific to my code */
  uint8_t ssid[32]; // SSID of the AP to connect to
  uint8_t password[64]; // Password of the network
  uint8_t auto_connect; // Should we auto connect
  uint8_t bssid[6]; // Optional: BSSID the AP
  uint8_t sta_hostname[32]; // Name of the station
  uint8_t ap_ssid[32]; // SSID of the own AP
  uint8_t first_run; // Has ESPerPass been configured yet?
  uint8_t current_mac_address;  // Holds currently broadcasted HomePass mac address index
  int32_t mac_change_interval;  // Interval to rotate HomePass mac address (in seconds)

  // Seconds without ap traffic will cause reset (-1 off, default)
  int32_t ap_watchdog;
  // Seconds without client traffic will cause reset (-1 off, default)
  int32_t client_watchdog;

  ip_addr_t network_addr; // Address of the internal network
  ip_addr_t dns_addr; // Optional: address of the dns server

  ip_addr_t my_addr; // Optional (if not DHCP): IP address of the uplink side
  ip_addr_t my_netmask; // Optional (if not DHCP): IP netmask of the uplink side
  ip_addr_t my_gw;  // Optional (if not DHCP): Gateway of the uplink side
#ifdef PHY_MODE
  uint16_t phy_mode; // WiFi PHY mode
#endif
  uint16_t clock_speed; // Freq of the CPU
  uint16_t status_led; // GPIO pin os the status LED (>16 disabled)

  uint8_t STA_MAC_address[6]; // MAC address of the STA

  uint16_t dhcps_entries; // number of allocated entries in the following table
  struct dhcps_pool dhcps_p[MAX_DHCP]; // DHCP entries
#ifdef ACLS
  acl_entry acl[MAX_NO_ACLS][MAX_ACL_ENTRIES]; // ACL entries
  uint8_t acl_freep[MAX_NO_ACLS]; // ACL free pointers
#endif

  // HomePass mac list
  // Allow 20 slots
  uint8_t mac_list[19][6];

} sysconfig_t, *sysconfig_p;

int config_load(sysconfig_p config);
void config_save(sysconfig_p config);

void blob_save(uint8_t blob_no, uint32_t *data, uint16_t len);
void blob_load(uint8_t blob_no, uint32_t *data, uint16_t len);
void blob_zero(uint8_t blob_no, uint16_t len);

#endif
