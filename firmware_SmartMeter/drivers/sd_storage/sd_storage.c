#include "sd_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "ff.h"
#include "board_config.h"
#include "logger.h"

static const char *TAG = "sd_storage";

static bool s_mounted;
static sdmmc_card_t *s_card;

esp_err_t sd_storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    // 1) Bus SPI3_HOST dedicado (aislado del ADE9153A que vive en SPI2).
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // 4000 bytes cubren holgado transferencias FATFS (512 B por bloque).
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        LOG_ERROR(TAG, "spi_bus_initialize(SPI3) fallo: %s",
                  esp_err_to_name(err));
        return err;
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
        // No desmontamos el bus: podria haber otros dispositivos futuros
        // en SPI3. Se deja inicializado.
        return err;
    }

    s_mounted = true;

    if (s_card != NULL) {
        const uint64_t cap_mb =
            ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) /
            (1024ULL * 1024ULL);
        LOG_INFO(TAG,
                 "SD montada OK en %s: %s, %llu MB, bus SPI3 (MOSI=%d MISO=%d SCLK=%d CS=%d)",
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

esp_err_t sd_storage_append_line(const char *relpath, const char *line)
{
    if ((relpath == NULL) || (line == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char fullpath[128];
    const int n = snprintf(fullpath, sizeof(fullpath), "%s/%s",
                           SD_MOUNT_POINT, relpath);
    if ((n <= 0) || ((size_t)n >= sizeof(fullpath))) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(fullpath, "a");
    if (f == NULL) {
        LOG_WARN(TAG, "fopen(%s, 'a') fallo", fullpath);
        return ESP_FAIL;
    }

    const size_t line_len = strlen(line);
    size_t written = fwrite(line, 1U, line_len, f);
    if (written == line_len) {
        if ((line_len == 0U) || (line[line_len - 1U] != '\n')) {
            const char nl = '\n';
            (void)fwrite(&nl, 1U, 1U, f);
        }
    }
    const int close_err = fclose(f);

    if (written != line_len) {
        LOG_WARN(TAG, "escritura parcial %s: %u/%u",
                 fullpath, (unsigned)written, (unsigned)line_len);
        return ESP_FAIL;
    }
    if (close_err != 0) {
        LOG_WARN(TAG, "fclose fallo %s", fullpath);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sd_storage_get_usage(uint32_t *out_total_mb, uint32_t *out_free_mb)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    // La unidad drive "0:" corresponde al primer volumen FAT montado por
    // esp_vfs_fat_sdspi_mount (mapeo por defecto de FATFS de ESP-IDF).
    FRESULT r = f_getfree("0:", &free_clusters, &fs);
    if ((r != FR_OK) || (fs == NULL)) {
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
    return ESP_OK;
}
