#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/lwip_napt.h"
#include "lwip/app/dhcpserver.h"
#include "lwip/app/espconn.h"
#include "lwip/app/espconn_tcp.h"

#include "user_interface.h"
#include "string.h"
#include "driver/uart.h"

#include "ringbuf.h"
#include "user_config.h"
#include "config_flash.h"
#include "sys_time.h"

#include "easygpio.h"

#ifdef ACLS
#include "acl.h"
#endif

/* System Task, for signals refer to user_config.h */
#define user_procTaskPrio 0
#define user_procTaskQueueLen 1
os_event_t user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);

static os_timer_t ptimer;

int32_t ap_watchdog_cnt;
int32_t client_watchdog_cnt;
int32_t mac_cnt;

/* Some stats */
uint64_t Bytes_in, Bytes_out, Bytes_in_last, Bytes_out_last;
uint32_t Packets_in, Packets_out, Packets_in_last, Packets_out_last;
uint64_t t_old;

/* Hold the system wide configuration */
sysconfig_t config;

static ringbuf_t console_rx_buffer, console_tx_buffer;

static ip_addr_t my_ip;
static ip_addr_t dns_ip;
bool connected;
uint8_t my_channel;
bool do_ip_config;

static netif_input_fn orig_input_ap, orig_input_sta;
static netif_linkoutput_fn orig_output_ap, orig_output_sta;

uint8_t remote_console_disconnect;
struct espconn *currentconn;

void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void);
void ICACHE_FLASH_ATTR user_set_softap_ip_config(void);
void ICACHE_FLASH_ATTR user_set_station_config(void);

void
ICACHE_FLASH_ATTR to_console(char *str)
{
  ringbuf_memcpy_into(console_tx_buffer, str, os_strlen(str));
}

err_t ICACHE_FLASH_ATTR
my_input_ap(struct pbuf *p, struct netif *inp)
{
  //  os_printf("Got packet from STA\r\n");

  if (config.status_led <= 16)
  {
    easygpio_outputSet (config.status_led, 1);
  }

  client_watchdog_cnt = config.client_watchdog;

#ifdef ACLS
  // Check ACLs - store result
  uint8_t acl_check = ACL_ALLOW;
  if (!acl_is_empty(0))
  {
    acl_check = acl_check_packet(0, p);
  }
#endif

#ifdef ACLS
  // If not allowed, drop packet
  if (!(acl_check&ACL_ALLOW))
  {
    pbuf_free(p);
    return;
  }
#endif

  Bytes_in += p->tot_len;
  Packets_in++;

  orig_input_ap (p, inp);
}

err_t ICACHE_FLASH_ATTR
my_output_ap(struct netif *outp, struct pbuf *p)
{
  //  os_printf("Send packet to STA\r\n");

  if (config.status_led <= 16)
  {
    easygpio_outputSet (config.status_led, 0);
  }

#ifdef ACLS
  // Check ACLs - store result
  uint8_t acl_check = ACL_ALLOW;
  if (!acl_is_empty(1))
  {
    acl_check = acl_check_packet(1, p);
  }
#endif

#ifdef ACLS
  // If not allowed, drop packet
  if (!(acl_check&ACL_ALLOW))
  {
    pbuf_free(p);
    return;
  }
#endif

  Bytes_out += p->tot_len;
  Packets_out++;

  orig_output_ap (outp, p);
}

err_t ICACHE_FLASH_ATTR
my_input_sta(struct pbuf *p, struct netif *inp)
{
  ap_watchdog_cnt = config.ap_watchdog;
#ifdef ACLS
  if (!acl_is_empty(2) && !(acl_check_packet(2, p) & ACL_ALLOW))
  {
    pbuf_free(p);
    return;
  }
#endif
  orig_input_sta (p, inp);
}

err_t ICACHE_FLASH_ATTR
my_output_sta(struct netif *outp, struct pbuf *p)
{
#ifdef ACLS
  if (!acl_is_empty(3) && !(acl_check_packet(3, p) & ACL_ALLOW))
  {
    pbuf_free(p);
    return;
  }
#endif
  orig_output_sta (outp, p);
}

static void ICACHE_FLASH_ATTR
patch_netif(ip_addr_t netif_ip,
            netif_input_fn ifn,
            netif_input_fn *orig_ifn,
            netif_linkoutput_fn ofn,
            netif_linkoutput_fn *orig_ofn,
            bool nat)
{
  struct netif *nif;

  for (nif = netif_list;
       nif != NULL && nif->ip_addr.addr != netif_ip.addr;
       nif = nif->next);

  if (nif == NULL)
  {
    return;
  }

  nif->napt = nat?1:0;
  if (ifn != NULL && nif->input != ifn)
  {
    *orig_ifn = nif->input;
    nif->input = ifn;
  }
  if (ofn != NULL && nif->linkoutput != ofn)
  {
    *orig_ofn = nif->linkoutput;
    nif->linkoutput = ofn;
  }
}

int ICACHE_FLASH_ATTR
parse_str_into_tokens(char *str, char **tokens, int max_tokens)
{
  char    *p, *q, *end;
  int     token_count = 0;
  bool    in_token = false;

  // preprocessing
  for (p = q = str; *p != 0; p++)
  {
    if (*(p) == '%' && *(p+1) != 0 && *(p+2) != 0)
    {
      // quoted hex
      uint8_t a;
      p++;
      if (*p <= '9')
          a = *p - '0';
      else
          a = toupper(*p) - 'A' + 10;
      a <<= 4;
      p++;
      if (*p <= '9')
          a += *p - '0';
      else
          a += toupper(*p) - 'A' + 10;
      *q++ = a;
    }
    else if (*p == '\\' && *(p+1) != 0)
    {
      // next char is quoted - just copy it, skip this one
      *q++ = *++p;
    }
    else if (*p == 8)
    {
      // backspace - delete previous char
      if (q != str)
      {
        q--;
      }
    }
    else if (*p <= ' ')
    {
      // mark this as whitespace
      *q++ = 0;
    }
    else
    {
      *q++ = *p;
    }
  }

  end = q;
  *q = 0;

  // cut into tokens
  for (p = str; p != end; p++)
  {
    if (*p == 0)
    {
      if (in_token)
      {
        in_token = false;
      }
    }
    else
    {
      if (!in_token)
      {
        tokens[token_count++] = p;
        if (token_count == max_tokens)
        {
          return token_count;
        }
        in_token = true;
      }
    }
  }
  return token_count;
}

