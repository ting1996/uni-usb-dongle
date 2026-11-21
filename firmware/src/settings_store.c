#include "dongle/settings_store.h"

#include "dongle/settings_codec.h"

#include <string.h>

#include "bflb_flash.h"
#include "bflb_mtd.h"
#define DBG_TAG "STORE"
#include "log.h"
#include "lfs.h"

#define SETTINGS_PARTITION_NAME "PSM"
#define SETTINGS_FILE_NAME "dongle.cfg"
#define SETTINGS_TEMP_FILE_NAME "dongle.tmp"
#define SETTINGS_BLOCK_SIZE 4096U
#define SETTINGS_READ_SIZE 256U
#define SETTINGS_PROGRAM_SIZE 256U
#define SETTINGS_CACHE_SIZE 512U
#define SETTINGS_LOOKAHEAD_SIZE 256U

#if defined(LFS_THREADSAFE)
static int settings_lock(const struct lfs_config *config)
{
    const struct lfs_context *context = config->context;
#if configUSE_RECURSIVE_MUTEXES
    return xSemaphoreTakeRecursive(context->fs_giant_lock, portMAX_DELAY) == pdTRUE
               ? 0
               : LFS_ERR_IO;
#else
    return xSemaphoreTake(context->fs_giant_lock, portMAX_DELAY) == pdTRUE
               ? 0
               : LFS_ERR_IO;
#endif
}

static int settings_unlock(const struct lfs_config *config)
{
    const struct lfs_context *context = config->context;
#if configUSE_RECURSIVE_MUTEXES
    return xSemaphoreGiveRecursive(context->fs_giant_lock) == pdTRUE ? 0 : LFS_ERR_IO;
#else
    return xSemaphoreGive(context->fs_giant_lock) == pdTRUE ? 0 : LFS_ERR_IO;
#endif
}
#endif

typedef enum {
    PARTITION_ERASED,
    PARTITION_NOT_ERASED,
    PARTITION_READ_ERROR
} partition_state_t;

static partition_state_t partition_state(uint32_t address, uint32_t size)
{
    uint8_t buffer[128];

    for (uint32_t offset = 0; offset < size;) {
        const uint32_t remaining = size - offset;
        const size_t chunk_size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (bflb_flash_read(address + offset, buffer, (uint32_t)chunk_size) != 0) {
            return PARTITION_READ_ERROR;
        }
        for (size_t i = 0; i < chunk_size; ++i) {
            if (buffer[i] != UINT8_MAX) {
                return PARTITION_NOT_ERASED;
            }
        }
        offset += (uint32_t)chunk_size;
    }

    return PARTITION_ERASED;
}

static dongle_store_result_t store_take(dongle_settings_store_t *store)
{
    if (store == NULL || !store->initialized || store->mutex == NULL) {
        return DONGLE_STORE_INVALID_ARGUMENT;
    }
    return xSemaphoreTake(store->mutex, portMAX_DELAY) == pdTRUE
               ? DONGLE_STORE_OK
               : DONGLE_STORE_IO_ERROR;
}

static dongle_store_result_t store_give(
    dongle_settings_store_t *store,
    dongle_store_result_t result)
{
    if (xSemaphoreGive(store->mutex) != pdTRUE) {
        return DONGLE_STORE_IO_ERROR;
    }
    return result;
}

