#ifndef MAIN_HEADER
#define MAIN_HEADER

#define AP_CONNECTION_STATUS_LED_PIN GPIO_Pin_5
#define SERVER_AVAILABILITY_STATUS_LED_PIN GPIO_Pin_4
#define PROJECTOR_RELAY_PIN GPIO_Pin_14

#ifndef true // needed only for Eclipse
   typedef unsigned char bool;
   #define true 1
   #define false 0
#endif

#define LONG_POLLING_REQUEST_ERROR_OCCURRED  1
#define SERVER_IS_AVAILABLE                  2

#define LONG_POLLING_REQUEST_IDLE_TIME_ON_ERROR (10000 / portTICK_RATE_MS)
#define LONG_POLLING_REQUEST_DURATION_TIME (5.5 * 60 * 1000 / portTICK_RATE_MS)

char RESPONSE_OK_STATUS[] ICACHE_RODATA_ATTR = "200 OK";
char RESPONSE_SERVER_SENT_OK[] ICACHE_RODATA_ATTR = "\"statusCode\":\"OK\"";
char PROJECTOR_DEFERRED_POST_REQUEST[] ICACHE_RODATA_ATTR =
      "POST /server/esp8266/projectorDeferred HTTP/1.1\r\nContent-Length: <1>\r\nHost: <2>\r\nUser-Agent: ESP8266\r\nContent-Type: application/json\r\nAccept: application/json\r\n\r\n<3>\r\n";
char PROJECTOR_DEFERRED_REQUEST_PAYLOAD[] ICACHE_RODATA_ATTR = "{\"gain\":\"<1>\",\"serverIsAvailable\":<2>,\"deviceName\":\"<3>\",\"errors\":\"<4>\"}";
char TURN_ON_TRUE_JSON_ELEMENT[] ICACHE_RODATA_ATTR = "\"turnOn\":true";

struct connection_user_data {
   bool response_received;
   char *request;
   char *response;
   void (*execute_on_long_polling_succeed)(struct espconn *connection);
   void (*execute_on_long_polling_error)(struct espconn *connection);
   xTaskHandle timeout_request_supervisor_task;
};

void scan_access_point_task(void *pvParameters);
void send_long_polling_request_task(void *pvParameters);
void autoconnect_task(void *pvParameters);
void successfull_connected_tcp_handler_callback(void *arg);
void successfull_disconnected_tcp_handler_callback();
void tcp_connection_error_handler_callback(void *arg, sint8 err);
void tcp_response_received_handler_callback(void *arg, char *pdata, unsigned short len);
void tcp_request_successfully_sent_handler_callback();
void tcp_request_successfully_written_into_buffer_handler_callback();
void long_polling_request_on_succeed_callback(struct espconn *connection);
void long_polling_request_on_error_callback(struct espconn *connection);
void long_polling_request_finish_action(struct espconn *connection);
#endif
