this is example of realtime connection between esp32 and vps using mqtt or websocket 

esp32 mqtt.ino and flash file is same 
some commands to to use esp flash tool 

// copying from esp32 
python -m esptool --port COM11 read_flash 0x1000 0x400000 flash_dump.bin
// knowing info of esp32 
python -m esptool --port COM11 flash_id

// flashing esp 32 with flash file 
python -m esptool --port COM11 write_flash 0x1000 flash_dump.bin

