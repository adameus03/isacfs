#include "microSD.hpp"

#define UNKNOWN_SECTOR 0x0
#define UNKNOWN_OFFSET 0x0
#define YEAR_DIFF_REF 2023
#define FILE_LEAP 3600 

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;
typedef enum
{
    isacfs_ok,
    isacfs_256GiB_limit_exceeded,
    isacfs_fail
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
    //u8 index;

} isacfs_file_meta;

static u32 SECTOR_COUNT;//= 0x1 << 23U;
static u32 SECTOR_SIZE;//= 0x200;
static u32 AVG_FILE_SIZE = 0x1 << 14U;
static u8 SECTOR_ADDR_WIDTH;
static u8 OFFSET_ADDR_WIDTH;
static u8 YEAR_DIFF_WIDTH;

static u32 CURR_WRITE_META_SECTOR;
static u32 CURR_WRITE_META_OFFSET;

static u32 CURR_WRITE_DATA_SECTOR;
static u32 CURR_WRITE_DATA_OFFSET;

/* format_critical (stored on the card)*/
static u32 DATA_START_SECTOR;
static u32 DATA_START_OFFSET;
static u32 FUTURE_WRITE_META_SECTOR;
static u32 FUTURE_WRITE_META_OFFSET;

/**
 * @brief Deduce using AVG_FILE_SIZE (in bytes)
 * @param[out] avg_file_sectors
 * @param[out] avg_file_offset
*/
void __isacfs_deduce_avg_file_storage(u32* avg_file_sectors, u32* avg_file_offset){
    *avg_file_sectors = AVG_FILE_SIZE >> OFFSET_ADDR_WIDTH;
    *avg_file_offset = AVG_FILE_SIZE - (*avg_file_sectors) << OFFSET_ADDR_WIDTH;
}

/**
 * @brief Obtain CURR_WRITE_DATA using CURR_WRITE_META-8/CURR_WRITE_META
 * @param sector sector 0. Reused as a sector buffer if the prev_write_meta_sector is beyond the sector 0
*/
esp_err_t __isacfs_compute_curr_write_data_loc(const u8* sector){
    esp_err_t res = ESP_OK;

    if(CURR_WRITE_META_OFFSET == 0xA && CURR_WRITE_META_SECTOR == 0x0){
        CURR_WRITE_DATA_SECTOR = DATA_START_SECTOR;
        CURR_WRITE_DATA_OFFSET = DATA_START_OFFSET;
    }
    else {
        u32 prev_write_meta_sector = CURR_WRITE_META_SECTOR;
        u32 prev_write_meta_offset = CURR_WRITE_META_OFFSET - 0x8;
        if(prev_write_meta_offset >= CURR_WRITE_META_OFFSET){
            prev_write_meta_sector--;
            //prev_write_data_offset = SECTOR_SIZE - (0x8 - (prev_write_data_offset + 0x8));
            //prev_write_meta_offset = SECTOR_SIZE + prev_write_meta_offset; // treat like `prev_write_data_offset` was negative (mod 2^32 arithmetic)
            prev_write_meta_offset += SECTOR_SIZE; //simplify
        }

        // prev_write_meta are ready 

        if(prev_write_meta_sector != 0x0){
            res = micro_sd_read_sectors((u8*)sector, prev_write_meta_sector, 0x1);
            if(res != ESP_OK){
                return res;
            }
        }

        u64 blk_5B_u64 = (((u64)(sector[prev_write_meta_offset])) << 56U) | (((u64)(sector[prev_write_meta_offset + 0x1])) << 48U);
            blk_5B_u64 |= (((u64)(sector[prev_write_meta_offset+0x2])) << 40U) | (((u64)(sector[prev_write_meta_offset + 0x3])) << 32U);
            blk_5B_u64 |= (((u64)(sector[prev_write_meta_offset + 0x4])) << 24U);

        u32 prev_write_data_sector = blk_5B_u64 >> (64U - SECTOR_ADDR_WIDTH);
        blk_5B_u64 <<= SECTOR_ADDR_WIDTH;
        u32 prev_write_data_offset = blk_5B_u64 >> (64U - OFFSET_ADDR_WIDTH);

        // prev_write_data are ready
        //add AVG_FILE_SIZE to the prev_write_data to obtain CURR_WRITE_DATA

        u32 avg_file_sectors;
        u32 avg_file_offset;
        __isacfs_deduce_avg_file_storage(&avg_file_sectors, &avg_file_offset);

        CURR_WRITE_DATA_SECTOR = prev_write_data_sector + avg_file_sectors;
        CURR_WRITE_DATA_OFFSET = prev_write_data_offset + avg_file_offset;
        
        if(CURR_WRITE_DATA_SECTOR >= SECTOR_COUNT){ // WARNING - check for bugs in the future
            CURR_WRITE_DATA_SECTOR = 0x0;
            CURR_WRITE_DATA_OFFSET = 0xA;
        }
        else if(CURR_WRITE_DATA_OFFSET >= SECTOR_SIZE){
            CURR_WRITE_DATA_OFFSET -= SECTOR_SIZE;
        }
    }
    return res;
}

