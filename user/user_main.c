/**
 * Pins 4 and 5 on some ESP8266-07 are exchanged on silk screen!!!
 */

#include "esp_common.h"
#include "uart.h"
#include "gpio.h"
#include "esp_sta.h"
#include "esp_wifi.h"
#include "upgrade.h"
#include "freertos/FreeRTOS.h"
#include "device_settings.h"
#include "espconn.h"
#include "utils.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "user_main.h"
#include "global_printf_usage.h"

unsigned int milliseconds_g;
int signal_strength_g;
unsigned short errors_counter_g;
LOCAL os_timer_t millisecons_time_serv_g;

struct _esp_tcp user_tcp;

unsigned char responses_index;
char *responses[10];
unsigned int general_flags;

xSemaphoreHandle long_polling_request_semaphore_g;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
uint32 user_rf_cal_sector_set(void) {
   flash_size_map size_map = system_get_flash_size_map();
   uint32 rf_cal_sec = 0;

   switch (size_map) {
      case FLASH_SIZE_4M_MAP_256_256:
         rf_cal_sec = 128 - 5;
         break;

      case FLASH_SIZE_8M_MAP_512_512:
         rf_cal_sec = 256 - 5;
         break;

      case FLASH_SIZE_16M_MAP_512_512:
      case FLASH_SIZE_16M_MAP_1024_1024:
         rf_cal_sec = 512 - 5;
         break;

      case FLASH_SIZE_32M_MAP_512_512:
      case FLASH_SIZE_32M_MAP_1024_1024:
         rf_cal_sec = 1024 - 5;
         break;

      default:
         rf_cal_sec = 0;
         break;
   }
   return rf_cal_sec;
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
void pin_output_set(unsigned int pin) {
   GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, pin);
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
void pin_output_reset(unsigned int pin) {
   GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pin);
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
bool read_output_pin_state(unsigned int pin) {
   return (GPIO_REG_READ(GPIO_OUT_ADDRESS) & pin) ? true : false;
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
bool read_input_pin_state(unsigned int pin) {
   return (GPIO_REG_READ(GPIO_IN_ADDRESS) & pin) ? true : false;
}

LOCAL void calculate_rom_string_length_or_fill_malloc(unsigned short *string_length, char *result, const char *rom_string) {
   unsigned char calculate_string_length = *string_length ? 0 : 1;
   unsigned short calculated_string_length = 0;
   unsigned int *rom_string_aligned = (unsigned int*) (((unsigned int) (rom_string)) & ~3); // Could be saved in not 4 bytes aligned address
   unsigned int rom_string_aligned_value = *rom_string_aligned;
   unsigned char shifted_bytes = (unsigned char) ((unsigned int) (rom_string) - (unsigned int) (rom_string_aligned)); // 0 - 3

   unsigned char shifted_bytes_tmp = shifted_bytes;
   while (shifted_bytes_tmp < 4) {
      unsigned int comparable = 0xFF;
      unsigned char bytes_to_shift = shifted_bytes_tmp * 8;
      comparable <<= bytes_to_shift;
      unsigned int current_character_shifted = rom_string_aligned_value & comparable;

      if (current_character_shifted == 0) {
         break;
      }
      shifted_bytes_tmp++;

      if (!calculate_string_length) {
         char current_character = (char) (current_character_shifted >> bytes_to_shift);
         *(result + calculated_string_length) = current_character;
      }

      calculated_string_length++;
   }

   if (!calculated_string_length) {
      return;
   }

   unsigned int *rom_string_aligned_next = rom_string_aligned + 1;
   while (1) {
      unsigned int shifted_tmp = 0xFF;
      unsigned int rom_string_aligned_tmp_value = *rom_string_aligned_next;
      unsigned char stop = 0;

      while (shifted_tmp) {
         unsigned int current_character_shifted = rom_string_aligned_tmp_value & shifted_tmp;

         if (current_character_shifted == 0) {
            stop = 1;
            break;
         }

         if (!calculate_string_length) {
            unsigned char bytes_to_shift;

            if (shifted_tmp == 0xFF) {
               bytes_to_shift = 0;
            } else if (shifted_tmp == 0xFF00) {
               bytes_to_shift = 8;
            } else if (shifted_tmp == 0xFF0000) {
               bytes_to_shift = 16;
            } else {
               bytes_to_shift = 24;
            }

            char current_character = (char) (current_character_shifted >> bytes_to_shift);
            *(result + calculated_string_length) = current_character;
         }

         calculated_string_length++;
         shifted_tmp <<= 8;
      }

      if (stop) {
         break;
      }
      rom_string_aligned_next++;
   }

   if (calculate_string_length) {
      *string_length = calculated_string_length;
   } else {
      *(result + *string_length) = '\0';
   }
}

LOCAL void milliseconds_counter() {
   milliseconds_g++;
}

void start_millisecons_counter() {
   os_timer_disarm(&millisecons_time_serv_g);
   os_timer_setfn(&millisecons_time_serv_g, (os_timer_func_t *)milliseconds_counter, NULL);
   os_timer_arm(&millisecons_time_serv_g, 1, 1); // 1 ms
}

void stop_milliseconds_counter() {
   os_timer_disarm(&millisecons_time_serv_g);
}

/**
 * Do not forget to call free when a string is not required anymore
 */
char *get_string_from_rom(const char *rom_string) {
   unsigned short string_length = 0;

   calculate_rom_string_length_or_fill_malloc(&string_length, NULL, rom_string);

   if (!string_length) {
      return NULL;
   }

   char *result = malloc(string_length + 1); // 1 for the last empty character

   calculate_rom_string_length_or_fill_malloc(&string_length, result, rom_string);
   return result;
}

// Callback function when AP scanning is completed
void get_ap_signal_strength(void *arg, STATUS status) {
   if (status == OK) {
      struct bss_info *got_bss_info = (struct bss_info *) arg;

      signal_strength_g = got_bss_info->rssi;
      //got_bss_info = got_bss_info->next.stqe_next;
   }
}

void scan_access_point_task(void *pvParameters) {
   long rescan_when_connected_task_delay = 10 * 60 * 1000 / portTICK_RATE_MS; // 10 mins
   long rescan_when_not_connected_task_delay = 10 * 1000 / portTICK_RATE_MS; // 10 secs

   for (;;) {
      STATION_STATUS status = wifi_station_get_connect_status();

      if (status == STATION_GOT_IP) {
         struct scan_config ap_scan_config;
         char *default_access_point_name = get_string_from_rom(ACCESS_POINT_NAME);

         ap_scan_config.ssid = default_access_point_name;
         wifi_station_scan(&ap_scan_config, get_ap_signal_strength);
         free(default_access_point_name);

         vTaskDelay(rescan_when_connected_task_delay);
      } else {
         vTaskDelay(rescan_when_not_connected_task_delay);
      }
   }
}

void autoconnect_task(void *pvParameters) {
   long task_delay = 10000 / portTICK_RATE_MS;

   for (;;) {
      STATION_STATUS status = wifi_station_get_connect_status();
      read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN);
      if (status != STATION_GOT_IP && status != STATION_CONNECTING) {
         wifi_station_connect(); // Do not call this API in user_init
      }
      vTaskDelay(task_delay);
   }
}

void ICACHE_FLASH_ATTR print_some_stuff_task(void *pvParameters) {
   vTaskDelay(10000 / portTICK_RATE_MS);

#ifdef ALLOW_USE_PRINTF
   printf("Address of upgrade_firmware function: 0x%x\n", upgrade_firmware);
#endif
   upgrade_firmware();

   vTaskDelete(NULL);
}

void successfull_connected_tcp_handler_callback(void *arg) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   char *request = user_data->request;
   unsigned short request_length = strnlen(request, 0xFFFF);

   // Keep-Alive timeout doesn't work yet
   //espconn_set_opt(connection, ESPCONN_KEEPALIVE); // ESPCONN_REUSEADDR |
   //uint32 espconn_keepidle_value = 5; // seconds
   //unsigned char keepalive_error_code = espconn_set_keepalive(connection, ESPCONN_KEEPIDLE, &espconn_keepidle_value);
   //uint32 espconn_keepintvl_value = 2; // seconds
   // If there is no response, retry ESPCONN_KEEPCNT times every ESPCONN_KEEPINTVL
   //keepalive_error_code |= espconn_set_keepalive(connection, ESPCONN_KEEPINTVL, &espconn_keepintvl_value);
   //uint32 espconn_keepcnt_value = 2; // count
   //keepalive_error_code |= espconn_set_keepalive(connection, ESPCONN_KEEPCNT, &espconn_keepcnt_value);

   int sent_status = espconn_send(connection, request, request_length);
   free(request);
   user_data->request = NULL;

   if (sent_status != 0) {
      void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_long_polling_error;
      execute_on_error(connection);
   }
}

