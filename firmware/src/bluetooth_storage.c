/* SDK EasyFlash ABI backed by the application's single LittleFS mount.
 * Never mount the same flash partition twice. Keys are encoded as hex paths. */
#include "dongle/bluetooth.h"
#include "easyflash.h"
#include <string.h>
#include <stdio.h>

static dongle_settings_store_t *storage;
void dongle_bluetooth_storage_bind(dongle_settings_store_t *store) { storage = store; }
EfErrCode __wrap_easyflash_init(void)
{
    return storage && storage->initialized ? EF_NO_ERR : EF_ENV_INIT_FAILED;
}
static bool path_for(const char *key, char path[132])
{
    static const char hex[] = "0123456789abcdef";
    if (!key || strlen(key) >= 64) return false;
    memcpy(path, "bt-", 3);
    size_t n = 3;
    for (; *key; ++key) {
        path[n++] = hex[(uint8_t)*key >> 4];
        path[n++] = hex[(uint8_t)*key & 15];
    }
    path[n] = 0;
    return true;
}
size_t __wrap_ef_get_env_blob_offset(const char *key, void *value, size_t capacity,
                             size_t *saved, int offset)
{
    char path[132];
    if (saved) *saved = 0;
    if (easyflash_init() || offset < 0 || !path_for(key, path)) return 0;
    xSemaphoreTake(storage->mutex, portMAX_DELAY);
    lfs_file_t file;
    lfs_ssize_t bytes = 0;
    if (!lfs_file_open(storage->filesystem, &file, path, LFS_O_RDONLY)) {
        lfs_soff_t length = lfs_file_size(storage->filesystem, &file);
        if (saved && length >= 0) *saved = (size_t)length;
        if (value && capacity && lfs_file_seek(storage->filesystem, &file, offset, LFS_SEEK_SET) >= 0)
            bytes = lfs_file_read(storage->filesystem, &file, value, capacity);
        if (lfs_file_close(storage->filesystem, &file)) bytes = 0;
    }
    xSemaphoreGive(storage->mutex);
    return bytes > 0 ? (size_t)bytes : 0;
}
size_t __wrap_ef_get_env_blob(const char *key, void *value, size_t size, size_t *saved)
{ return ef_get_env_blob_offset(key, value, size, saved, 0); }
EfErrCode __wrap_ef_set_env_blob(const char *key, const void *value, size_t size)
{
    char path[132];
    if (easyflash_init() || !path_for(key, path) || !value || !size) return EF_ENV_ARG_ERR;
    xSemaphoreTake(storage->mutex, portMAX_DELAY);
    lfs_file_t file;
    int err = lfs_file_open(storage->filesystem, &file, "bt.tmp", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (!err) {
        if (lfs_file_write(storage->filesystem, &file, value, size) != (lfs_ssize_t)size) err = -1;
        if (lfs_file_sync(storage->filesystem, &file)) err = -1;
        if (lfs_file_close(storage->filesystem, &file)) err = -1;
        if (!err) err = lfs_rename(storage->filesystem, "bt.tmp", path);
    }
    xSemaphoreGive(storage->mutex);
    return err ? EF_WRITE_ERR : EF_NO_ERR;
}
EfErrCode __wrap_ef_del_env(const char *key)
{
    char path[132];
    if (easyflash_init() || !path_for(key, path)) return EF_ENV_ARG_ERR;
    xSemaphoreTake(storage->mutex, portMAX_DELAY);
    int err = lfs_remove(storage->filesystem, path);
    xSemaphoreGive(storage->mutex);
    return !err || err == LFS_ERR_NOENT ? EF_NO_ERR : EF_WRITE_ERR;
}
