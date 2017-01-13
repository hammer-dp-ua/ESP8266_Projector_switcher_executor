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

void ICACHE_FLASH_ATTR print_some_stuff_task(void *pvParameters) {
   vTaskDelay(6000 / portTICK_RATE_MS);
   GPIO_AS_OUTPUT(5);
   GPIO_OUTPUT_SET(5, 1);

   vTaskDelete(NULL);
}

void set_default_wi_fi_settings() {
   vTaskDelay(6000 / portTICK_RATE_MS);

   wifi_station_set_auto_connect(false);
   wifi_set_opmode(STATION_MODE);

   STATION_STATUS station_status = wifi_station_get_connect_status();
   if (station_status == STATION_GOT_IP) {
      wifi_station_disconnect();
   }

   struct station_config station_config_settings;

   wifi_station_get_config_default(&station_config_settings);

   char *default_access_point_name = get_string_from_rom(DEFAULT_ACCESS_POINT_NAME);
   char *default_access_point_password = get_string_from_rom(DEFAULT_ACCESS_POINT_PASSWORD);

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
   char *own_ip_address = get_string_from_rom(ESP8226_OWN_IP_ADDRESS);

   if (strncmp(current_ip, own_ip_address, 15) != 0) {
      struct ip_info ip_info_to_set;
      ip_info_to_set.ip.addr = ipaddr_addr(own_ip_address);
      wifi_set_ip_info(STATION_IF, &ip_info_to_set);
   }
   free(current_ip);
   free(own_ip_address);

   vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void user_init(void) {
   uart_init_new();
   UART_SetBaudrate(UART0, 115200);

   xTaskCreate(set_default_wi_fi_settings, "set_default_wi_fi_settings", 256, NULL, 9, NULL);
   //xTaskCreate(print_some_stuff_task, "print_some_stuff_task", 256, NULL, 9, NULL);
}