void successfull_disconnected_tcp_handler_callback(void *arg) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   bool response_received = user_data->response_received;

#ifdef ALLOW_USE_PRINTF
   printf("Disconnected callback beginning. Response received: %d\n", response_received);
#endif

   void (*execute_on_succeed)(struct espconn *connection) = user_data->execute_on_long_polling_succeed;
   execute_on_succeed(connection);

#ifdef ALLOW_USE_PRINTF
   printf("Disconnected callback end\n");
#endif
}

void tcp_connection_error_handler_callback(void *arg, sint8 err) {
#ifdef ALLOW_USE_PRINTF
   printf("Connection error callback. Error code: %d\n", err);
#endif

   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_long_polling_error;

   execute_on_error(connection);
}

void tcp_response_received_handler_callback(void *arg, char *pdata, unsigned short len) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   bool response_received = user_data->response_received;

   if (!response_received) {
      char *server_sent = get_string_from_rom(RESPONSE_SERVER_SENT_OK);

      if (strstr(pdata, server_sent)) {
         user_data->response_received = true;
         char *response = malloc(len);
         memcpy(response, pdata, len);
         user_data->response = response;

#ifdef ALLOW_USE_PRINTF
         printf("Response length: %d, content: %s\n", len, pdata);
#endif
      }
      free(server_sent);
   }

   // Don't call this API in any espconn callback. If needed, please use system task to trigger espconn_disconnect.
   //espconn_disconnect(connection);
}

