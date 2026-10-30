#ifndef SIM7080G_RADIO_H
#define SIM7080G_RADIO_H

#include <stdbool.h>

typedef struct {
    bool valid;
    float rsrp_dbm;
    float rsrq_db;
    float rssi_dbm;
    float rssnr_raw;
    float sinr_db;
} sim7080g_radio_t;

/* SIMCom AT Manual V1.07, 5.2.19: LTE fields 10..13, SINR=2*RSSNR-20.
 * Reject other RATs, absent fields and undocumented encodings. */
bool sim7080g_radio_parse_cpsi(const char *response, sim7080g_radio_t *out);

#endif
