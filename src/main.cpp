#include <Arduino.h>
//#include "SD.h"
//#include "SD_MMC.h"

#include "microSD.hpp"


const char* getBinaryString(uint8_t u){
  static char binaryString[0x8];
  for (uint8_t i = 0; i < 0x8; i++){
    binaryString[0x7 - i] = (0x1 << i) & u ? '1' : '0';
  }
  return binaryString;
}

void printSector(uint8_t* buffer){
  for (uint8_t i = 0x0; i < 0x40; i++){
    for (uint8_t j = 0x0; j < 0x8; j++){
      Serial.print(getBinaryString(*(buffer + (i << 0x3) + j)));
      Serial.print(' ');
      //return;
    }
    Serial.println();
  }
}




void setup() {
  Serial.begin(115200);
  delay(1000);
  //SPI.begin();
  delay(1000);

  esp_err_t sd_err = init_sdcard();
  if (sd_err == ESP_OK)
  {
    Serial.println("SD card mount successfully!");
  }
  else
  {
    Serial.println("Failed to mount SD card VFAT filesystem. Error: " + String(esp_err_to_name(sd_err)));
  }

  delay(1000);

  micro_sd_print_csd();

  uint8_t buffer[0x200];

  for (uint8_t i = 0x0; i != 0xff; i++){

    micro_sd_read_sectors(buffer, i, 0x1);

    Serial.printf("Sector %u dump:\r\n", i);
    //printSector(buffer);
  }
}

void loop() {
  //SD.readRAW();
  //SD.writeRAW()
  delay(1000);
}