void tcp_request_successfully_sent_handler_callback() {
   //printf("Request sent callback\n");
}

void tcp_request_successfully_written_into_buffer_handler_callback() {
   //printf("Request written into buffer callback\n");
}

void long_polling_request_on_error_callback(struct espconn *connection) {
#ifdef ALLOW_USE_PRINTF
   printf("long_polling_request_on_error_callback\n");
#endif

   struct connection_user_data *user_data = connection->reserve;
   char *request = user_data->request;

   errors_counter_g++;
   pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
   set_flag(&general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED_FLAG);
   reset_flag(&general_flags, SERVER_IS_AVAILABLE_FLAG);
   long_polling_request_finish_action(connection);
}

void long_polling_request_on_succeed_callback(struct espconn *connection) {
#ifdef ALLOW_USE_PRINTF
   printf("long_polling_request_on_succeed_callback\n");
#endif

   struct connection_user_data *user_data = connection->reserve;

   if (!user_data->response_received) {
      long_polling_request_on_error_callback(connection);
      return;
   }

   char *turn_on_true_json_element = get_string_from_rom(TURN_ON_TRUE_JSON_ELEMENT);

#ifdef ALLOW_USE_PRINTF
   printf("Response from long_polling_request_on_succeed_callback:\n%s", user_data->response);
#endif

   if (strstr(user_data->response, turn_on_true_json_element)) {
      pin_output_set(PROJECTOR_RELAY_PIN);
   } else {
      pin_output_reset(PROJECTOR_RELAY_PIN);
   }
   free(turn_on_true_json_element);

   char *update_firmware_json_element = get_string_from_rom(UPDATE_FIRMWARE);
   if (strstr(user_data->response, update_firmware_json_element)) {
      set_flag(&general_flags, UPDATE_FIRMWARE_FLAG);
   }
   free(update_firmware_json_element);

   set_flag(&general_flags, SERVER_IS_AVAILABLE_FLAG);
   pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);
   long_polling_request_finish_action(connection);
}

void long_polling_request_finish_action(struct espconn *connection) {
   struct connection_user_data *user_data = connection->reserve;
   char *request = user_data->request;

   if (request != NULL) {
      free(request);
      user_data->request = NULL;
   }
   if (user_data->response != NULL) {
      free(user_data->response);
      user_data->response = NULL;
   }

   if (user_data->timeout_request_supervisor_task != NULL) {
      vTaskDelete(user_data->timeout_request_supervisor_task);

#ifdef ALLOW_USE_PRINTF
      printf("timeout_request_supervisor_task exists\n");
#endif
   }
   espconn_delete(connection);
   xSemaphoreGive(long_polling_request_semaphore_g);
}

