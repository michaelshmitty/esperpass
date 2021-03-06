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

  // NOTE(m): Interval at which to restart the system to select a new
  // random StreetPass MAC from the list.
  // In seconds. Default: 900 (15 minutes)
  config->system_restart_interval = 900;

  // NOTE(m): How long to keep the "attwifi" AP up during one MAC cycle
  // In seconds. Default: 90 seconds.
  config->ap_enable_duration = 90;

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

  return 0;
}

void
config_save(sysconfig_p config)
{
  uint16_t base_address = FLASH_BLOCK_NO;
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
