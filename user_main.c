/**
 * Pins 4 and 5 on some ESP8266-07 are exchanged on silk screen!!!
 */

#include "esp_common.h"
#include "uart.h"
#include "gpio.h"
#include "esp_sta.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "device_settings.h"
#include "espconn.h"
#include "user_main.h"
#include "utils.h"
#include "lwip/sys.h"

unsigned int milliseconds_g;
int signal_strength_g;
LOCAL os_timer_t millisecons_time_serv_g;

struct _esp_tcp user_tcp;

unsigned char responses_index;
char *responses[10];
unsigned int general_flags;

xSemaphoreHandle long_polling_request_semaphore_g;

char REQUEST[] ICACHE_RODATA_ATTR = "POST /server/esp8266/testDeferred HTTP/1.1\r\nContent-Length: 43\r\nHost: 192.168.0.2\r\nUser-Agent: ESP8266\r\nContent-Type: application/json\r\nAccept: application/json\r\n\r\n{\"gain\":\"-1\",\"deviceName\":\"Some Projector\"}\r\n";

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
   GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, pin);
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
void pin_output_reset(unsigned int pin) {
   GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pin);
   GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, pin);
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
   unsigned int *rom_string_aligned = (unsigned int*) ((unsigned int)rom_string & ~3); // Could be saved in not 4 bytes aligned address
   unsigned int rom_string_aligned_value = *rom_string_aligned;
   unsigned char shifted_bytes = (unsigned char) ((unsigned int)rom_string - (unsigned int)rom_string_aligned); // 0 - 3

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
      struct bss_info *got_bss_info = (struct bss_info *)arg;

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

         vTaskDelay(rescan_when_not_connected_task_delay);
      } else {
         vTaskDelay(rescan_when_connected_task_delay);
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
   vTaskDelay(6000 / portTICK_RATE_MS);

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
      void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_error;
      execute_on_error(connection);
   }
}

void successfull_disconnected_tcp_handler_callback(void *arg) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   bool response_received = user_data->response_received;

   //printf("Disconnected callback beginning. Response received: %d\n", response_received);
   void (*execute_on_succeed)(struct espconn *connection) = user_data->execute_on_succeed;
   execute_on_succeed(connection);
   printf("Disconnected callback end\n");
}

void tcp_connection_error_handler_callback(void *arg, sint8 err) {
   printf("Connection error callback. Error code: %d\n", err);

   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_error;

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
         printf("Response length: %d, content: %s\n", len, pdata);
      }
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
   struct connection_user_data *user_data = connection->reserve;
   char *request = user_data->request;

   pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
   set_flag(&general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED);
   if (request != NULL) {
      free(request);
      user_data->request = NULL;
   }
   if (user_data->timeout_request_supervisor_task != NULL) {
      vTaskDelete(user_data->timeout_request_supervisor_task);
   }
   espconn_delete(connection);
   printf("long_polling_request_on_error_callback\n");
   xSemaphoreGive(long_polling_request_semaphore_g);
}

void long_polling_request_on_succeed_callback(struct espconn *connection) {
   //printf("long_polling_request_on_succeed_callback beginning\n");
   struct connection_user_data *user_data = connection->reserve;

   if (!user_data->response_received) {
      long_polling_request_on_error_callback(connection);
      return;
   }

   char *request = user_data->request;

   pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);
   if (request != NULL) {
      free(request);
      user_data->request = NULL;
   }
   if (user_data->timeout_request_supervisor_task != NULL) {
      vTaskDelete(user_data->timeout_request_supervisor_task);
      //printf("timeout_request_supervisor_task exists\n");
   }
   espconn_delete(connection);
   printf("long_polling_request_on_succeed_callback end\n");
   xSemaphoreGive(long_polling_request_semaphore_g);
}