/**
 * @note "init_sdcard" has to be called first
 * @note does not push forward te FUTURE_WRITE marker
*/
isacfs_err_t isacfs_init()
{
    SECTOR_COUNT = micro_sd_get_sectors_count();
    SECTOR_SIZE = micro_sd_get_sector_size();

    SECTOR_ADDR_WIDTH = micro_sd_get_sector_addr_width();
    OFFSET_ADDR_WIDTH = micro_sd_get_offset_addr_width();
    if(SECTOR_ADDR_WIDTH + OFFSET_ADDR_WIDTH > 38U){
        return isacfs_256GiB_limit_exceeded;
    }
    YEAR_DIFF_WIDTH = 38U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH;

    /* Load DATA_START and CURR_WRITE = FUTURE_WRITE */
    u8 sector0[SECTOR_SIZE];
    if(micro_sd_read_sectors(sector0, 0x0, 0x1) != ESP_OK){
        Serial.println("ERROR WHILE READING SECTOR 0 [in isacfs_init()]");
        return isacfs_fail;
    }

    u64 blk_5B_u64 = (((u64)sector0[0x0]) << 56U) | (((u64)sector0[0x1]) << 48U) | (((u64)sector0[0x2]) << 40U) | (((u64)sector0[0x3]) << 32U);
    blk_5B_u64 |= (((u64)sector0[0x4]) << 24U);

    DATA_START_SECTOR = blk_5B_u64 >> (64U - SECTOR_ADDR_WIDTH);
    blk_5B_u64 <<= SECTOR_ADDR_WIDTH;
    DATA_START_OFFSET = blk_5B_u64 >> (64U - OFFSET_ADDR_WIDTH);

    blk_5B_u64 = (((u64)sector0[0x5]) << 56U) | (((u64)sector0[0x6]) << 48U) | (((u64)sector0[0x7]) << 40U) | (((u64)sector0[0x8]) << 32U);
    blk_5B_u64 |= (((u64)sector0[0x9]) << 24U);

    FUTURE_WRITE_META_SECTOR = blk_5B_u64 >> (64U - SECTOR_ADDR_WIDTH);
    blk_5B_u64 <<= SECTOR_ADDR_WIDTH;
    FUTURE_WRITE_META_OFFSET = blk_5B_u64 >> (64U - OFFSET_ADDR_WIDTH);

    CURR_WRITE_META_SECTOR = FUTURE_WRITE_META_SECTOR;
    CURR_WRITE_META_OFFSET = FUTURE_WRITE_META_OFFSET;

    /* Calculate CURR_WRITE_DATA */
    if(__isacfs_compute_curr_write_data_loc(sector0) != ESP_OK){
        Serial.println("ERROR WHILE READING A SECTOR [in isacfs_init()]");
        return isacfs_fail;
    }

    return isacfs_ok;
}