void
console_send_response(struct espconn *pespconn, uint8_t do_cmd)
{
  char payload[MAX_CON_SEND_SIZE+4];
  uint16_t len = ringbuf_bytes_used(console_tx_buffer);

  ringbuf_memcpy_from(payload, console_tx_buffer, len);

  if (do_cmd)
  {
    os_memcpy(&payload[len], "CMD>", 4);
    len += 4;
  }

  if (pespconn != NULL)
  {
    espconn_sent(pespconn, payload, len);
  }
  else
  {
    UART_Send(0, &payload, len);
  }
}


#ifdef ACLS
void ICACHE_FLASH_ATTR
parse_IP_addr(uint8_t *str, uint32_t *addr, uint32_t *mask)
{
  int i;
  uint32_t net;
  if (strcmp(str, "any") == 0)
  {
    *addr = 0;
    *mask = 0;
    return;
  }

  for(i=0; str[i]!=0 && str[i]!='/'; i++);

  *mask = 0xffffffff;
  if (str[i]!=0)
  {
    str[i]=0;
    *mask <<= (32 - atoi(&str[i+1]));
  }
  *mask = htonl(*mask);
  *addr = ipaddr_addr(str);
}

struct espconn *deny_cb_conn = 0;
uint8_t acl_debug = 0;

uint8_t
acl_deny_cb(uint8_t proto,
            uint32_t saddr,
            uint16_t s_port,
            uint32_t daddr,
            uint16_t d_port,
            uint8_t allow)
{
  char response[128];

    if (!acl_debug)
    {
      return allow;
    }

    os_sprintf(response,
               "\rdeny: %s Src: %d.%d.%d.%d:%d Dst: %d.%d.%d.%d:%d\r\n",
               proto==IP_PROTO_TCP?"TCP":proto==IP_PROTO_UDP?"UDP":"IP4",
               IP2STR((ip_addr_t *)&saddr),
               s_port,
               IP2STR((ip_addr_t *)&daddr),
               d_port);

    if (acl_debug)
    {
      to_console(response);
      system_os_post(0, SIG_CONSOLE_TX, (ETSParam) deny_cb_conn);
    }
    return allow;
}
#endif /* ACLS */

// Use this from ROM instead
int ets_str2macaddr(uint8 *mac, char *str_mac);
#define parse_mac ets_str2macaddr
/*bool parse_mac(uint8_t *mac, uint8_t *inp)
{
int i;

    if (os_strlen(inp) != 17) return false;
    for (i=0; i<17; i++) {
  if (inp[i] == ':') continue;
  inp[i] = toupper(inp[i]);
        inp[i] = inp[i] <= '9'? inp[i]-'0' : (inp[i]-'A')+10;
  if (inp[i] >= 16) return false;
    }

    for (i=0; i<17; i+=3) {
  *mac++ = inp[i]*16+inp[i+1];
    }
    return true;
}
*/
static char INVALID_NUMARGS[] = "Invalid number of arguments\r\n";
static char INVALID_ARG[] = "Invalid argument\r\n";

