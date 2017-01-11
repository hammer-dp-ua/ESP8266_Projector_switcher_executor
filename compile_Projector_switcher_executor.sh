#!/bin/bash

cp -f /home/esp8266/Desktop/ESP8266_Projector_switcher_executor/user_main.c /home/esp8266/Desktop/ESP8266_RTOS_SDK/examples/project_template/user

cd /home/esp8266/Desktop/ESP8266_RTOS_SDK/examples/project_template/

make BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=2

cp -f /home/esp8266/Desktop/ESP8266_RTOS_SDK/bin/upgrade/user1.1024.new.2.bin /home/esp8266/Desktop/ESP8266_Projector_switcher_executor/bin/upgrade/