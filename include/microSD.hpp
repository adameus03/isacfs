#include <Arduino.h>
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

esp_err_t init_sdcard();
esp_err_t micro_sd_read_sectors(void *dst, size_t start_sector, size_t sector_count);
esp_err_t micro_sd_write_sectors(const void *src, size_t start_sector, size_t sector_count);
void micro_sd_print_csd();
int micro_sd_get_sectors_count();
int micro_sd_get_sector_size();
uint8_t micro_sd_get_sector_addr_width();
int micro_sd_get_offset_addr_width();
