#include "sim7080g_radio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool sim7080g_radio_parse_cpsi(const char *response, sim7080g_radio_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = (sim7080g_radio_t){0};
    if (response == NULL) {
        return false;
    }
    const char *start = strstr(response, "+CPSI:");
    if (start == NULL) {
        return false;
    }
    start += strlen("+CPSI:");
    while (*start == ' ') { ++start; }
    if ((strncmp(start, "LTE CAT-M1,", 11U) != 0) &&
        (strncmp(start, "LTE NB-IOT,", 11U) != 0)) {
        return false;
    }
    float fields[4];
    for (unsigned field = 0; field < 14U; ++field) {
        while (*start == ' ') { ++start; }
        if ((*start == '\0') || (*start == '\r') || (*start == '\n')) {
            return false;
        }
        const char *end = strchr(start, ',');
        const char *line_end = strpbrk(start, "\r\n");
        if ((end == NULL) || ((line_end != NULL) && (line_end < end))) {
            end = line_end != NULL ? line_end : start + strlen(start);
        }
        if (field == 1U && ((end - start) != 6 || strncmp(start, "Online", 6U) != 0)) {
            return false;
        }
        if (field >= 10U) {
            char *parsed_end;
            const float value = strtof(start, &parsed_end);
            const bool consumed = parsed_end != start;
            while ((*parsed_end == ' ') || (*parsed_end == '\t')) { ++parsed_end; }
            if (!consumed || (parsed_end != end) || !isfinite(value)) {
                return false;
            }
            fields[field - 10U] = value;
        }
        if ((field < 13U) && (*end != ',')) {
            return false;
        }
        start = end + (*end == ',' ? 1U : 0U);
    }
    if ((fields[0] < -40.0f) || (fields[0] > 0.0f) ||
        (fields[1] < -156.0f) || (fields[1] > -20.0f) ||
        (fields[2] < -150.0f) || (fields[2] > 0.0f) ||
        (fields[3] < 0.0f) || (fields[3] > 25.0f)) {
        return false;
    }
    out->rsrq_db = fields[0];
    out->rsrp_dbm = fields[1];
    out->rssi_dbm = fields[2];
    out->rssnr_raw = fields[3];
    out->sinr_db = 2.0f * fields[3] - 20.0f;
    out->valid = true;
    return true;
}
