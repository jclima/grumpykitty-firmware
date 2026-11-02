/**
 * @file  lv_port_fs.cpp
 * @brief LVGL filesystem driver — SD_MMC (drive letter 'S:')
 */

#include "lv_port_fs.h"
#include <SD_MMC.h>
#include <Arduino.h>

/*********************
 *      DEFINES
 *********************/
#define SDCARD_MOUNT_POINT "/sdcard"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw);
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p);
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path);
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn);
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_fs_fatfs_init(void)
{
    /*---------------------------------------------------
     * Register the file system interface in LVGL
     *--------------------------------------------------*/

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    fs_drv.letter = 'S';
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = fs_write;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;
    fs_drv.dir_close_cb = fs_dir_close;
    fs_drv.dir_open_cb = fs_dir_open;
    fs_drv.dir_read_cb = fs_dir_read;

    lv_fs_drv_register(&fs_drv);
    
    Serial.println("[LVGL FS] SD_MMC filesystem driver registered");
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Open a file using SD_MMC
 * Path format: S:/apps/badusb/icon.png -> /apps/badusb/icon.png
 * Note: SD_MMC.begin() registers "/sdcard" mount point, but SD_MMC.open()
 *       accesses relative to SD card root, so no mount point prefix needed.
 */
static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    
    // SD_MMC library uses paths relative to SD card root (no /sdcard prefix)
    String fullPath = String("/") + String(path);
    
    const char* openMode = "r";
    if (mode == LV_FS_MODE_WR) {
        openMode = "w";
    } else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) {
        openMode = "r+";
    }
    
    File* file = new File();
    if (file == NULL) return NULL;
    
    *file = SD_MMC.open(fullPath.c_str(), openMode);
    if (!(*file)) {
        delete file;
        return NULL;
    }
    
    return file;
}

/**
 * Close an opened file
 */
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    File* file = (File*)file_p;
    if (file) {
        file->close();
        delete file;
    }
    return LV_FS_RES_OK;
}

/**
 * Read data from an opened file
 */
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
    File* file = (File*)file_p;
    if (!file || !(*file)) return LV_FS_RES_UNKNOWN;
    
    *br = file->read((uint8_t*)buf, btr);
    return LV_FS_RES_OK;
}

/**
 * Write into a file
 */
static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    LV_UNUSED(drv);
    File* file = (File*)file_p;
    if (!file || !(*file)) return LV_FS_RES_UNKNOWN;
    
    *bw = file->write((const uint8_t*)buf, btw);
    return LV_FS_RES_OK;
}

/**
 * Set the read write pointer
 */
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    File* file = (File*)file_p;
    if (!file || !(*file)) return LV_FS_RES_UNKNOWN;
    
    SeekMode mode = SeekSet;
    if (whence == LV_FS_SEEK_CUR) mode = SeekCur;
    else if (whence == LV_FS_SEEK_END) mode = SeekEnd;
    
    file->seek(pos, mode);
    return LV_FS_RES_OK;
}

/**
 * Give the position of the read write pointer
 */
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    File* file = (File*)file_p;
    if (!file || !(*file)) return LV_FS_RES_UNKNOWN;
    
    *pos_p = file->position();
    return LV_FS_RES_OK;
}

/**
 * Initialize a directory for reading
 */
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path)
{
    LV_UNUSED(drv);
    
    String fullPath = String("/") + String(path);
    
    File* dir = new File();
    if (dir == NULL) return NULL;
    
    *dir = SD_MMC.open(fullPath.c_str());
    if (!(*dir) || !dir->isDirectory()) {
        delete dir;
        return NULL;
    }
    
    return dir;
}

/**
 * Read the next filename from a directory
 */
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn)
{
    LV_UNUSED(drv);
    File* dir = (File*)dir_p;
    fn[0] = '\0';
    
    if (!dir || !(*dir)) return LV_FS_RES_UNKNOWN;
    
    File entry = dir->openNextFile();
    if (!entry) {
        return LV_FS_RES_OK;  // End of directory
    }
    
    /* LVGL 8.3 passes no buffer size; callers provide at least
     * LV_FS_MAX_FN_LENGTH bytes. Truncate to that. */
    if (entry.isDirectory()) {
        fn[0] = '/';
        strncpy(&fn[1], entry.name(), LV_FS_MAX_FN_LENGTH - 2);
        fn[LV_FS_MAX_FN_LENGTH - 1] = '\0';
    } else {
        strncpy(fn, entry.name(), LV_FS_MAX_FN_LENGTH - 1);
        fn[LV_FS_MAX_FN_LENGTH - 1] = '\0';
    }
    
    entry.close();
    return LV_FS_RES_OK;
}

/**
 * Close the directory reading
 */
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p)
{
    LV_UNUSED(drv);
    File* dir = (File*)dir_p;
    if (dir) {
        dir->close();
        delete dir;
    }
    return LV_FS_RES_OK;
}
