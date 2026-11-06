#ifndef CELL_RECOVERY_H
#define CELL_RECOVERY_H

/**
 * @file cell_recovery.h
 * @brief Politica de la escalera de recuperacion celular (q1-rc4).
 *
 * Modulo puro: no habla con el modem ni usa FreeRTOS. Recibe el tiempo
 * monotono en ms y la clase de falla observada, y decide que escalon toca.
 * task_communication ejecuta la accion y la registra con
 * cell_recovery_note_action(). Asi la politica se puede autoprobar con una
 * linea de tiempo simulada (cell_recovery_selftest).
 *
 * Origen: E4-01 (2026-11-05). El modem contesto AT durante un corte de 4 h
 * 40 min con 93 registros fallidos y solo se recupero al reiniciarlo con el
 * boton (< 1 min). q1-rc3 no escalaba porque el PWRKEY solo actua sin AT.
 */

#include <stdbool.h>
#include <stdint.h>

#define CELL_RECOVERY_N3_HISTORY 8U

typedef enum {
    CELL_FAIL_NONE = 0,
    CELL_FAIL_NOT_REGISTERED,  /**< AT responde; CEREG/CGREG sin registro. */
    CELL_FAIL_PDP,             /**< Registrado; el contexto PDP no activa. */
    CELL_FAIL_DATA,            /**< Registrado y PDP activo; MQTT no publica. */
} cell_fail_class_t;

typedef enum {
    CELL_ACTION_NONE = 0,
    CELL_ACTION_PROBE,          /**< Ejecutar sonda de datos y volver a decidir. */
    CELL_ACTION_HOLD_PROBE_OK,  /**< Sonda correcta: falla del broker; no tocar. */
    CELL_ACTION_N1,             /**< Radio off/on: AT+CFUN=0 / AT+CFUN=1. */
    CELL_ACTION_N2,             /**< Reinicio software del modem: AT+CFUN=1,1. */
    CELL_ACTION_N3,             /**< Apagado ordenado + PWRKEY: reinicio fisico. */
} cell_action_t;

typedef struct {
    uint32_t n1_after_ms;
    uint32_t n1_early_ms;
    uint32_t n1_min_gap_ms;
    uint32_t n2_after_ms;
    uint32_t n2_min_gap_ms;
    uint32_t n3_after_ms;
    uint32_t n3_gap_ms;
    uint32_t n3_max_gap_ms;
    uint8_t  n3_max_per_day;
    uint32_t probe_min_gap_ms;
    uint32_t probe_ok_n2_after_ms;
} cell_recovery_config_t;

typedef struct {
    cell_recovery_config_t cfg;
    uint64_t last_ok_ms;      /**< Ultima publicacion confirmada (o arranque). */
    uint64_t last_n1_ms;      /**< 0 = no ejecutado en el corte actual. */
    uint64_t last_n2_ms;
    uint64_t last_n3_ms;
    uint64_t last_probe_ms;   /**< 0 = sin sonda vigente. */
    bool     probe_ok;
    bool     hold_reported;   /**< Omision por sonda ya registrada en este corte. */
    uint32_t n3_gap_ms;       /**< Intervalo N3 vigente (se duplica si no sirve). */
    uint64_t n3_history[CELL_RECOVERY_N3_HISTORY];
    uint32_t n1_total;
    uint32_t n2_total;
    uint32_t n3_total;
    uint32_t holds_total;
} cell_recovery_t;

void cell_recovery_init(cell_recovery_t *s, const cell_recovery_config_t *cfg,
                        uint64_t now_ms);

/** Publicacion MQTT confirmada: cierra el corte y reinicia la escalera. */
void cell_recovery_note_ok(cell_recovery_t *s, uint64_t now_ms);

/**
 * Decide la accion para una falla. reg_stat es el ultimo <stat> de
 * CEREG/CGREG (-1 si se desconoce). Con CELL_ACTION_PROBE el llamador hace la
 * sonda, informa con cell_recovery_note_probe() y vuelve a llamar.
 */
cell_action_t cell_recovery_decide(cell_recovery_t *s, uint64_t now_ms,
                                   cell_fail_class_t cls, int reg_stat);

void cell_recovery_note_probe(cell_recovery_t *s, uint64_t now_ms, bool ok);

/** Registra una accion ejecutada (N1/N2/N3 o la omision por sonda). */
void cell_recovery_note_action(cell_recovery_t *s, uint64_t now_ms,
                               cell_action_t action);

/** Minutos de corte en curso (para el detalle de los eventos). */
uint32_t cell_recovery_outage_min(const cell_recovery_t *s, uint64_t now_ms);

uint8_t cell_recovery_n3_last_24h(const cell_recovery_t *s, uint64_t now_ms);

const char *cell_recovery_action_name(cell_action_t action);

/**
 * Autoprueba con una linea de tiempo simulada sobre los umbrales de
 * produccion (10/3/20/30/60/90 min, N3 cada 2 h duplicando hasta 8 h, 6 por
 * dia, sonda 5 min, N2 con sonda correcta tras 2 h). No depende de la
 * configuracion compilada.
 * @return 0 si pasa; si falla, el numero del paso que fallo.
 */
int cell_recovery_selftest(void);

#endif // CELL_RECOVERY_H
