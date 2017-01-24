#define AP_CONNECTION_STATUS_LED_PIN GPIO_Pin_5
#define SERVER_AVAILABILITY_STATUS_LED_PIN GPIO_Pin_4
#define PROJECTOR_RELAY_PIN GPIO_Pin_14

void scan_access_point_task(void *pvParameters);
void send_request_task(void *pvParameters);
void autoconnect_task(void *pvParameters);
void successfull_connected_tcp_handler_callback(void *arg);
void successfull_disconnected_tcp_handler_callback();
void tcp_connection_error_handler_callback(void *arg, sint8 err);
void tcp_response_received_handler_callback(void *arg, char *pdata, unsigned short len);
void tcp_request_successfully_sent_handler_callback();
void tcp_request_successfully_written_into_buffer_handler_callback();