/**
 * @brief Encode/compress "file_meta" into a 64-bit block
 * @param[in] file_meta_p in
 * @param[out] desc_8B_blk out
*/
void /*const u8**/ __isacfs_file_meta__to__desc_8B_blk(isacfs_file_meta* file_meta_p, u8* desc_8B_blk){
    //static u8 desc_8B_blk[0x8];
    static u64 desc_u64;
    desc_u64 = (u64)(file_meta_p->sector) << (64U - SECTOR_ADDR_WIDTH);
    desc_u64 |= (u64)(file_meta_p->offset) << (64U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH);
    /*
        desc_u64 |= (u64)(file_meta_p->year_diff) << (64U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH - YEAR_DIFF_WIDTH);
        64 - 38 = 26
    */
    desc_u64 |= (u64)(file_meta_p->year_diff) << 26U;
    desc_u64 |= (u64)(file_meta_p->month) << 22U; //drop 4
    desc_u64 |= (u64)(file_meta_p->day) << 17U; //drop 5
    desc_u64 |= (u64)(file_meta_p->hour) << 12U; //drop 5
    desc_u64 |= (u64)(file_meta_p->minute) << 6U; //drop 6
    desc_u64 |= (u64)(file_meta_p->second); //drop 6

    desc_8B_blk[0x0] = desc_u64 >> 56U;
    desc_8B_blk[0x1] = (desc_u64 >> 48U) & 0xFF;
    desc_8B_blk[0x2] = (desc_u64 >> 40U) & 0xFF;
    desc_8B_blk[0x3] = (desc_u64 >> 32U) & 0xFF;
    desc_8B_blk[0x4] = (desc_u64 >> 24U) & 0xFF;
    desc_8B_blk[0x5] = (desc_u64 >> 16U) & 0xFF;
    desc_8B_blk[0x6] = (desc_u64 >> 8U) & 0xFF;
    desc_8B_blk[0x7] = desc_u64 & 0xFF;

    //return desc_8B_blk;
}

/**
 * @brief Decode/expand "desc_8B_blk" into a isac_file_meta structure
*/
const isacfs_file_meta* __desc_8B_blk__to__isacfs_file_meta(const u8* desc_8B_blk){
    static isacfs_file_meta *file_meta;
    static u64 desc_u64;

    desc_u64 = (((u64)(desc_8B_blk[0])) << 56U) | (((u64)(desc_8B_blk[1])) << 48U) | (((u64)(desc_8B_blk[2])) << 40U) | (((u64)(desc_8B_blk[3])) << 32U);
    desc_u64 |= (((u64)(desc_8B_blk[4])) << 24U) | (((u64)(desc_8B_blk[5])) << 16U) | (((u64)(desc_8B_blk[6])) << 8U) | (u64)(desc_8B_blk[7]);

    file_meta->sector = desc_u64 >> (64U - SECTOR_ADDR_WIDTH);
    desc_u64 <<= SECTOR_ADDR_WIDTH;
    file_meta->offset = desc_u64 >> (64U - OFFSET_ADDR_WIDTH);
    desc_u64 <<= OFFSET_ADDR_WIDTH;
    file_meta->year_diff = desc_u64 >> (64U - YEAR_DIFF_WIDTH);
    desc_u64 <<= YEAR_DIFF_WIDTH;

    file_meta->month = desc_u64 >> 60U;
    desc_u64 <<= 4U;
    file_meta->day = desc_u64 >> 59U;
    desc_u64 <<= 5U;
    file_meta->hour = desc_u64 >> 59U;
    desc_u64 <<= 5U;
    file_meta->minute = desc_u64 >> 58U;
    desc_u64 <<= 6U;
    file_meta->second = desc_u64 >> 58U;

    return file_meta;
}

/**
 * @brief Fills all the sectors with zeroes
*/
esp_err_t __isacfs_clear_all_sectors(){
    u8 zero[SECTOR_SIZE];
    memset(zero, 0x0, SECTOR_SIZE); //neccessary?
    esp_err_t res = isacfs_ok;
    for (u32 i = 0x0; i < SECTOR_COUNT; i++){
        res = micro_sd_write_sectors(zero, i, 0x1);
        if(res != ESP_OK){
            return res;
        }
    }
    return ESP_OK;
}

float __isacfs_get_deflate_coefficient(){
    return 8.0f / (8.0f + AVG_FILE_SIZE);
}

/**
 * @brief DATA_START_SECTOR = ?, DATA_START_OFFSET = ?
*/
void __isacfs_compute_data_start(){
    //DATA_START_SECTOR = ?
    //DATA_START_OFFSET = ?

    float deflate_coef = __isacfs_get_deflate_coefficient();
    float psaddr = deflate_coef;
    psaddr *= SECTOR_COUNT;
    psaddr *= SECTOR_SIZE;
    psaddr -= deflate_coef * 16U;

    u32 addr = psaddr;
    addr += 8U - (addr % 8U);

    addr += 10U;

    //DATA_START_SECTOR = addr / SECTOR_SIZE;
    //DATA_START_OFFSET = addr - DATA_START_SECTOR * SECTOR_SIZE;
    DATA_START_SECTOR = addr >> OFFSET_ADDR_WIDTH;
    DATA_START_OFFSET = addr - (DATA_START_OFFSET << OFFSET_ADDR_WIDTH);
}

