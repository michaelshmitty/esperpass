#include "user_interface.h"
#include "lwip/ip.h"
#include "config_flash.h"


/*     From the document 99A-SDK-Espressif IOT Flash RW Operation_v0.2      *
 * -------------------------------------------------------------------------*
 * Flash is erased sector by sector, which means it has to erase 4Kbytes one
 * time at least. When you want to change some data in flash, you have to
 * erase the whole sector, and then write it back with the new data.
 *--------------------------------------------------------------------------*/
void
config_load_default(sysconfig_p config)
{
  uint8_t mac[6];

  wifi_get_macaddr(STATION_IF, mac);

  os_memset(config, 0, sizeof(sysconfig_t));
  os_printf("Loading default configuration\r\n");
  config->magic_number = MAGIC_NUMBER;
  config->length = sizeof(sysconfig_t);
  os_sprintf(config->ssid,"%s", WIFI_SSID);
  os_sprintf(config->password,"%s", WIFI_PASSWORD);
  config->auto_connect = 0;
  os_memset(config->bssid, 0, 6);
  os_sprintf(config->sta_hostname, "ESP_%02x%02x%02x", mac[3], mac[4], mac[5]);
  os_sprintf(config->ap_ssid,"%s", WIFI_AP_SSID);

  config->first_run = 1;
  config->ap_watchdog = -1;
  config->client_watchdog = -1;

  IP4_ADDR(&config->network_addr, 192, 168, 4, 1);
  config->dns_addr.addr = 0;  // use DHCP
  config->my_addr.addr = 0;  // use DHCP
  config->my_netmask.addr = 0;  // use DHCP
  config->my_gw.addr = 0;  // use DHCP
#ifdef PHY_MODE
  config->phy_mode = 3;  // mode n
#endif
  config->clock_speed = 80;
  config->status_led = STATUS_LED_GPIO;

  wifi_get_macaddr(STATION_IF, config->STA_MAC_address);

  config->dhcps_entries = 0;
#ifdef ACLS
  acl_init();	// initializes the ACLs, written in config during save
#endif

  config->current_mac_address = 0;
  // Interval to change mac address in seconds
  // Default: 28800 (8 hours)
  config->mac_change_interval = 28800;

  // list of mac addresses
  // from https://docs.google.com/spreadsheets/d/1su5u-vPrQwkTixR6YnOTWSi_Ls9lV-_XNJHaWIJspv4/edit#gid=0
  ets_str2macaddr(config->mac_list[0], "4E:53:50:4F:4F:40");
  ets_str2macaddr(config->mac_list[1], "4E:53:50:4F:4F:41");
  ets_str2macaddr(config->mac_list[2], "4E:53:50:4F:4F:42");
  ets_str2macaddr(config->mac_list[3], "4E:53:50:4F:4F:43");
  ets_str2macaddr(config->mac_list[4], "4E:53:50:4F:4F:44");
  ets_str2macaddr(config->mac_list[5], "4E:53:50:4F:4F:45");
  ets_str2macaddr(config->mac_list[6], "4E:53:50:4F:4F:46");
  ets_str2macaddr(config->mac_list[7], "4E:53:50:4F:4F:47");
  ets_str2macaddr(config->mac_list[8], "4E:53:50:4F:4F:48");
  ets_str2macaddr(config->mac_list[9], "4E:53:50:4F:4F:49");
  ets_str2macaddr(config->mac_list[10], "4E:53:50:4F:4F:4A");
  ets_str2macaddr(config->mac_list[11], "4E:53:50:4F:4F:4B");
  ets_str2macaddr(config->mac_list[12], "4E:53:50:4F:4F:4C");
  ets_str2macaddr(config->mac_list[13], "4E:53:50:4F:4F:4D");
  ets_str2macaddr(config->mac_list[14], "4E:53:50:4F:4F:4E");
  ets_str2macaddr(config->mac_list[15], "4E:53:50:4F:4F:4F");

  // Streetpass relay whitelist
  uint32_t daddr;
  uint32_t dmask;

  // Clear all acl rules
  acl_clear(0);
  acl_clear(1);
  acl_clear(2);
  acl_clear(3);

  // Whitelist broadcast to enable DHCP
  parse_IP_addr("255.255.255.255", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  // Whitelist DNS
  acl_add(0, 0, 0, 0, 0, IP_PROTO_UDP, 0, 53, ACL_ALLOW);

  // Whitelist Streetpass relays
  // acl from_sta IP any 52.43.174.40 allow
  parse_IP_addr("52.43.174.40", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("104.70.153.178", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("104.74.48.110", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("23.7.18.146", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("23.7.24.35", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("52.11.210.152", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("52.25.179.65", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("52.89.56.205", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("54.148.137.96", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("54.218.98.74", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("54.218.99.79", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("54.244.22.201", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("69.25.139.140", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("192.195.204.216", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  parse_IP_addr("52.10.249.207", &daddr, &dmask);
  acl_add(0, 0, 0, daddr, dmask, 0, 0, 0, ACL_ALLOW);

  // Default implementation denies everything not matched above.
  // This last rule is not necessary and commented out to save memory space:
  // acl_add(0, 0, 0, 0, 0, 0, 0, 0, ACL_DENY);
}

int
config_load(sysconfig_p config)
{
  if (config == NULL)
  {
    return -1;
  }
  uint16_t base_address = FLASH_BLOCK_NO;

  spi_flash_read(base_address* SPI_FLASH_SEC_SIZE, &config->magic_number, 4);

  if((config->magic_number != MAGIC_NUMBER))
  {
    os_printf("\r\nNo config found, saving default in flash\r\n");
    config_load_default(config);
    config_save(config);
    return -1;
  }

  os_printf("\r\nConfig found and loaded\r\n");
  spi_flash_read(base_address * SPI_FLASH_SEC_SIZE,
                 (uint32 *) config,
                 sizeof(sysconfig_t));
  if (config->length != sizeof(sysconfig_t))
  {
    os_printf("Length Mismatch, probably old version of config, loading defaults\r\n");
    config_load_default(config);
    config_save(config);
    return -1;
  }
#ifdef ACLS
  os_memcpy(&acl, &(config->acl), sizeof(acl));
  os_memcpy(&acl_freep, &(config->acl_freep), sizeof(acl_freep));
#endif
  return 0;
}

void
config_save(sysconfig_p config)
{
  uint16_t base_address = FLASH_BLOCK_NO;
#ifdef ACLS
  os_memcpy(&(config->acl), &acl, sizeof(acl));
  os_memcpy(&(config->acl_freep), &acl_freep, sizeof(acl_freep));
#endif
  os_printf("Saving configuration\r\n");
  spi_flash_erase_sector(base_address);
  spi_flash_write(base_address * SPI_FLASH_SEC_SIZE,
                  (uint32 *)config,
                  sizeof(sysconfig_t));
}

void
blob_save(uint8_t blob_no, uint32_t *data, uint16_t len)
{
  uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
  spi_flash_erase_sector(base_address);
  spi_flash_write(base_address * SPI_FLASH_SEC_SIZE, data, len);
}

void
blob_load(uint8_t blob_no, uint32_t *data, uint16_t len)
{
  uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
  spi_flash_read(base_address * SPI_FLASH_SEC_SIZE, data, len);
}

void
blob_zero(uint8_t blob_no, uint16_t len)
{
  int i;
  uint8_t z[len];
  os_memset(z, 0,len);
  uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
  spi_flash_erase_sector(base_address);
  spi_flash_write(base_address * SPI_FLASH_SEC_SIZE, (uint32_t *)z, len);
}
