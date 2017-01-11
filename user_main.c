#include "esp_common.h"
#include "uart.h"

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

LOCAL STATUS uart_tx_one_char(uint8 uart, uint8 TxChar) {
   while (1) {
      uint32 fifo_cnt = READ_PERI_REG(UART_STATUS(uart))
            & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);

      if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126) {
         break;
      }
   }

   WRITE_PERI_REG(UART_FIFO(uart), TxChar);
   return OK;
}

LOCAL void uart0_write_char(char c) {
   if (c == '\n') {
      uart_tx_one_char(UART0, '\r');
      uart_tx_one_char(UART0, '\n');
   } else if (c == '\r') {
   } else {
      uart_tx_one_char(UART0, c);
   }
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void user_init(void) {
   UART_ConfigTypeDef uartConfig;
   uartConfig.baud_rate = BIT_RATE_115200;
   uartConfig.data_bits = UART_WordLength_8b;
   uartConfig.parity = USART_Parity_None;
   uartConfig.stop_bits = USART_StopBits_1;
   uartConfig.flow_ctrl = USART_HardwareFlowControl_None;
   uartConfig.UART_RxFlowThresh = 120;
   uartConfig.UART_InverseMask = UART_None_Inverse;

   UART_ParamConfig(UART0, &uartConfig);
   os_install_putc1(uart0_write_char);

   os_printf("aaakkkkkk:%s\n", system_get_sdk_version());
}