/**
 * @brief Format the card using the predefined average file size "AVG_FILE_SIZE" (in bytes)
 * @note Sector size can't be smaller than 11B
*/
esp_err_t isacfs_format(){
    esp_err_t res = ESP_OK;
    res = __isacfs_clear_all_sectors();
    if(res != ESP_OK){
        return res;
    }

    __isacfs_compute_data_start();
    FUTURE_WRITE_META_SECTOR = 0U;   /////////////////////////////////////////////
    FUTURE_WRITE_META_OFFSET = 10U; // UPDATE them for example every 1h        //
                                   /////////////////////////////////////////////
    CURR_WRITE_META_SECTOR = 0U;
    CURR_WRITE_META_OFFSET = 10U;

    CURR_WRITE_DATA_SECTOR = DATA_START_SECTOR;
    CURR_WRITE_DATA_OFFSET = DATA_START_OFFSET;

    u8 sector0[SECTOR_SIZE];
    memset(sector0, 0x0, SECTOR_SIZE); //neccessary?
    // Write data_start and future_write into the first 5B+5B of the Sector 0x0
    //38, 40/8=5
    u64 blk_5B_u64 = ((u64)DATA_START_SECTOR) << (64U - SECTOR_ADDR_WIDTH);
    blk_5B_u64 |= ((u64)DATA_START_OFFSET) << (64U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH);

    sector0[0x0] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x1] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x2] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x3] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x4] = blk_5B_u64 >> 56U;

    blk_5B_u64 = ((u64)FUTURE_WRITE_META_SECTOR) << (64U - SECTOR_ADDR_WIDTH);
    blk_5B_u64 |= ((u64)FUTURE_WRITE_META_OFFSET) << (64U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH);

    sector0[0x5] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x6] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x7] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x8] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x9] = blk_5B_u64 >> 56U;

    return micro_sd_write_sectors(sector0, 0x0, 0x1);
}

esp_err_t __isacfs_shift_future_marker(){
    esp_err_t res = ESP_OK;
    u32 sector_leap = (FILE_LEAP << 0x3) >> OFFSET_ADDR_WIDTH;
    FUTURE_WRITE_META_SECTOR += sector_leap;
    FUTURE_WRITE_META_OFFSET = (FILE_LEAP - (sector_leap << OFFSET_ADDR_WIDTH));

    if(FUTURE_WRITE_META_OFFSET >> OFFSET_ADDR_WIDTH){
        FUTURE_WRITE_META_OFFSET -= (FUTURE_WRITE_META_OFFSET >> OFFSET_ADDR_WIDTH) << OFFSET_ADDR_WIDTH;
    }

    // - and UPDATE MEMORY

    u8 sector0[SECTOR_SIZE];
    res = micro_sd_read_sectors(sector0, 0x0, 0x1);
    if(res != ESP_OK){
        return res;
    }

    u64 blk_5B_u64 = ((u64)FUTURE_WRITE_META_SECTOR) << (64U - SECTOR_ADDR_WIDTH);
    blk_5B_u64 |= ((u64)FUTURE_WRITE_META_OFFSET) << (64U - SECTOR_ADDR_WIDTH - OFFSET_ADDR_WIDTH);

    sector0[0x5] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x6] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x7] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x8] = blk_5B_u64 >> 56U;
    blk_5B_u64 <<= 0x8;
    sector0[0x9] = blk_5B_u64 >> 56U;

    return micro_sd_write_sectors(sector0, 0x0, 0x1);
}