void ICACHE_FLASH_ATTR
console_handle_command(struct espconn *pespconn)
{
  #define MAX_CMD_TOKENS 20

  char cmd_line[MAX_CON_CMD_SIZE+1];
  char response[1024];
  char *tokens[MAX_CMD_TOKENS];

  int bytes_count, nTokens;

  bytes_count = ringbuf_bytes_used(console_rx_buffer);
  ringbuf_memcpy_from(cmd_line, console_rx_buffer, bytes_count);

  cmd_line[bytes_count] = 0;
  response[0] = 0;

  nTokens = parse_str_into_tokens(cmd_line, tokens, MAX_CMD_TOKENS);

  if (nTokens == 0)
  {
    char c = '\n';
    ringbuf_memcpy_into(console_tx_buffer, &c, 1);
    goto command_handled_2;
  }

  if (strcmp(tokens[0], "help") == 0)
  {
    os_sprintf(response, "show [config|stats]\r\n");
    to_console(response);

    os_sprintf(response, "set [ssid|password|auto_connect|ap_ssid] <val>\r\nset [sta_mac|sta_hostname] <val>\r\nset [dns|ip|netmask|gw] <val>\r\n");
    to_console(response);
    os_sprintf(response, "set [speed|status_led|config_port] <val>\r\nsave [config|dhcp]\r\nconnect | disconnect| reset [factory] | quit\r\n");
    to_console(response);
    os_sprintf(response, "set [client_watchdog|ap_watchdog] <val>\r\n");
    to_console(response);
#ifdef PHY_MODE
    os_sprintf(response, "set phy_mode [1|2|3]\r\n");
    to_console(response);
#endif

#ifdef ACLS
    os_sprintf(response, "acl [from_sta|to_sta|from_ap|to_ap] clear\r\nacl [from_sta|to_sta|from_ap|to_ap] [IP|TCP|UDP] <src_addr> [<src_port>] <dest_addr> [<dest_port>] [allow|deny|allow_monitor|deny_monitor]\r\n");
    to_console(response);
#endif
    goto command_handled_2;
  }

  if (strcmp(tokens[0], "mac_list") == 0)
  {
    // List stored HomePass mac addresses
    int16_t i;

    to_console("HomePass mac list:\r\n");
    for (i = 0; i < MAX_MAC_LIST_LENGTH; i++)
    {
      os_sprintf(response, "%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                 config.mac_list[i][0], config.mac_list[i][1],
                 config.mac_list[i][2], config.mac_list[i][3],
                 config.mac_list[i][4], config.mac_list[i][5]);
      to_console(response);
    }
    os_sprintf(response, "\r\n");
    to_console(response);

    goto command_handled_2;
  }

  if (strcmp(tokens[0], "show") == 0)
  {
    int16_t i;
    ip_addr_t i_ip;

    if (nTokens == 1 || (nTokens == 2 && strcmp(tokens[1], "config") == 0))
    {
      os_sprintf(response,
                 "Version %s (build: %s)\r\n",
                 ESPERPASS_VERSION, __TIMESTAMP__);
      to_console(response);

      os_sprintf(response, "STA: SSID:%s PW:%s%s\r\n",
                 config.ssid,
                 (char*)config.password,
                 config.auto_connect?"":" [AutoConnect:0]");
      to_console(response);
      if (*(int*)config.bssid != 0)
      {
        os_sprintf(response, "BSSID: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                   config.bssid[0], config.bssid[1], config.bssid[2],
                   config.bssid[3], config.bssid[4], config.bssid[5]);
        to_console(response);
      }

      os_sprintf(response, "AP:  SSID:%s IP:%d.%d.%d.%d/24",
                 config.ap_ssid,
                 IP2STR(&config.network_addr));
      to_console(response);

      // if static DNS, add it
      os_sprintf(response,
                 config.dns_addr.addr?" DNS: %d.%d.%d.%d\r\n":"\r\n",
                 IP2STR(&config.dns_addr));
      to_console(response);

      // if static IP, add it
      os_sprintf(response,
                 config.my_addr.addr?"Static IP: %d.%d.%d.%d Netmask: %d.%d.%d.%d Gateway: %d.%d.%d.%d\r\n":"",
                 IP2STR(&config.my_addr), IP2STR(&config.my_netmask),
                 IP2STR(&config.my_gw));
      to_console(response);

      uint8_t current_mac[6];
      wifi_get_macaddr(SOFTAP_IF, current_mac);
      os_sprintf(response,
                 "STA MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\nAP MAC:  %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                 config.STA_MAC_address[0], config.STA_MAC_address[1],
                 config.STA_MAC_address[2], config.STA_MAC_address[3],
                 config.STA_MAC_address[4], config.STA_MAC_address[5],
                 current_mac[0],
                 current_mac[1],
                 current_mac[2],
                 current_mac[3],
                 current_mac[4],
                 current_mac[5]);
      to_console(response);
      os_sprintf(response, "STA hostname: %s\r\n", config.sta_hostname);
      to_console(response);

      os_sprintf(response, "Clock speed: %d\r\n", config.clock_speed);
      to_console(response);

      goto command_handled_2;
    }

    if (nTokens == 2 && strcmp(tokens[1], "stats") == 0)
    {
      uint32_t time = (uint32_t)(get_long_systime()/1000000);
      int16_t i;
      enum phy_mode phy;
      struct dhcps_pool *p;
      os_sprintf(response, "System uptime: %d:%02d:%02d\r\n",
                 time/3600, (time%3600)/60, time%60);
      to_console(response);
      os_sprintf(response,
                 "%d KiB in (%d packets)\r\n%d KiB out (%d packets)\r\n",
      (uint32_t)(Bytes_in/1024), Packets_in,
      (uint32_t)(Bytes_out/1024), Packets_out);
      to_console(response);
#ifdef PHY_MODE
      phy = wifi_get_phy_mode();
      os_sprintf(response, "Phy mode: %c\r\n",
                 phy == PHY_MODE_11B?'b':phy == PHY_MODE_11G?'g':'n');
      to_console(response);
#endif
      os_sprintf(response, "Free mem: %d\r\n", system_get_free_heap_size());
      to_console(response);
      if (connected)
      {
        os_sprintf(response, "External IP-address: " IPSTR "\r\n", IP2STR(&my_ip));
      }
      else
      {
        os_sprintf(response, "Not connected to AP\r\n");
      }
      to_console(response);

      os_sprintf(response,
                 "%d Station%s connected to SoftAP\r\n",
                 wifi_softap_get_station_num(),
      wifi_softap_get_station_num()==1?"":"s");

      to_console(response);
      for (i = 0; p = dhcps_get_mapping(i); i++)
      {
        os_sprintf(response,
                   "Station: %02x:%02x:%02x:%02x:%02x:%02x - "  IPSTR "\r\n",
                   p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4],
                   p->mac[5], IP2STR(&p->ip));
        to_console(response);
      }

      if (config.ap_watchdog >= 0 || config.client_watchdog >= 0)
      {
        os_sprintf(response, "AP watchdog: %d Client watchdog: %d\r\n",
                   ap_watchdog_cnt, client_watchdog_cnt);
        to_console(response);
      }
      goto command_handled_2;
    }
#ifdef ACLS
    if (nTokens == 2 && strcmp(tokens[1], "acl") == 0)
    {
      char *txt[] = {"From STA:\r\n",
                     "To STA:\r\n", "From AP:\r\n", "To AP:\r\n"};
      for (i = 0; i<MAX_NO_ACLS; i++)
      {
        if (!acl_is_empty(i))
        {
          ringbuf_memcpy_into(console_tx_buffer, txt[i], os_strlen(txt[i]));
          acl_show(i, response);
          to_console(response);
        }
      }
      os_sprintf(response, "Packets denied: %d Packets allowed: %d\r\n",
                 acl_deny_count, acl_allow_count);
      to_console(response);
      goto command_handled_2;
    }
#endif
  }