void timeout_request_supervisor_task(void *pvParameters) {
   vTaskDelay(LONG_POLLING_REQUEST_DURATION_TIME);

#ifdef ALLOW_USE_PRINTF
   printf("Request timeout\n");
#endif

   struct espconn *connection = pvParameters;

   if (connection->state == ESPCONN_CONNECT) {
#ifdef ALLOW_USE_PRINTF
      printf("Was connected\n");
#endif

      espconn_disconnect(connection);
   } else {
#ifdef ALLOW_USE_PRINTF
      printf("Some another connection timeout error\n");
#endif

      struct connection_user_data *user_data = connection->reserve;

      // To not delete this task in other functions
      user_data->timeout_request_supervisor_task = NULL;
      void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_long_polling_error;
      execute_on_error(connection);
   }

   vTaskDelete(NULL);
}

void ota_finished_callback(void *arg) {
   struct upgrade_server_info *update = arg;

   if (update->upgrade_flag == true) {
#ifdef ALLOW_USE_PRINTF
      printf("[OTA] success; rebooting!\n");
#endif

      system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
      system_upgrade_reboot();
   } else {
#ifdef ALLOW_USE_PRINTF
      printf("[OTA] failed!\n");
#endif

      system_restart();
   }

   free(&update->sockaddrin);
   free(update->url);
   free(update);
}

void blink_leds_while_updating_task(void *pvParameters) {
   for (;;) {
      if (read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN)) {
         pin_output_reset(AP_CONNECTION_STATUS_LED_PIN);
         pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);
      } else {
         pin_output_set(AP_CONNECTION_STATUS_LED_PIN);
         pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
      }

      vTaskDelay(100 / portTICK_RATE_MS);
   }
}

void upgrade_firmware() {
   xTaskCreate(blink_leds_while_updating_task, "blink_leds_while_updating_task", 256, NULL, 1, NULL);

   struct upgrade_server_info *upgrade_server = (struct upgrade_server_info *) zalloc(sizeof(struct upgrade_server_info));
   struct sockaddr_in *sockaddrin = (struct sockaddr_in *) zalloc(sizeof(struct sockaddr_in));

   upgrade_server->sockaddrin = *sockaddrin;
   upgrade_server->sockaddrin.sin_family = AF_INET;
   struct in_addr sin_addr;
   char *server_ip = get_string_from_rom(SERVER_IP_ADDRESS);
   sin_addr.s_addr = inet_addr(server_ip);
   upgrade_server->sockaddrin.sin_addr = sin_addr;
   upgrade_server->sockaddrin.sin_port = htons(SERVER_PORT);
   upgrade_server->sockaddrin.sin_len = sizeof(upgrade_server->sockaddrin);
   upgrade_server->check_cb = ota_finished_callback;
   upgrade_server->check_times = 10;

   char *url_pattern = get_string_from_rom(FIRMWARE_UPDATE_GET_REQUEST);
   unsigned char user_bin = system_upgrade_userbin_check();
   char *file_to_download = user_bin == UPGRADE_FW_BIN1 ? "user2.bin" : "user1.bin";
   char *url_parameters[] = {file_to_download, server_ip, NULL};
   char *url = set_string_parameters(url_pattern, url_parameters);

   free(url_pattern);
   free(server_ip);
   upgrade_server->url = url;
   system_upgrade_start(upgrade_server);
}