void timeout_request_supervisor_task(void *pvParameters) {
   vTaskDelay(LONG_POLLING_REQUEST_DURATION_TIME);
   //printf("Request timeout\n");

   struct espconn *connection = pvParameters;

   if (connection->state == ESPCONN_CONNECT) {
      printf("Was connected\n");
      espconn_disconnect(connection);
   } else {
      printf("Some another connection timeout error\n");
      struct connection_user_data *user_data = connection->reserve;

      user_data->timeout_request_supervisor_task = NULL;
      void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_error;
      execute_on_error(connection);
   }

   printf("timeout_request_supervisor_task to be deleted\n");
   vTaskDelete(NULL);
}

void send_long_polling_request_task(void *pvParameters) {
   for (;;) {
      if (read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN) && xSemaphoreTake(long_polling_request_semaphore_g, portMAX_DELAY) == pdTRUE) {
         if (read_flag(general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED)) {
            reset_flag(&general_flags, LONG_POLLING_REQUEST_ERROR_OCCURRED);

            vTaskDelay(LONG_POLLING_REQUEST_IDLE_TIME_ON_ERROR);
         }

         printf("Start creating request\n");
         char *request = get_string_from_rom(REQUEST);
         struct espconn connection;
         struct connection_user_data user_data;

         user_data.response_received = false;
         user_data.timeout_request_supervisor_task = NULL;
         user_data.request = request;
         user_data.execute_on_succeed = long_polling_request_on_succeed_callback;
         user_data.execute_on_error = long_polling_request_on_error_callback;
         connection.reserve = &user_data;
         connection.type = ESPCONN_TCP;
         connection.state = ESPCONN_NONE;

         // remote IP of TCP server
         unsigned char tcp_server_ip[] = {SERVER_IP_ADDRESS_1, SERVER_IP_ADDRESS_2, SERVER_IP_ADDRESS_3, SERVER_IP_ADDRESS_4};

         connection.proto.tcp = &user_tcp;
         memcpy(&connection.proto.tcp->remote_ip, tcp_server_ip, 4);
         connection.proto.tcp->remote_port = SERVER_PORT;
         connection.proto.tcp->local_port = espconn_port(); //local port of ESP8266

         espconn_regist_connectcb(&connection, successfull_connected_tcp_handler_callback);
         espconn_regist_disconcb(&connection, successfull_disconnected_tcp_handler_callback);
         espconn_regist_reconcb(&connection, tcp_connection_error_handler_callback);
         espconn_regist_sentcb(&connection, tcp_request_successfully_sent_handler_callback);
         espconn_regist_recvcb(&connection, tcp_response_received_handler_callback);
         //espconn_regist_write_finish(&connection, tcp_request_successfully_written_into_buffer_handler_callback);
         int connection_status = espconn_connect(&connection);

         //printf("Connection status: ");
         switch (connection_status) {
            case ESPCONN_OK:
               xTaskCreate(timeout_request_supervisor_task, "timeout_request_supervisor_task", 256, &connection, 1, &user_data.timeout_request_supervisor_task);
               //printf("Connected\n");
               break;
            case ESPCONN_RTE:
               //printf("Routing problem\n");
               break;
            case ESPCONN_MEM:
               //printf("Out of memory\n");
               break;
            case ESPCONN_ISCONN:
               printf("Already connected\n");
               break;
            case ESPCONN_ARG:
               //printf("Illegal argument\n");
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
   //output_pins.GPIO_Pullup = GPIO_PullUp_DIS;
   gpio_config(&output_pins);
}

void user_init(void) {
   uart_init_new();
   UART_SetBaudrate(UART0, 115200);

   pins_config();
   wifi_set_event_handler_cb(wifi_event_handler_callback);
   set_default_wi_fi_settings();
   espconn_init();

   xTaskCreate(autoconnect_task, "autoconnect_task", 256, NULL, 1, NULL);
   xTaskCreate(scan_access_point_task, "scan_access_point_task", 256, NULL, 1, NULL);

   vSemaphoreCreateBinary(long_polling_request_semaphore_g);
   xSemaphoreGive(long_polling_request_semaphore_g);
   xTaskCreate(send_long_polling_request_task, "send_long_polling_request_task", 384, NULL, 1, NULL);
   //xTaskCreate(print_some_stuff_task, "print_some_stuff_task", 176, NULL, 1, NULL);
}