dongle_store_result_t dongle_settings_store_init(dongle_settings_store_t *store)
{
    if (store == NULL) {
        return DONGLE_STORE_INVALID_ARGUMENT;
    }

    memset(store, 0, sizeof(*store));
    store->mutex = xSemaphoreCreateMutex();
    if (store->mutex == NULL) {
        return DONGLE_STORE_IO_ERROR;
    }

    bflb_mtd_init();
    bflb_mtd_handle_t partition;
    if (bflb_mtd_open(SETTINGS_PARTITION_NAME, &partition,
                      BFLB_MTD_OPEN_FLAG_BUSADDR) < 0) {
        vSemaphoreDelete(store->mutex);
        store->mutex = NULL;
        return DONGLE_STORE_IO_ERROR;
    }

    bflb_mtd_info_t partition_info;
    memset(&partition_info, 0, sizeof(partition_info));
    const int info_result = bflb_mtd_info(partition, &partition_info);
    const int close_result = bflb_mtd_close(partition);
    if (info_result != 0 || close_result != 0) {
        vSemaphoreDelete(store->mutex);
        store->mutex = NULL;
        return DONGLE_STORE_IO_ERROR;
    }

    if (partition_info.size == 0U ||
        partition_info.size % SETTINGS_BLOCK_SIZE != 0U) {
        vSemaphoreDelete(store->mutex);
        store->mutex = NULL;
        return DONGLE_STORE_IO_ERROR;
    }

    store->filesystem_context.partition_name = SETTINGS_PARTITION_NAME;
    store->filesystem_context.flash_addr = partition_info.offset;
    store->filesystem_config.read_size = SETTINGS_READ_SIZE;
    store->filesystem_config.prog_size = SETTINGS_PROGRAM_SIZE;
    store->filesystem_config.block_size = SETTINGS_BLOCK_SIZE;
    store->filesystem_config.block_count =
        partition_info.size / SETTINGS_BLOCK_SIZE;
    store->filesystem_config.block_cycles = 500;
    store->filesystem_config.cache_size = SETTINGS_CACHE_SIZE;
    store->filesystem_config.lookahead_size = SETTINGS_LOOKAHEAD_SIZE;
    store->filesystem_config.context = &store->filesystem_context;
    store->filesystem_config.read = lfs_xip_flash_read;
    store->filesystem_config.prog = lfs_xip_flash_prog;
    store->filesystem_config.erase = lfs_xip_flash_erase;
    store->filesystem_config.sync = lfs_xip_flash_sync;

#if defined(LFS_THREADSAFE)
#if configUSE_RECURSIVE_MUTEXES
    store->filesystem_context.fs_giant_lock = xSemaphoreCreateRecursiveMutex();
#else
    store->filesystem_context.fs_giant_lock = xSemaphoreCreateMutex();
#endif
    if (store->filesystem_context.fs_giant_lock == NULL) {
        vSemaphoreDelete(store->mutex);
        store->mutex = NULL;
        return DONGLE_STORE_IO_ERROR;
    }
    store->filesystem_config.lock = settings_lock;
    store->filesystem_config.unlock = settings_unlock;
#endif

    int result = lfs_mount(&store->filesystem_context.lfs,
                           &store->filesystem_config);
    if (result != 0) {
        const partition_state_t state =
            partition_state(partition_info.offset, partition_info.size);
        if (state != PARTITION_ERASED) {
            if (dongle_settings_store_close(store) != DONGLE_STORE_OK) {
                LOG_E("Settings-store cleanup failed after mount error\r\n");
            }
            if (state == PARTITION_READ_ERROR) {
                return DONGLE_STORE_IO_ERROR;
            }
            return DONGLE_STORE_CORRUPT;
        }

        result = lfs_format(&store->filesystem_context.lfs,
                            &store->filesystem_config);
        if (result == 0) {
            result = lfs_mount(&store->filesystem_context.lfs,
                               &store->filesystem_config);
        }
        if (result != 0) {
            if (dongle_settings_store_close(store) != DONGLE_STORE_OK) {
                LOG_E("Settings-store cleanup failed after format error\r\n");
            }
            return DONGLE_STORE_IO_ERROR;
        }
    }

    store->filesystem = &store->filesystem_context.lfs;
    store->initialized = true;
    return DONGLE_STORE_OK;
}

