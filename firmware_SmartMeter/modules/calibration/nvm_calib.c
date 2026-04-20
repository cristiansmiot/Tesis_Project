#include "nvm_calib.h"

#include <string.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "meter_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "logger.h"

static const char *TAG = "nvm_calib";
static bool s_ready;
static nvs_handle_t s_handle;

static esp_err_t nvm_calib_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        err = ESP_OK;
    }
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        LOG_WARN(TAG, "nvs_flash requires erase (%s)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    return err;
}

bool nvm_calib_is_ready(void)
{
    return s_ready;
}

/**
 * @brief Inicializa backend NVS para constantes de calibracion.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t nvm_calib_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvm_calib_nvs_init();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(METER_CALIB_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_open(%s) failed: %s", METER_CALIB_NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

esp_err_t nvm_calib_load(nvm_calib_record_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvm_calib_record_t tmp = {0};
    size_t size = sizeof(tmp);
    esp_err_t err = nvs_get_blob(s_handle, METER_CALIB_NVS_KEY_RECORD, &tmp, &size);
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(tmp)) {
        LOG_WARN(TAG, "calibration blob size mismatch: %u", (unsigned)size);
        return ESP_FAIL;
    }
    if ((tmp.magic != NVM_CALIB_RECORD_MAGIC) || (tmp.version != NVM_CALIB_RECORD_VERSION)) {
        LOG_WARN(TAG, "calibration blob header mismatch");
        return ESP_FAIL;
    }
    *out = tmp;
    return ESP_OK;
}

esp_err_t nvm_calib_save(const nvm_calib_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvm_calib_record_t tmp = *record;
    tmp.magic = NVM_CALIB_RECORD_MAGIC;
    tmp.version = NVM_CALIB_RECORD_VERSION;

    esp_err_t err = nvs_set_blob(s_handle, METER_CALIB_NVS_KEY_RECORD, &tmp, sizeof(tmp));
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t nvm_calib_apply(const nvm_calib_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ade9153a_write_reg32(ADE9153A_REG_AIGAIN, (uint32_t)record->aigain);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AIGAIN write failed");
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_AVGAIN, (uint32_t)record->avgain);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AVGAIN write failed");
        return err;
    }
    return ESP_OK;
}