#ifdef ACLS
  if (strcmp(tokens[0], "acl") == 0)
  {
    uint8_t acl_no;
    uint8_t proto;
    uint32_t saddr;
    uint32_t smask;
    uint16_t sport;
    uint32_t daddr;
    uint32_t dmask;
    uint16_t dport;
    uint8_t allow;
    uint8_t last_arg;

    if (nTokens < 3)
    {
      os_sprintf(response, INVALID_NUMARGS);
      goto command_handled;
    }

    if (strcmp(tokens[1],"from_sta")==0)
    {
      acl_no = 0;
    }
    else if (strcmp(tokens[1],"to_sta")==0)
    {
      acl_no = 1;
    }
    else if (strcmp(tokens[1],"from_ap")==0)
    {
      acl_no = 2;
    }
    else if (strcmp(tokens[1],"to_ap")==0)
    {
      acl_no = 3;
    }
    else
    {
      os_sprintf(response, INVALID_ARG);
      goto command_handled;
    }

    if (strcmp(tokens[2],"clear")==0)
    {
      acl_clear(acl_no);
      os_sprintf(response, "ACL cleared\r\n");
      goto command_handled;
    }

    last_arg = 7;
    if (strcmp(tokens[2],"IP") == 0)
    {
      proto = 0;
      last_arg = 5;
    }
    else if (strcmp(tokens[2],"TCP") == 0)
    {
      proto = IP_PROTO_TCP;
    }
    else if (strcmp(tokens[2],"UDP") == 0)
    {
      proto = IP_PROTO_UDP;
    }
    else
    {
      os_sprintf(response, INVALID_ARG);
      goto command_handled;
    }

    if (nTokens != last_arg+1)
    {
      os_sprintf(response, INVALID_NUMARGS);
      goto command_handled;
    }

    if (proto == 0)
    {
      parse_IP_addr(tokens[3], &saddr, &smask);
      parse_IP_addr(tokens[4], &daddr, &dmask);
      sport = dport = 0;
    }
    else
    {
      parse_IP_addr(tokens[3], &saddr, &smask);
      sport = (uint16_t)atoi(tokens[4]);
      parse_IP_addr(tokens[5], &daddr, &dmask);
      dport = (uint16_t)atoi(tokens[6]);
    }

    if (strcmp(tokens[last_arg],"allow") == 0)
    {
       allow = ACL_ALLOW;
    }
    else if (strcmp(tokens[last_arg],"deny") == 0)
    {
      allow = ACL_DENY;
    }
    else
    {
      os_sprintf(response, INVALID_ARG);
      goto command_handled;
    }

    if (acl_add(acl_no, saddr, smask, daddr, dmask, proto, sport,
                dport, allow))
    {
      os_sprintf(response, "ACL added\r\n");
    }
    else
    {
      os_sprintf(response, "ACL add failed\r\n");
    }
    goto command_handled;
  }