dongle_store_result_t dongle_settings_store_load(
    dongle_settings_store_t *store,
    dongle_device_catalog_t *catalog)
{
    if (catalog == NULL) {
        return DONGLE_STORE_INVALID_ARGUMENT;
    }
    dongle_store_result_t result = store_take(store);
    if (result != DONGLE_STORE_OK) {
        return result;
    }

    lfs_file_t file;
    int filesystem_result = lfs_file_open(store->filesystem, &file, SETTINGS_FILE_NAME,
                                          LFS_O_RDONLY);
    if (filesystem_result == LFS_ERR_NOENT) {
        return store_give(store, DONGLE_STORE_NOT_FOUND);
    }
    if (filesystem_result != 0) {
        return store_give(store, DONGLE_STORE_IO_ERROR);
    }

    uint8_t buffer[DONGLE_SETTINGS_BLOB_SIZE];
    lfs_ssize_t bytes_read = lfs_file_read(store->filesystem, &file, buffer, sizeof(buffer));
    uint8_t trailing_byte;
    lfs_ssize_t trailing_read = bytes_read == (lfs_ssize_t)sizeof(buffer)
                                    ? lfs_file_read(store->filesystem, &file,
                                                    &trailing_byte, sizeof(trailing_byte))
                                    : 0;
    int close_result = lfs_file_close(store->filesystem, &file);
    if (bytes_read < 0 || trailing_read < 0 || close_result != 0) {
        return store_give(store, DONGLE_STORE_IO_ERROR);
    }
    if (bytes_read != (lfs_ssize_t)sizeof(buffer) || trailing_read != 0) {
        return store_give(store, DONGLE_STORE_CORRUPT);
    }

    result = dongle_settings_decode(buffer, sizeof(buffer), catalog) ==
                     DONGLE_SETTINGS_CODEC_OK
                 ? DONGLE_STORE_OK
                 : DONGLE_STORE_CORRUPT;
    return store_give(store, result);
}

dongle_store_result_t dongle_settings_store_save(
    dongle_settings_store_t *store,
    const dongle_device_catalog_t *catalog)
{
    uint8_t buffer[DONGLE_SETTINGS_BLOB_SIZE];
    if (dongle_settings_encode(catalog, buffer, sizeof(buffer)) !=
        DONGLE_SETTINGS_CODEC_OK) {
        return DONGLE_STORE_INVALID_ARGUMENT;
    }

    dongle_store_result_t result = store_take(store);
    if (result != DONGLE_STORE_OK) {
        return result;
    }

    lfs_file_t file;
    int filesystem_result = lfs_file_open(store->filesystem, &file,
                                          SETTINGS_TEMP_FILE_NAME,
                                          LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (filesystem_result != 0) {
        return store_give(store, DONGLE_STORE_IO_ERROR);
    }

    lfs_ssize_t bytes_written =
        lfs_file_write(store->filesystem, &file, buffer, sizeof(buffer));
    filesystem_result = bytes_written == (lfs_ssize_t)sizeof(buffer)
                            ? lfs_file_sync(store->filesystem, &file)
                            : LFS_ERR_IO;
    int close_result = lfs_file_close(store->filesystem, &file);
    if (filesystem_result != 0 || close_result != 0) {
        return store_give(store, DONGLE_STORE_IO_ERROR);
    }

    filesystem_result = lfs_rename(store->filesystem, SETTINGS_TEMP_FILE_NAME,
                                    SETTINGS_FILE_NAME);
    return store_give(store, filesystem_result == 0
                                 ? DONGLE_STORE_OK
                                 : DONGLE_STORE_IO_ERROR);
}

dongle_store_result_t dongle_settings_store_close(dongle_settings_store_t *store)
{
    if (store == NULL || store->mutex == NULL) {
        return DONGLE_STORE_INVALID_ARGUMENT;
    }

    dongle_store_result_t result = DONGLE_STORE_OK;
    if (store->filesystem != NULL) {
        result = lfs_unmount(store->filesystem) == 0
                     ? DONGLE_STORE_OK
                     : DONGLE_STORE_IO_ERROR;
    }
#if defined(LFS_THREADSAFE)
    if (store->filesystem_context.fs_giant_lock != NULL) {
        vSemaphoreDelete(store->filesystem_context.fs_giant_lock);
    }
#endif
    vSemaphoreDelete(store->mutex);
    memset(store, 0, sizeof(*store));
    return result;
}
