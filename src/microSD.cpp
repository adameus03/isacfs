#include <Arduino.h>
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static sdmmc_card_t *sdmmc_p;

esp_err_t init_sdcard()
{
  esp_err_t ret = ESP_FAIL;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 3,
  };

  ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &sdmmc_p);

  return ret;
}

esp_err_t micro_sd_read_sectors(void* dst, size_t start_sector, size_t sector_count){
    return sdmmc_read_sectors(sdmmc_p, dst, start_sector, sector_count);
}

esp_err_t micro_sd_write_sectors(const void* src, size_t start_sector, size_t sector_count){
    return sdmmc_write_sectors(sdmmc_p, src, start_sector, sector_count);
}

void micro_sd_print_csd(){
    Serial.println("SD MMC CSD\r\n--------------------");
    Serial.println("Capacity: " + String(sdmmc_p->csd.capacity));
    Serial.println("Command class: " + String(sdmmc_p->csd.card_command_class));
    Serial.println("CSD ver: " + String(sdmmc_p->csd.csd_ver));
    Serial.println("MMC ver: " + String(sdmmc_p->csd.mmc_ver));
    Serial.println("Read blk len: " + String(sdmmc_p->csd.read_block_len));
    Serial.println("Sector size: " + String(sdmmc_p->csd.sector_size));
    Serial.println("Tr speed: " + String(sdmmc_p->csd.tr_speed));
    Serial.println();
}

int micro_sd_get_sectors_count(){
    return sdmmc_p->csd.capacity;
}

int micro_sd_get_sector_size(){
    return sdmmc_p->csd.sector_size;
}

uint8_t micro_sd_get_sector_addr_width(){
    uint8_t addr_width = 0x1;
    static int cap;
    cap = sdmmc_p->csd.capacity;
    while((0x1 << addr_width) < cap){
        addr_width++;
    }
    return addr_width;
}

int micro_sd_get_offset_addr_width(){

    return sdmmc_p->csd.read_block_len;
}