#endif /* ACLS */

  if (strcmp(tokens[0], "connect") == 0)
  {
    if (nTokens > 1)
    {
      os_sprintf(response, INVALID_NUMARGS);
      goto command_handled;
    }

    user_set_station_config();
    os_sprintf(response, "Trying to connect to ssid %s, password: %s\r\n", config.ssid, config.password);

    wifi_station_disconnect();
    wifi_station_connect();

    goto command_handled;
  }

  if (strcmp(tokens[0], "disconnect") == 0)
  {
    if (nTokens > 1)
    {
      os_sprintf(response, INVALID_NUMARGS);
      goto command_handled;
    }

    os_sprintf(response, "Disconnect from ssid\r\n");

    wifi_station_disconnect();

    goto command_handled;
  }

  if (strcmp(tokens[0], "save") == 0)
  {
    if (nTokens == 1 || (nTokens == 2 && strcmp(tokens[1], "config") == 0))
    {
      config.first_run = 0;
      config_save(&config);
      os_sprintf(response, "Config saved\r\n");
      goto command_handled;
    }

    if (nTokens == 2 && strcmp(tokens[1], "dhcp") == 0)
    {
      int16_t i;
      struct dhcps_pool *p;

      for (i = 0; i<MAX_DHCP && (p = dhcps_get_mapping(i)); i++)
      {
        os_memcpy(&config.dhcps_p[i], p, sizeof(struct dhcps_pool));
      }
      config.dhcps_entries = i;
      config_save(&config);
      os_sprintf(response, "Config and DHCP table saved\r\n");
      goto command_handled;
    }
  }
  if (strcmp(tokens[0], "reset") == 0)
  {
    if (nTokens == 2 && strcmp(tokens[1], "factory") == 0)
    {
      config_load_default(&config);
      config_save(&config);
    }

    os_printf("Restarting ... \r\n");
    system_restart(); // if it works this will not return

    os_sprintf(response, "Reset failed\r\n");
    goto command_handled;
  }

  if (strcmp(tokens[0], "quit") == 0)
  {
    remote_console_disconnect = 1;
    os_sprintf(response, "Quitting console\r\n");
    goto command_handled;
  }

  if (strcmp(tokens[0], "set") == 0)
  {
    /*
     * For set commands atleast 2 tokens "set" "parameter" "value" is needed
     * hence the check
     */
    if (nTokens < 3)
    {
      os_sprintf(response, INVALID_NUMARGS);
      goto command_handled;
    }
    else
    {
      // atleast 3 tokens, proceed
      if (strcmp(tokens[1], "ssid") == 0)
      {
        // Handle case when ssid has spaces in it
        // This means the rest of the ssid has been split over the remainder
        // of the tokens[] array and we need to re-concatenate them
        // to get the full ssid name.
        if (nTokens > 3)
        {
          char ssid[32] = {};
          int i;

          // The ssid starts at the 3rd token, position 2 in the tokens
          // array. Hence i = 2.
          for (i = 2; i < nTokens; i++)
          {
            _strcat(ssid, tokens[i]);

            // Restore the space character between tokens
            // that form the ssid.
            // Don't add space after the last token.
            if (i < nTokens - 1)
            {
              _strcat(ssid, " ");
            }
          }

          // Copy the recomposed ssid to the config.
          os_sprintf(config.ssid, "%s", ssid);
        }
        else
        {
          os_sprintf(config.ssid, "%s", tokens[2]);
        }

        config.auto_connect = 1;
        os_sprintf(response, "SSID set (auto_connect = 1)\r\n");
        goto command_handled;
      }

      if (strcmp(tokens[1], "password") == 0)
      {
        // Handle case when password has spaces in it
        // This means the rest of the password has been split over the remainder
        // of the tokens[] array and we need to re-concatenate them
        // to get the full password.
        if (nTokens > 3)
        {
          char password[64] = {};
          int i;

          // The password starts at the 3rd token, position 2 in the tokens
          // array. Hence i = 2.
          for (i = 2; i < nTokens; i++)
          {
            _strcat(password, tokens[i]);

            // Restore the space character between tokens
            // that form the password.
            // Don't add space after the last token.
            if (i < nTokens - 1)
            {
              _strcat(password, " ");
            }
          }

          // Copy the recomposed password to the config.
          os_sprintf(config.password, "%s", password);
        }
        else
        {
          os_sprintf(config.password, "%s", tokens[2]);
        }

        os_sprintf(response, "Password set\r\n");
        goto command_handled;
      }

      if (strcmp(tokens[1], "auto_connect") == 0)
      {
        config.auto_connect = atoi(tokens[2]);
        os_sprintf(response, "Auto Connect set\r\n");
        goto command_handled;
      }

      if (strcmp(tokens[1], "sta_hostname") == 0)
      {
        os_sprintf(config.sta_hostname, "%s", tokens[2]);
        os_sprintf(response, "STA hostname set\r\n");
        goto command_handled;
      }

      if (strcmp(tokens[1], "ap_ssid") == 0)
      {
        os_sprintf(config.ap_ssid, "%s", tokens[2]);
        os_sprintf(response, "AP SSID set\r\n");
        goto command_handled;
      }

      if (strcmp(tokens[1], "ap_watchdog") == 0)
      {
        if (strcmp(tokens[2],"none") == 0)
        {
          config.ap_watchdog = ap_watchdog_cnt = -1;
          os_sprintf(response, "AP watchdog off\r\n");
          goto command_handled;
        }
        int32_t wd_val = atoi(tokens[2]);
        if (wd_val < 30)
        {
          os_sprintf(response, "AP watchdog value invalid\r\n");
          goto command_handled;
        }
        config.ap_watchdog = ap_watchdog_cnt = wd_val;
        os_sprintf(response,
                   "AP watchdog set to %d\r\n", config.ap_watchdog);
        goto command_handled;
      }

      if (strcmp(tokens[1], "client_watchdog") == 0)
      {
        if (strcmp(tokens[2], "none") == 0)
        {
          config.client_watchdog = client_watchdog_cnt = -1;
          os_sprintf(response, "Client watchdog off\r\n");
          goto command_handled;
        }
        int32_t wd_val = atoi(tokens[2]);
        if (wd_val < 30)
        {
          os_sprintf(response, "Client watchdog value invalid\r\n");
          goto command_handled;
        }
        config.client_watchdog = client_watchdog_cnt = wd_val;
        os_sprintf(response, "Client watchdog set to %d\r\n", config.client_watchdog);
        goto command_handled;
      }

#ifdef ACLS
      if (strcmp(tokens[1], "acl_debug") == 0)
      {
        acl_debug = atoi(tokens[2]);
        os_sprintf(response, "ACL debug set\r\n");
        goto command_handled;
      }
#endif

      if (strcmp(tokens[1], "speed") == 0)
      {
        uint16_t speed = atoi(tokens[2]);
        bool succ = system_update_cpu_freq(speed);
        if (succ)
        {
          config.clock_speed = speed;
        }
        os_sprintf(response, "Clock speed update %s\r\n",
                   succ?"successful":"failed");
        goto command_handled;
      }

      if (strcmp(tokens[1], "status_led") == 0)
      {
        if (config.status_led <= 16)
        {
          easygpio_outputSet (config.status_led, 1);
        }
        if (config.status_led == 1)
        {
          // Enable output if serial pin was used as status LED
          system_set_os_print(1);
        }
        config.status_led = atoi(tokens[2]);
        if (config.status_led > 16)
        {
          os_sprintf(response, "Status led disabled\r\n");
          goto command_handled;
        }
        if (config.status_led == 1)
        {
          // Disable output if serial pin is used as status LED
          system_set_os_print(0);
        }
        easygpio_pinMode(config.status_led, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
        easygpio_outputSet (config.status_led, 0);
        os_sprintf(response, "Status led set to GPIO %d\r\n",
                   config.status_led);
        goto command_handled;
      }

#ifdef PHY_MODE
      if (strcmp(tokens[1], "phy_mode") == 0)
      {
        uint16_t mode = atoi(tokens[2]);
        bool succ = wifi_set_phy_mode(mode);
        if (succ)
        {
          config.phy_mode = mode;
        }
        os_sprintf(response, "Phy mode setting %s\r\n",
                   succ?"successful":"failed");
        goto command_handled;
      }
#endif
      if (strcmp(tokens[1], "network") == 0)
      {
        config.network_addr.addr = ipaddr_addr(tokens[2]);
        ip4_addr4(&config.network_addr) = 0;
        os_sprintf(response, "Network set to %d.%d.%d.%d/24\r\n",
        IP2STR(&config.network_addr));
        goto command_handled;
      }

      if (strcmp(tokens[1], "dns") == 0)
      {
        if (os_strcmp(tokens[2], "dhcp") == 0)
        {
          config.dns_addr.addr = 0;
          os_sprintf(response, "DNS from DHCP\r\n");
        }
        else
        {
          config.dns_addr.addr = ipaddr_addr(tokens[2]);
          os_sprintf(response, "DNS set to %d.%d.%d.%d\r\n",
          IP2STR(&config.dns_addr));
          if (config.dns_addr.addr)
          {
            dns_ip.addr = config.dns_addr.addr;
            dhcps_set_DNS(&dns_ip);
          }
        }
        goto command_handled;
      }

      if (strcmp(tokens[1], "ip") == 0)
      {
        if (os_strcmp(tokens[2], "dhcp") == 0)
        {
          config.my_addr.addr = 0;
          os_sprintf(response, "IP from DHCP\r\n");
        }
        else
        {
          config.my_addr.addr = ipaddr_addr(tokens[2]);
          os_sprintf(response, "IP address set to %d.%d.%d.%d\r\n",
                     IP2STR(&config.my_addr));
        }
        goto command_handled;
      }

      if (strcmp(tokens[1],"netmask") == 0)
      {
        config.my_netmask.addr = ipaddr_addr(tokens[2]);
        os_sprintf(response, "IP netmask set to %d.%d.%d.%d\r\n",
                   IP2STR(&config.my_netmask));
        goto command_handled;
      }

      if (strcmp(tokens[1],"gw") == 0)
      {
        config.my_gw.addr = ipaddr_addr(tokens[2]);
        os_sprintf(response, "Gateway set to %d.%d.%d.%d\r\n",
                   IP2STR(&config.my_gw));
        goto command_handled;
      }

      if (strcmp(tokens[1], "ap_mac") == 0)
      {
        uint8_t new_ap_mac[6];
        if (!parse_mac(new_ap_mac, tokens[2]))
        {
          os_sprintf(response, INVALID_ARG);
        }
        else
        {
          wifi_set_macaddr(SOFTAP_IF, new_ap_mac);
          os_sprintf(response, "AP MAC set\r\n");
        }
        goto command_handled;
      }

      if (strcmp(tokens[1],"sta_mac") == 0)
      {
        if (!parse_mac(config.STA_MAC_address, tokens[2]))
        {
          os_sprintf(response, INVALID_ARG);
        }
        else
        {
          os_sprintf(response, "STA MAC set\r\n");
        }
        goto command_handled;
      }

      if (strcmp(tokens[1],"bssid") == 0)
      {
        if (!parse_mac(config.bssid, tokens[2]))
        {
          os_sprintf(response, INVALID_ARG);
        }
        else
        {
          os_sprintf(response, "bssid set\r\n");
        }
        goto command_handled;
      }
    }
  }

  /* Control comes here only if the tokens[0] command is not handled */
  os_sprintf(response, "\r\nInvalid Command\r\n");

command_handled:
  to_console("\r\n");
  to_console(response);

command_handled_2:
  system_os_post(0, SIG_CONSOLE_TX, (ETSParam) pespconn);

  return;
}