void send_long_polling_requests_task(void *pvParameters) {
   //vTaskDelay(5000 / portTICK_RATE_MS);
   for (;;) {
      if (read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN) && xSemaphoreTake(long_polling_request_semaphore_g, portMAX_DELAY) == pdTRUE) {
         if (read_flag(general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED_FLAG)) {
            reset_flag(&general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED_FLAG);

            vTaskDelay(LONG_POLLING_REQUEST_IDLE_TIME_ON_ERROR);
         }

         if (read_flag(general_flags, UPDATE_FIRMWARE_FLAG)) {
            reset_flag(&general_flags, UPDATE_FIRMWARE_FLAG);
            upgrade_firmware();
            continue;
         }

         char signal_strength[4];
         sprintf(signal_strength, "%d", signal_strength_g);
         char *server_is_available = read_flag(general_flags, SERVER_IS_AVAILABLE_FLAG) ? "true" : "false";
         char *device_name = get_string_from_rom(DEVICE_NAME);
         char errors_counter[5];
         sprintf(errors_counter, "%d", errors_counter_g);
         char build_timestamp[30];
         sprintf(build_timestamp, "%s", __TIMESTAMP__);
         char *projector_deferred_request_payload_template_parameters[] = {signal_strength, server_is_available, device_name, errors_counter, build_timestamp, NULL};
         char *projector_deferred_request_payload_template = get_string_from_rom(PROJECTOR_DEFERRED_REQUEST_PAYLOAD);
         char *request_payload = set_string_parameters(projector_deferred_request_payload_template, projector_deferred_request_payload_template_parameters);

         free(device_name);
         free(projector_deferred_request_payload_template);

         char *request_template = get_string_from_rom(PROJECTOR_DEFERRED_POST_REQUEST);
         unsigned short request_payload_length = strnlen(request_payload, 0xFFFF);
         char request_payload_length_string[3];
         sprintf(request_payload_length_string, "%d", request_payload_length);
         char *server_ip_address = get_string_from_rom(SERVER_IP_ADDRESS);
         char *request_template_parameters[] = {request_payload_length_string, server_ip_address, request_payload, NULL};
         char *request = set_string_parameters(request_template, request_template_parameters);

         free(request_payload);
         free(request_template);
         free(server_ip_address);

#ifdef ALLOW_USE_PRINTF
         printf("Request created: %s\n", request);
#endif

         struct espconn connection;
         struct connection_user_data user_data;

         user_data.response_received = false;
         user_data.timeout_request_supervisor_task = NULL;
         user_data.request = request;
         user_data.response = NULL;
         user_data.execute_on_long_polling_succeed = long_polling_request_on_succeed_callback;
         user_data.execute_on_long_polling_error = long_polling_request_on_error_callback;
         connection.reserve = &user_data;
         connection.type = ESPCONN_TCP;
         connection.state = ESPCONN_NONE;

         // remote IP of TCP server
         unsigned char tcp_server_ip[] = {SERVER_IP_ADDRESS_1, SERVER_IP_ADDRESS_2, SERVER_IP_ADDRESS_3, SERVER_IP_ADDRESS_4};

         connection.proto.tcp = &user_tcp;
         memcpy(&connection.proto.tcp->remote_ip, tcp_server_ip, 4);
         connection.proto.tcp->remote_port = SERVER_PORT;
         connection.proto.tcp->local_port = espconn_port(); // local port of ESP8266

         espconn_regist_connectcb(&connection, successfull_connected_tcp_handler_callback);
         espconn_regist_disconcb(&connection, successfull_disconnected_tcp_handler_callback);
         espconn_regist_reconcb(&connection, tcp_connection_error_handler_callback);
         espconn_regist_sentcb(&connection, tcp_request_successfully_sent_handler_callback);
         espconn_regist_recvcb(&connection, tcp_response_received_handler_callback);
         //espconn_regist_write_finish(&connection, tcp_request_successfully_written_into_buffer_handler_callback);
         int connection_status = espconn_connect(&connection);

#ifdef ALLOW_USE_PRINTF
         printf("Connection status: ");
#endif

         switch (connection_status) {
            case ESPCONN_OK:
               xTaskCreate(timeout_request_supervisor_task, "timeout_request_supervisor_task", 256, &connection, 1, &user_data.timeout_request_supervisor_task);

#ifdef ALLOW_USE_PRINTF
               printf("Connected\n");
#endif
               break;
            case ESPCONN_RTE:
#ifdef ALLOW_USE_PRINTF
               printf("Routing problem\n");
#endif

               break;
            case ESPCONN_MEM:
#ifdef ALLOW_USE_PRINTF
               printf("Out of memory\n");
#endif

               break;
            case ESPCONN_ISCONN:
#ifdef ALLOW_USE_PRINTF
               printf("Already connected\n");
#endif

               break;
            case ESPCONN_ARG:
#ifdef ALLOW_USE_PRINTF
               printf("Illegal argument\n");
#endif

               break;
         }

         if (connection_status != ESPCONN_OK) {
            long_polling_request_on_error_callback(&connection);
         }
      } else if (read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN)) {
         pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
         vTaskDelay(1000 / portTICK_RATE_MS);
      }
   }
}

