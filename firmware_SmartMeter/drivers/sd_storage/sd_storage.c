#include "sd_storage.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ff.h"
#include "board_config.h"
#include "logger.h"

static const char *TAG = "sd_storage";

static bool s_mounted;
static bool s_spi_bus_ready;
static sdmmc_card_t *s_card;
static StaticSemaphore_t s_storage_mutex_buffer;
static SemaphoreHandle_t s_storage_mutex;

static esp_err_t sd_storage_build_path(const char *relpath,
                                       const char *suffix,
                                       char *out,
                                       size_t out_len)
{
    if ((relpath == NULL) || (relpath[0] == '\0') ||
        (strstr(relpath, "..") != NULL) || (out == NULL) || (out_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int n = snprintf(out, out_len, "%s/%s%s", SD_MOUNT_POINT, relpath,
                           (suffix != NULL) ? suffix : "");
    return ((n > 0) && ((size_t)n < out_len)) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool sd_storage_path_exists(const char *path)
{
    struct stat st;
    return (path != NULL) && (stat(path, &st) == 0);
}

static bool sd_storage_lock(void)
{
    return (s_storage_mutex != NULL) &&
           (xSemaphoreTake(s_storage_mutex, portMAX_DELAY) == pdTRUE);
}

static void sd_storage_unlock(void)
{
    if (s_storage_mutex != NULL) {
        (void)xSemaphoreGive(s_storage_mutex);
    }
}

esp_err_t sd_storage_init(void)
{
    if (s_storage_mutex == NULL) {
        s_storage_mutex = xSemaphoreCreateMutexStatic(&s_storage_mutex_buffer);
        if (s_storage_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_mounted) {
        return ESP_OK;
    }

    // 1) Bus SPI2_HOST compartido fisicamente con el ADE9153A. Si el ADE
    // ya inicializo el bus, ESP_ERR_INVALID_STATE es el resultado esperado;
    // FATFS agrega la SD como otro dispositivo con su propio CS.
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // 4000 bytes cubren holgado transferencias FATFS (512 B por bloque).
        .max_transfer_sz = 4000,
    };
    esp_err_t err = ESP_OK;
    if (!s_spi_bus_ready) {
        err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            LOG_ERROR(TAG, "spi_bus_initialize(SPI2 compartido) fallo: %s",
                      esp_err_to_name(err));
            return err;
        }
        /* El bus queda instalado aunque no haya tarjeta. Recordarlo evita
         * repetir spi_bus_initialize() en cada sondeo de recuperacion y el
         * mensaje de error interno "SPI bus already initialized". */
        s_spi_bus_ready = true;
    }

    // 2) Configuracion del slot SDSPI. CS manejado por el driver.
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)SD_PIN_CS;
    slot_cfg.host_id = (spi_host_device_t)SD_SPI_HOST;

    // 3) Host SDSPI (velocidad final post-init negociada).
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = (int)(SD_SPI_CLK_HZ / 1000);

    // 4) Parametros de montaje VFS+FAT.
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,  // si usuario inserto SD sin formato,
                                          // que sea explicito reformatearla.
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg,
                                  &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            LOG_ERROR(TAG, "mount fallo: tarjeta sin FAT o corrupta. "
                           "Formatear FAT32 y reintentar.");
        } else {
            LOG_ERROR(TAG, "mount fallo: %s", esp_err_to_name(err));
        }
        // No liberamos el bus compartido: el ADE9153A puede estar agregado.
        return err;
    }

    s_mounted = true;

    if (s_card != NULL) {
        const uint64_t cap_mb =
            ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) /
            (1024ULL * 1024ULL);
        LOG_INFO(TAG,
                 "SD montada OK en %s: %s, %llu MB, bus SPI2 compartido (MOSI=%d MISO=%d SCLK=%d CS=%d)",
                 SD_MOUNT_POINT,
                 s_card->cid.name,
                 (unsigned long long)cap_mb,
                 SD_PIN_MOSI, SD_PIN_MISO, SD_PIN_SCLK, SD_PIN_CS);
    }
    return ESP_OK;
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_storage_deinit(void)
{
    if (s_card == NULL) {
        s_mounted = false;
        return ESP_OK;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_mounted = false;
    s_card = NULL;
    sd_storage_unlock();
    return err;
}

void sd_storage_invalidate(void)
{
    s_mounted = false;
}

esp_err_t sd_storage_append_line(const char *relpath, const char *line)
{
    if ((relpath == NULL) || (line == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    char fullpath[128];
    if (sd_storage_build_path(relpath, NULL, fullpath, sizeof(fullpath)) != ESP_OK) {
        sd_storage_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(fullpath, "a");
    if (f == NULL) {
        LOG_WARN(TAG, "fopen(%s, 'a') fallo", fullpath);
        sd_storage_unlock();
        return ESP_FAIL;
    }

    const size_t line_len = strlen(line);
    size_t written = fwrite(line, 1U, line_len, f);
    bool newline_written = true;
    if (written == line_len) {
        if ((line_len == 0U) || (line[line_len - 1U] != '\n')) {
            const char nl = '\n';
            newline_written = (fwrite(&nl, 1U, 1U, f) == 1U);
        }
    }
    const int close_err = fclose(f);

    if ((written != line_len) || !newline_written) {
        LOG_WARN(TAG, "escritura parcial %s: %u/%u",
                 fullpath, (unsigned)written, (unsigned)line_len);
        sd_storage_unlock();
        return ESP_FAIL;
    }
    if (close_err != 0) {
        LOG_WARN(TAG, "fclose fallo %s", fullpath);
        sd_storage_unlock();
        return ESP_FAIL;
    }
    sd_storage_unlock();
    return ESP_OK;
}

esp_err_t sd_storage_drain_lines(const char *relpath,
                                 size_t max_lines,
                                 sd_line_consumer_t consumer,
                                 void *ctx,
                                 size_t *out_remaining)
{
    if ((relpath == NULL) || (consumer == NULL) || (max_lines == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    char fullpath[128];
    char tmppath[128];
    char bakpath[128];
    if (sd_storage_build_path(relpath, NULL, fullpath, sizeof(fullpath)) != ESP_OK) {
        sd_storage_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    if ((sd_storage_build_path(relpath, ".tmp", tmppath, sizeof(tmppath)) != ESP_OK) ||
        (sd_storage_build_path(relpath, ".bak", bakpath, sizeof(bakpath)) != ESP_OK)) {
        sd_storage_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    FILE *in = fopen(fullpath, "r");
    if (in == NULL) {
        if (out_remaining != NULL) {
            *out_remaining = 0U;
        }
        sd_storage_unlock();
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    // Buffer de linea estatico: las lineas son payloads SenML de /datos
    // (~400 B); 768 coincide con el buffer de publicacion de task_communication.
    static char line[1280];
    size_t consumed = 0U;
    size_t remaining = 0U;
    bool consumer_stopped = false;
    FILE *out = NULL;

    while (fgets(line, sizeof(line), in) != NULL) {
        // Quitar el '\n' final para entregar la linea limpia al consumidor.
        const size_t len = strlen(line);
        if ((len > 0U) && (line[len - 1U] == '\n')) {
            line[len - 1U] = '\0';
        } else if (!feof(in)) {
            LOG_WARN(TAG, "linea truncada en %s; backlog intacto", fullpath);
            (void)fclose(in);
            if (out != NULL) {
                (void)fclose(out);
                (void)remove(tmppath);
            }
            sd_storage_unlock();
            return ESP_ERR_INVALID_SIZE;
        }
        if (line[0] == '\0') {
            continue; // linea vacia: descartar silenciosamente
        }

        if (!consumer_stopped && (consumed < max_lines)) {
            if (consumer(line, ctx) == ESP_OK) {
                ++consumed;
                continue;
            }
            consumer_stopped = true;
        }

        // Linea no consumida: preservarla en el archivo temporal.
        if (out == NULL) {
            out = fopen(tmppath, "w");
            if (out == NULL) {
                LOG_WARN(TAG, "no se pudo crear %s - backlog intacto", tmppath);
                (void)fclose(in);
                // Lo consumido se repetira en el proximo drenado; el backend
                // tolera duplicados (mediciones idempotentes por timestamp).
                sd_storage_unlock();
                return ESP_FAIL;
            }
        }
        (void)fputs(line, out);
        (void)fputc('\n', out);
        ++remaining;
    }

    (void)fclose(in);
    if (out != NULL) {
        const int flush_err = fflush(out);
        const int output_close_err = fclose(out);
        if ((flush_err != 0) || (output_close_err != 0)) {
            sd_storage_unlock();
            return ESP_FAIL;
        }
        out = NULL;
        (void)remove(bakpath);
        if (rename(fullpath, bakpath) != 0) {
            LOG_WARN(TAG, "rename %s -> %s fallo", fullpath, bakpath);
            (void)remove(tmppath);
            sd_storage_unlock();
            return ESP_FAIL;
        }
        if (rename(tmppath, fullpath) != 0) {
            LOG_WARN(TAG, "rename %s -> %s fallo", tmppath, fullpath);
            (void)rename(bakpath, fullpath);
            sd_storage_unlock();
            return ESP_FAIL;
        }
        (void)remove(bakpath);
    } else {
        // Todo consumido: el backlog quedo vacio.
        (void)remove(fullpath);
    }

    if (consumed > 0U) {
        LOG_INFO(TAG, "backlog %s: %u lineas drenadas, %u pendientes",
                 relpath, (unsigned)consumed, (unsigned)remaining);
    }
    if (out_remaining != NULL) {
        *out_remaining = remaining;
    }
    sd_storage_unlock();
    return ESP_OK;
}

esp_err_t sd_storage_recover_file(const char *relpath)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    char main_path[128];
    char tmp_path[128];
    char bak_path[128];
    esp_err_t err = ESP_OK;
    if ((sd_storage_build_path(relpath, NULL, main_path, sizeof(main_path)) != ESP_OK) ||
        (sd_storage_build_path(relpath, ".tmp", tmp_path, sizeof(tmp_path)) != ESP_OK) ||
        (sd_storage_build_path(relpath, ".bak", bak_path, sizeof(bak_path)) != ESP_OK)) {
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }
    if (sd_storage_path_exists(main_path)) {
        (void)remove(tmp_path);
        (void)remove(bak_path);
    } else if (sd_storage_path_exists(bak_path)) {
        if (rename(bak_path, main_path) != 0) {
            err = ESP_FAIL;
        } else {
            (void)remove(tmp_path);
        }
    } else if (sd_storage_path_exists(tmp_path) &&
               (rename(tmp_path, main_path) != 0)) {
        err = ESP_FAIL;
    }
done:
    sd_storage_unlock();
    return err;
}

esp_err_t sd_storage_peek_line(const char *relpath, char *out, size_t out_len)
{
    if ((out == NULL) || (out_len < 2U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    char path[128];
    if (sd_storage_build_path(relpath, NULL, path, sizeof(path)) != ESP_OK) {
        sd_storage_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        sd_storage_unlock();
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    const bool read_ok = (fgets(out, out_len, f) != NULL);
    const bool complete = read_ok &&
        ((strchr(out, '\n') != NULL) || feof(f));
    (void)fclose(f);
    if (!read_ok || !complete) {
        out[0] = '\0';
        sd_storage_unlock();
        return read_ok ? ESP_ERR_INVALID_SIZE : ESP_ERR_NOT_FOUND;
    }
    out[strcspn(out, "\r\n")] = '\0';
    sd_storage_unlock();
    return (out[0] != '\0') ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t sd_storage_drop_consumer(const char *line, void *ctx)
{
    (void)line;
    (void)ctx;
    return ESP_OK;
}

esp_err_t sd_storage_drop_first_line(const char *relpath, size_t *out_remaining)
{
    return sd_storage_drain_lines(relpath, 1U, sd_storage_drop_consumer,
                                  NULL, out_remaining);
}

esp_err_t sd_storage_trim_oldest_lines(const char *relpath,
                                       size_t preserve_lines,
                                       size_t max_drop,
                                       size_t *out_dropped,
                                       uint64_t *out_dropped_bytes,
                                       size_t *out_remaining)
{
    if ((relpath == NULL) || (max_drop == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    char fullpath[128];
    char tmppath[128];
    char bakpath[128];
    if ((sd_storage_build_path(relpath, NULL, fullpath, sizeof(fullpath)) != ESP_OK) ||
        (sd_storage_build_path(relpath, ".tmp", tmppath, sizeof(tmppath)) != ESP_OK) ||
        (sd_storage_build_path(relpath, ".bak", bakpath, sizeof(bakpath)) != ESP_OK)) {
        sd_storage_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    FILE *in = fopen(fullpath, "r");
    if (in == NULL) {
        sd_storage_unlock();
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    FILE *out = fopen(tmppath, "w");
    if (out == NULL) {
        (void)fclose(in);
        sd_storage_unlock();
        return ESP_FAIL;
    }

    static char line[1280];
    size_t line_index = 0U;
    size_t dropped = 0U;
    size_t remaining = 0U;
    uint64_t dropped_bytes = 0U;
    esp_err_t err = ESP_OK;
    while (fgets(line, sizeof(line), in) != NULL) {
        const size_t len = strlen(line);
        const bool complete = ((len > 0U) && (line[len - 1U] == '\n')) || feof(in);
        if (!complete) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }

        const bool prefix = line_index < preserve_lines;
        const bool discard = !prefix && (dropped < max_drop);
        ++line_index;
        if (discard) {
            ++dropped;
            dropped_bytes += (uint64_t)len;
            continue;
        }
        if (!prefix) {
            ++remaining;
        }
        if (fwrite(line, 1U, len, out) != len) {
            err = ESP_FAIL;
            break;
        }
    }

    if (ferror(in) != 0) {
        err = ESP_FAIL;
    }
    (void)fclose(in);
    const int flush_err = fflush(out);
    const int close_err = fclose(out);
    if ((flush_err != 0) || (close_err != 0)) {
        err = ESP_FAIL;
    }
    out = NULL;

    if (err != ESP_OK) {
        (void)remove(tmppath);
        sd_storage_unlock();
        return err;
    }
    if (dropped == 0U) {
        (void)remove(tmppath);
    } else {
        (void)remove(bakpath);
        if (rename(fullpath, bakpath) != 0) {
            (void)remove(tmppath);
            sd_storage_unlock();
            return ESP_FAIL;
        }
        if (rename(tmppath, fullpath) != 0) {
            (void)rename(bakpath, fullpath);
            sd_storage_unlock();
            return ESP_FAIL;
        }
        (void)remove(bakpath);
    }

    if (out_dropped != NULL) {
        *out_dropped = dropped;
    }
    if (out_dropped_bytes != NULL) {
        *out_dropped_bytes = dropped_bytes;
    }
    if (out_remaining != NULL) {
        *out_remaining = remaining;
    }
    sd_storage_unlock();
    return ESP_OK;
}

esp_err_t sd_storage_remove_file(const char *relpath)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    char path[128];
    esp_err_t err = sd_storage_build_path(relpath, NULL, path, sizeof(path));
    if (err == ESP_OK) {
        if (!sd_storage_path_exists(path)) {
            err = ESP_ERR_NOT_FOUND;
        } else if (remove(path) != 0) {
            err = ESP_FAIL;
        }
    }
    sd_storage_unlock();
    return err;
}

esp_err_t sd_storage_get_file_size(const char *relpath, uint64_t *out_size)
{
    if (out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    char path[128];
    esp_err_t err = sd_storage_build_path(relpath, NULL, path, sizeof(path));
    struct stat st;
    if ((err == ESP_OK) && (stat(path, &st) != 0)) {
        err = (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if (err == ESP_OK) {
        *out_size = (uint64_t)st.st_size;
    }
    sd_storage_unlock();
    return err;
}

esp_err_t sd_storage_ensure_dir(const char *relpath)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    char path[128];
    esp_err_t err = sd_storage_build_path(relpath, NULL, path, sizeof(path));
    if ((err == ESP_OK) && (mkdir(path, 0775) != 0) && (errno != EEXIST)) {
        err = ESP_FAIL;
    }
    sd_storage_unlock();
    return err;
}

esp_err_t sd_storage_get_usage(uint32_t *out_total_mb, uint32_t *out_free_mb)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    // La unidad drive "0:" corresponde al primer volumen FAT montado por
    // esp_vfs_fat_sdspi_mount (mapeo por defecto de FATFS de ESP-IDF).
    FRESULT r = f_getfree("0:", &free_clusters, &fs);
    if ((r != FR_OK) || (fs == NULL)) {
        sd_storage_unlock();
        return ESP_FAIL;
    }
    const uint64_t sector_sz = 512ULL;  // FATFS sobre SD usa 512 B
    const uint64_t cluster_sz = (uint64_t)fs->csize * sector_sz;
    const uint64_t total_b = (uint64_t)(fs->n_fatent - 2U) * cluster_sz;
    const uint64_t free_b  = (uint64_t)free_clusters * cluster_sz;
    if (out_total_mb != NULL) {
        *out_total_mb = (uint32_t)(total_b / (1024ULL * 1024ULL));
    }
    if (out_free_mb != NULL) {
        *out_free_mb = (uint32_t)(free_b / (1024ULL * 1024ULL));
    }
    sd_storage_unlock();
    return ESP_OK;
}