bool toggle;
// Timer cb function
void ICACHE_FLASH_ATTR
timer_func(void *arg)
{
  uint32_t Vcurr;
  uint64_t t_new;
  uint32_t t_diff;

  toggle = !toggle;

  // Check if watchdogs
  if (toggle)
  {
    // Rotate HomePass mac address if necessary
    if (config.auto_connect == 1)
    {
      if (mac_cnt >= config.mac_change_interval)
      {
        mac_cnt = 0;
        // os_printf("Rotating mac address...\r\n");
        // os_printf("config.current_mac_address = %d\r\n", config.current_mac_address);

        // os_printf("OLD MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
        //           config.mac_list[config.current_mac_address][0],
        //           config.mac_list[config.current_mac_address][1],
        //           config.mac_list[config.current_mac_address][2],
        //           config.mac_list[config.current_mac_address][3],
        //           config.mac_list[config.current_mac_address][4],
        //           config.mac_list[config.current_mac_address][5]);

        if (config.current_mac_address >= (MAX_MAC_LIST_LENGTH - 1))
        {
          config.current_mac_address = 0;
        }
        else
        {
          config.current_mac_address++;
        }

        // os_printf("NEW MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n\r\n",
        //           config.mac_list[config.current_mac_address][0],
        //           config.mac_list[config.current_mac_address][1],
        //           config.mac_list[config.current_mac_address][2],
        //           config.mac_list[config.current_mac_address][3],
        //           config.mac_list[config.current_mac_address][4],
        //           config.mac_list[config.current_mac_address][5]);

        // Save current mac address config to flash
        config_save(&config);

        // Start using new mac address
        // wifi_set_macaddr(SOFTAP_IF, config.mac_list[config.current_mac_address]);
        system_restart();
      }
      else
      {
        mac_cnt++;
      }
    }

    if (ap_watchdog_cnt >= 0)
    {
      if (ap_watchdog_cnt == 0)
      {
        os_printf("AP watchdog reset\r\n");
        system_restart();
      }
      ap_watchdog_cnt--;
    }

    if (client_watchdog_cnt >= 0)
    {
      if (client_watchdog_cnt == 0)
      {
        os_printf("Client watchdog reset\r\n");
        system_restart();
      }
      client_watchdog_cnt--;
    }
  }

  if (config.status_led <= 16)
  {
    easygpio_outputSet (config.status_led, toggle && connected);
  }

  // Do we still have to configure the AP netif?
  if (do_ip_config)
  {
    user_set_softap_ip_config();
    do_ip_config = false;
  }

  t_new = get_long_systime();

  os_timer_arm(&ptimer, toggle?900:100, 0);
}