void wifi_event_handler_callback(System_Event_t *event) {
   switch (event->event_id) {
      case EVENT_STAMODE_CONNECTED:
         pin_output_set(AP_CONNECTION_STATUS_LED_PIN);
         break;
      case EVENT_STAMODE_DISCONNECTED:
         pin_output_reset(AP_CONNECTION_STATUS_LED_PIN);
         break;
   }
}

void set_default_wi_fi_settings() {
   wifi_station_set_auto_connect(false);
   wifi_station_set_reconnect_policy(false);
   wifi_station_dhcpc_stop();
   wifi_set_opmode(STATION_MODE);

   STATION_STATUS station_status = wifi_station_get_connect_status();
   if (station_status == STATION_GOT_IP) {
      wifi_station_disconnect();
   }

   struct station_config station_config_settings;

   wifi_station_get_config_default(&station_config_settings);

   char *default_access_point_name = get_string_from_rom(ACCESS_POINT_NAME);
   char *default_access_point_password = get_string_from_rom(ACCESS_POINT_PASSWORD);

   if (strncmp(default_access_point_name, station_config_settings.ssid, 32) != 0
         || strncmp(default_access_point_password, station_config_settings.password, 64) != 0) {
      struct station_config station_config_settings_to_save;

      memcpy(&station_config_settings_to_save.ssid, default_access_point_name, 32);
      memcpy(&station_config_settings_to_save.password, default_access_point_password, 64);
      wifi_station_set_config(&station_config_settings_to_save);
   }
   free(default_access_point_name);
   free(default_access_point_password);

   struct ip_info current_ip_info;
   wifi_get_ip_info(STATION_IF, &current_ip_info);
   char *current_ip = ipaddr_ntoa(&current_ip_info.ip);
   char *own_ip_address = get_string_from_rom(OWN_IP_ADDRESS);

   if (strncmp(current_ip, own_ip_address, 15) != 0) {
      char *own_netmask = get_string_from_rom(OWN_NETMASK);
      char *own_getaway_address = get_string_from_rom(OWN_GETAWAY_ADDRESS);
      struct ip_info ip_info_to_set;

      ip_info_to_set.ip.addr = ipaddr_addr(own_ip_address);
      ip_info_to_set.netmask.addr = ipaddr_addr(own_netmask);
      ip_info_to_set.gw.addr = ipaddr_addr(own_getaway_address);
      wifi_set_ip_info(STATION_IF, &ip_info_to_set);
      free(own_netmask);
      free(own_getaway_address);
   }
   free(current_ip);
   free(own_ip_address);
}

pins_config() {
   GPIO_ConfigTypeDef output_pins;
   //output_pins.GPIO_IntrType = GPIO_PIN_INTR_ANYEDGE;
   output_pins.GPIO_Mode = GPIO_Mode_Output;
   output_pins.GPIO_Pin = AP_CONNECTION_STATUS_LED_PIN | SERVER_AVAILABILITY_STATUS_LED_PIN | PROJECTOR_RELAY_PIN;
   pin_output_reset(AP_CONNECTION_STATUS_LED_PIN);
   pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
   pin_output_reset(PROJECTOR_RELAY_PIN);
   //output_pins.GPIO_Pullup = GPIO_PullUp_DIS;
   gpio_config(&output_pins);
}

void user_init(void) {
   uart_init_new();
   UART_SetBaudrate(UART0, 115200);

#ifdef ALLOW_USE_PRINTF
   printf("\nSoftware is running from: %s\n", system_upgrade_userbin_check() ? "user2.bin" : "user1.bin");
#endif

   pins_config();
   wifi_set_event_handler_cb(wifi_event_handler_callback);
   vTaskDelay(5000 / portTICK_RATE_MS);
   set_default_wi_fi_settings();
   espconn_init();

   xTaskCreate(autoconnect_task, "autoconnect_task", 256, NULL, 1, NULL);
   xTaskCreate(scan_access_point_task, "scan_access_point_task", 256, NULL, 1, NULL);

   vSemaphoreCreateBinary(long_polling_request_semaphore_g);
   xSemaphoreGive(long_polling_request_semaphore_g);
   xTaskCreate(send_long_polling_requests_task, "send_long_polling_requests_task", 384, NULL, 1, NULL);
   //xTaskCreate(print_some_stuff_task, "print_some_stuff_task", 256, NULL, 1, NULL);
}
