#!/bin/bash

linux_project_dir=/home/esp8266/Desktop/ESP8266_RTOS_SDK/examples/project_template
windows_project_dir=/home/esp8266/Desktop/Projector_switcher_executor
xtensa_compiler_dir=/opt/xtensa-lx106-elf
user1_file=$linux_project_dir/../../bin/upgrade/user1.1024.new.2.bin
user2_file=$linux_project_dir/../../bin/upgrade/user2.1024.new.2.bin
eagle_flash_file=$linux_project_dir/../../bin/eagle.flash.bin
eagle_irom_file=$linux_project_dir/../../bin/eagle.irom0text.bin
fota_dir=/mnt/ESP8266_FOTA

cp -f $windows_project_dir/user_main.c $linux_project_dir/user
cp -f $windows_project_dir/user_main.h $linux_project_dir/user
cp -f $windows_project_dir/utils.c $linux_project_dir/user
cp -f $windows_project_dir/utils.h $linux_project_dir/user
cp -f $windows_project_dir/device_settings.h $linux_project_dir/user
upgrade_lib_dir=$linux_project_dir/upgrade_lib
if [ ! -d $upgrade_lib_dir ]; then
	mkdir $upgrade_lib_dir
fi
cp -f $windows_project_dir/upgrade/upgrade.c $upgrade_lib_dir/
cp -f $windows_project_dir/upgrade/upgrade_crc32.c $upgrade_lib_dir/
cp -f $windows_project_dir/upgrade/upgrade_lib.c $upgrade_lib_dir/
cp -f $windows_project_dir/upgrade/Makefile $upgrade_lib_dir/

cd $linux_project_dir

make clean
#make BOOT=new APP=0 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=2
make BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=2
make BOOT=new APP=2 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=2

out_file=$linux_project_dir/.output/eagle/debug/image/eagle.app.v6.out
if [ -f $out_file ]; then
	$xtensa_compiler_dir/bin/xtensa-lx106-elf-objdump -dgl $out_file > $windows_project_dir/disassembled.txt
else
	echo "$out_file file doesn't exist"
fi

if [ -f $user1_file ]; then
	cp -f $user1_file $windows_project_dir/bin/upgrade/user1.bin
	cp -f $user1_file $fota_dir/user1.bin
	echo "$user1_file file copied"
fi

if [ -f $user2_file ]; then
	cp -f $user2_file $windows_project_dir/bin/upgrade/user2.bin
	cp -f $user2_file $fota_dir/user2.bin
	echo "$user2_file file copied"
fi