//Priority 0 Task
static void ICACHE_FLASH_ATTR
user_procTask(os_event_t *events)
{
  //os_printf("Sig: %d\r\n", events->sig);

  switch(events->sig)
  {
    case SIG_START_SERVER:
    {
      // Anything else to do here, when the repeater has received its IP?
    } break;

    case SIG_CONSOLE_TX:
    case SIG_CONSOLE_TX_RAW:
    {
      struct espconn *pespconn = (struct espconn *) events->par;
      console_send_response(pespconn, events->sig == SIG_CONSOLE_TX);

      if (pespconn != 0 && remote_console_disconnect)
      {
        espconn_disconnect(pespconn);
      }
      remote_console_disconnect = 0;
    } break;

    case SIG_CONSOLE_RX:
    {
      struct espconn *pespconn = (struct espconn *) events->par;
      console_handle_command(pespconn);
    } break;

    case SIG_DO_NOTHING:
    default:
    {
      // Intentionally ignoring other signals
      os_printf("Spurious Signal received\r\n");
    } break;
  }
}

/* Callback called when the connection state of the module with an Access Point changes */
void
wifi_handle_event_cb(System_Event_t *evt)
{
  uint16_t i;
  uint8_t mac_str[20];

  //os_printf("wifi_handle_event_cb: ");
  switch (evt->event)
  {
    case EVENT_STAMODE_CONNECTED:
    {
      os_printf("connect to ssid %s, channel %d\r\n",
                evt->event_info.connected.ssid,
                evt->event_info.connected.channel);
      my_channel = evt->event_info.connected.channel;
    } break;

    case EVENT_STAMODE_DISCONNECTED:
    {
      os_printf("disconnect from ssid %s, reason %d\r\n",
                evt->event_info.disconnected.ssid,
                evt->event_info.disconnected.reason);
      connected = false;
    } break;

    case EVENT_STAMODE_AUTHMODE_CHANGE:
    {
      // os_printf("mode: %d -> %d\r\n",
      //           evt->event_info.auth_change.old_mode,
      //           evt->event_info.auth_change.new_mode);
    } break;

    case EVENT_STAMODE_GOT_IP:
    {
      if (config.dns_addr.addr == 0)
      {
        dns_ip = dns_getserver(0);
      }
      dhcps_set_DNS(&dns_ip);

      os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR ",dns:" IPSTR "\n",
                IP2STR(&evt->event_info.got_ip.ip),
                IP2STR(&evt->event_info.got_ip.mask),
                IP2STR(&evt->event_info.got_ip.gw),
                IP2STR(&dns_ip));

      my_ip = evt->event_info.got_ip.ip;
      connected = true;

      patch_netif(my_ip, my_input_sta, &orig_input_sta, my_output_sta, &orig_output_sta, false);

      // Post a Server Start message as the IP has been acquired to Task with priority 0
      system_os_post(user_procTaskPrio, SIG_START_SERVER, 0 );
    } break;

    case EVENT_SOFTAPMODE_STACONNECTED:
    {
      os_sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_connected.mac));
      os_printf("station: %s join, AID = %d\r\n",
                mac_str, evt->event_info.sta_connected.aid);
      ip_addr_t ap_ip = config.network_addr;
      ip4_addr4(&ap_ip) = 1;
      patch_netif(ap_ip, my_input_ap, &orig_input_ap, my_output_ap,
                  &orig_output_ap, true);
    } break;

    case EVENT_SOFTAPMODE_STADISCONNECTED:
    {
      os_sprintf(mac_str, MACSTR,
                 MAC2STR(evt->event_info.sta_disconnected.mac));
      os_printf("station: %s leave, AID = %d\r\n", mac_str,
                evt->event_info.sta_disconnected.aid);
    } break;

    default:
    {
      // Do nothing
    } break;
  }
}

void ICACHE_FLASH_ATTR
user_set_softap_wifi_config(void)
{
  struct softap_config apConfig;

  wifi_softap_get_config(&apConfig); // Get config first.

  os_memset(apConfig.ssid, 0, 32);
  os_sprintf(apConfig.ssid, "%s", config.ap_ssid);
  apConfig.authmode = AUTH_OPEN;
  apConfig.ssid_len = 0;// or its actual length

  // how many stations can connect to ESP8266 softAP at most.
  apConfig.max_connection = MAX_CLIENTS;

  // Set ESP8266 softap config
  wifi_softap_set_config(&apConfig);
}