/**
 * @note "file_meta" is supposed to have sector=UNKNOWN_SECTOR, offset=UNKNOWN_OFFSET
*/
esp_err_t isacfs_write_file(isacfs_file_meta* file_meta, u8* buffer, u32 buf_sz){
    esp_err_t res = ESP_OK;
    if(CURR_WRITE_META_SECTOR == FUTURE_WRITE_META_SECTOR && CURR_WRITE_META_OFFSET == FUTURE_WRITE_META_OFFSET){
        //shift the FUTURE_WRITE marker
        res = __isacfs_shift_future_marker();
        if(res != ESP_OK){
            return res;
        }
    }

    //obtain the sectors for the file
    file_meta->sector = CURR_WRITE_DATA_SECTOR;
    file_meta->offset = CURR_WRITE_DATA_OFFSET;

    /* write the file into the sectors */
    u8 sector[SECTOR_SIZE]; // I can't make it static :((
                                 // unless I sacrificed the auto sector size detection
                                 // and made SECTOR_SIZE predefined
    res = micro_sd_read_sectors(sector, CURR_WRITE_META_SECTOR, 0x1);
    if(res != ESP_OK){
        return res;
    }

    //    write file meta into the sectors
    __isacfs_file_meta__to__desc_8B_blk(file_meta, sector + CURR_WRITE_META_OFFSET);
    res = micro_sd_write_sectors(sector, CURR_WRITE_META_SECTOR, 0x1);
    if(res != ESP_OK){
        return res;
    }

    //    write file data into the sectors && update CURR_WRITE_DATA
    res = micro_sd_read_sectors(sector, CURR_WRITE_DATA_SECTOR, 0x1);
    if(res != ESP_OK){
        return res;
    }
    memcpy(sector + CURR_WRITE_DATA_OFFSET, 
           buffer, 
           SECTOR_SIZE-CURR_WRITE_DATA_OFFSET < buf_sz ? SECTOR_SIZE - CURR_WRITE_DATA_OFFSET : buf_sz);
    res = micro_sd_write_sectors(sector, CURR_WRITE_DATA_SECTOR, 0x1);
    if(res != ESP_OK){
        return res;
    }
    

    if(SECTOR_SIZE-CURR_WRITE_DATA_OFFSET < buf_sz){
        buf_sz -= SECTOR_SIZE - CURR_WRITE_DATA_OFFSET;
        buffer += SECTOR_SIZE - CURR_WRITE_DATA_OFFSET;

        u32 num_full_sectors = buf_sz >> OFFSET_ADDR_WIDTH;
        CURR_WRITE_DATA_SECTOR++;
        
        res = micro_sd_write_sectors(buffer, CURR_WRITE_DATA_SECTOR, 0x1);
        if(res != ESP_OK){
            return res;
        }

        buf_sz -= num_full_sectors << OFFSET_ADDR_WIDTH;
        buffer += num_full_sectors << OFFSET_ADDR_WIDTH;
        CURR_WRITE_DATA_SECTOR += num_full_sectors;

        //write last sector if anything remains in the buffer
        if(buf_sz > 0){
            res = micro_sd_read_sectors(sector, CURR_WRITE_DATA_SECTOR, 0x1);
            if(res != ESP_OK){
                return res;
            }
            memcpy(sector, buffer, buf_sz);
            res = micro_sd_write_sectors(sector, CURR_WRITE_DATA_SECTOR, 0x1);
            if(res != ESP_OK){
                return res;
            }
        }
        CURR_WRITE_DATA_OFFSET = buf_sz;
    }
    else {
        CURR_WRITE_DATA_OFFSET += buf_sz;
    }

    // UPDATE CURR_WRITE_META
    CURR_WRITE_META_OFFSET += 0x8;
    if(CURR_WRITE_META_OFFSET >= SECTOR_SIZE){
        CURR_WRITE_META_SECTOR++;
        if(CURR_WRITE_META_SECTOR >= SECTOR_COUNT){
            CURR_WRITE_META_SECTOR = 0x0;
            CURR_WRITE_META_OFFSET = 0xA;
        }
        else {
            CURR_WRITE_META_OFFSET -= SECTOR_SIZE;
        }
    }

    return res;
}

/**
 * @brief Fills "file_meta" with the sector & offset info - {{{IMPLEMENT}}} using quick search on dates
 * @returns Size of the file (in bytes)
*/
u32 isacfs_file_desc(isacfs_file_meta file_meta){

}

/**
 * Read the file based on the sector, offset and size data obtained using the "isacfs_file_desc" function
*/
void isacfs_read_file(isacfs_file_meta file_meta, void* out_buffer){
    
}

//{{{void/isacfs_file_meta get_next_file R/W}}}
