#!/bin/bash

linux_project_dir=/home/esp8266/Desktop/ESP8266_RTOS_SDK/examples/project_template
windows_project_dir=/home/esp8266/Desktop/Projector_switcher_executor
xtensa_compiler_dir=/opt/xtensa-lx106-elf

cp -f $windows_project_dir/user_main.c $linux_project_dir/user
cp -f $windows_project_dir/user_main.h $linux_project_dir/user
cp -f $windows_project_dir/device_settings.h $linux_project_dir/user

cd $linux_project_dir

make BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=2

out_file=$linux_project_dir/.output/eagle/debug/image/eagle.app.v6.out
if [ -f $out_file ]; then
   $xtensa_compiler_dir/bin/xtensa-lx106-elf-objdump -dgl $out_file > $windows_project_dir/disassembled.txt
else
   echo "$out_file file doesn't exist"
fi

cp -f $linux_project_dir/../../bin/upgrade/user1.1024.new.2.bin $windows_project_dir/bin/upgrade/