void ICACHE_FLASH_ATTR
user_set_softap_ip_config(void)
{
  struct ip_info info;
  struct dhcps_lease dhcp_lease;
  struct netif *nif;
  int i;

  // Configure the internal network

  // Find the netif of the AP (that with num != 0)
  for (nif = netif_list; nif != NULL && nif->num == 0; nif = nif->next);
  if (nif == NULL)
  {
    return;
  }

  // If is not 1, set it to 1.
  // Kind of a hack, but the Espressif-internals expect it like this (hardcoded 1).
  nif->num = 1;

  wifi_softap_dhcps_stop();

  info.ip = config.network_addr;
  ip4_addr4(&info.ip) = 1;
  info.gw = info.ip;
  IP4_ADDR(&info.netmask, 255, 255, 255, 0);

  wifi_set_ip_info(nif->num, &info);

  dhcp_lease.start_ip = config.network_addr;
  ip4_addr4(&dhcp_lease.start_ip) = 2;
  dhcp_lease.end_ip = config.network_addr;
  ip4_addr4(&dhcp_lease.end_ip) = 128;
  wifi_softap_set_dhcps_lease(&dhcp_lease);

  wifi_softap_dhcps_start();

  // Change the DNS server again
  dhcps_set_DNS(&dns_ip);

  // Enter any saved dhcp enties if they are in this network
  for (i = 0; i<config.dhcps_entries; i++)
  {
    if ((config.network_addr.addr & info.netmask.addr) ==
        (config.dhcps_p[i].ip.addr & info.netmask.addr))
    {
      dhcps_set_mapping(&config.dhcps_p[i].ip,
                        &config.dhcps_p[i].mac[0], 100000 /* several months */);
    }
  }
}

void ICACHE_FLASH_ATTR
user_set_station_config(void)
{
  struct station_config stationConf;
  char hostname[40];

  /* Setup AP credentials */
  os_sprintf(stationConf.ssid, "%s", config.ssid);
  os_sprintf(stationConf.password, "%s", config.password);
  if (*(int*)config.bssid != 0)
  {
    stationConf.bssid_set = 1;
    os_memcpy(stationConf.bssid, config.bssid, 6);
  }
  else
  {
    stationConf.bssid_set = 0;
  }
  wifi_station_set_config(&stationConf);

  wifi_station_set_hostname(config.sta_hostname);

  wifi_set_event_handler_cb(wifi_handle_event_cb);

  wifi_station_set_auto_connect(config.auto_connect != 0);
}

#define RANDOM_REG (*(volatile u32 *)0x3FF20E44)

void ICACHE_FLASH_ATTR
user_init()
{
  struct ip_info info;

  connected = false;
  do_ip_config = false;
  my_ip.addr = 0;
  Bytes_in = Bytes_out = Bytes_in_last = Bytes_out_last = 0,
  Packets_in = Packets_out = Packets_in_last = Packets_out_last = 0;
  t_old = 0;

  console_rx_buffer = ringbuf_new(MAX_CON_CMD_SIZE);
  console_tx_buffer = ringbuf_new(MAX_CON_SEND_SIZE);

  gpio_init();
  init_long_systime();

  UART_init_console(BIT_RATE_115200, 0, console_rx_buffer, console_tx_buffer);

  os_printf("\r\n\r\nESPerPass %s starting\r\n", ESPERPASS_VERSION);

  // Load config
  config_load(&config);

#ifdef ACLS
  acl_debug = 0;
  int i;
  for(i=0; i< MAX_NO_ACLS; i++)
  {
    acl_clear_stats(i);
  }
  acl_set_deny_cb(acl_deny_cb);
#endif
  // Config GPIO pin as output
  if (config.status_led == 1)
  {
    // Disable output if serial pin is used as status LED
    system_set_os_print(0);
  }

  ap_watchdog_cnt = config.ap_watchdog;
  client_watchdog_cnt = config.client_watchdog;

  if (config.status_led <= 16)
  {
    easygpio_pinMode(config.status_led, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
    easygpio_outputSet (config.status_led, 0);
  }

  // Configure the AP and start it, if required
  if (config.dns_addr.addr == 0)
  {
    // Google's DNS as default, as long as we havn't got one from DHCP
    IP4_ADDR(&dns_ip, 8, 8, 8, 8);
  }
  else
  {
    // We have a static DNS server
    dns_ip.addr = config.dns_addr.addr;
  }

  wifi_set_opmode(STATIONAP_MODE);
  wifi_set_macaddr(SOFTAP_IF, config.mac_list[config.current_mac_address]);
  user_set_softap_wifi_config();
  do_ip_config = true;

  wifi_set_macaddr(STATION_IF, config.STA_MAC_address);

#ifdef PHY_MODE
  wifi_set_phy_mode(config.phy_mode);
#endif

  if (config.my_addr.addr != 0)
  {
    wifi_station_dhcpc_stop();
    info.ip.addr = config.my_addr.addr;
    info.gw.addr = config.my_gw.addr;
    info.netmask.addr = config.my_netmask.addr;
    wifi_set_ip_info(STATION_IF, &info);
    espconn_dns_setserver(0, &dns_ip);
  }

  remote_console_disconnect = 0;

  // Display first run message
  if (config.first_run == 1)
  {
    os_printf("\r\n\r\n***ESPerPass not yet configured***\r\n");
    os_printf("Hit return to show the CMD> prompt and follow these instructions:\r\n");
    os_printf("Note that the console does not support the backspace key.\r\n");
    os_printf("If you make a mistake, hit return and try the command again.\r\n");
    os_printf("Note that the maximum length for the SSID is 31 characters,\r\n");
    os_printf("for the password 64 characters. Spaces are allowed.\r\n\r\n");

    os_printf("1. Set your Internet WiFi ssid: set ssid <name>\r\n");
    os_printf("2. Set your Internet WiFi password: set password <password>\r\n");
    os_printf("3. Save the settings: save\r\n");
    os_printf("4. Restart the device: reset\r\n");
    os_printf("\r\n");

    // Disable any further verbose system output
    system_set_os_print(0);
  }
  else
  {
    // Now start the STA-Mode
    user_set_station_config();
  }

  system_update_cpu_freq(config.clock_speed);

  // Start the timer
  os_timer_setfn(&ptimer, timer_func, 0);
  os_timer_arm(&ptimer, 500, 0);

  //Start task
  system_os_task(user_procTask, user_procTaskPrio, user_procTaskQueue,
                 user_procTaskQueueLen);
}
