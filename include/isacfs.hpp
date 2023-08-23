#include "microSD.hpp"

#define UNKNOWN_SECTOR 0x0
#define UNKNOWN_OFFSET 0x0

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;

typedef enum
{
    isacfs_ok,
    isacfs_256GiB_limit_exceeded
} isacfs_err_t;

typedef struct {
    u32 sector = 0x0;
    u32 offset = 0x0;
    u8 year_diff;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
} isacfs_file_meta;


/**
 * @note "init_sdcard" has to be called first
*/
isacfs_err_t isacfs_init();

/**
 * @brief Encode/compress "file_meta" into a 64-bit block
*/
const u8 *__isacfs_file_meta__to__desc_8B_blk(isacfs_file_meta *file_meta_p);

/**
 * @brief Decode/expand "desc_8B_blk" into a isac_file_meta structure
*/
const isacfs_file_meta *__desc_8B_blk__to__isacfs_file_meta(const u8 *desc_8B_blk);

/**
 * @brief Fills all the sectors with zeroes
*/
esp_err_t __isacfs_clear_all_sectors();

void isacfs_format();

void isacfs_write_file(isacfs_file_meta file_meta, const void *buffer, u32 buf_sz);

/**
 * @brief Fills "file_meta" with the sector & offset info
 * @returns Size of the file (in bytes)
*/
u32 isacfs_file_desc(isacfs_file_meta file_meta);

/**
 * Read the file based on the sector, offset and size data obtained using the "isacfs_file_desc" function
*/
void isacfs_read_file(isacfs_file_meta file_meta, void *out_buffer);
