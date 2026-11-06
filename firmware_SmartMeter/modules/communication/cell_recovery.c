#include "cell_recovery.h"

#include <stddef.h>
#include <string.h>

#define CELL_MS_PER_MIN 60000ULL
#define CELL_MS_PER_DAY (24ULL * 60ULL * CELL_MS_PER_MIN)

static uint64_t elapsed_ms(uint64_t now_ms, uint64_t then_ms)
{
    return (now_ms > then_ms) ? (now_ms - then_ms) : 0ULL;
}

static bool gap_elapsed(uint64_t now_ms, uint64_t last_ms, uint32_t gap_ms)
{
    return (last_ms == 0ULL) || (elapsed_ms(now_ms, last_ms) >= (uint64_t)gap_ms);
}

void cell_recovery_init(cell_recovery_t *s, const cell_recovery_config_t *cfg,
                        uint64_t now_ms)
{
    if ((s == NULL) || (cfg == NULL)) {
        return;
    }
    (void)memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->last_ok_ms = now_ms;
    s->n3_gap_ms = cfg->n3_gap_ms;
}

void cell_recovery_note_ok(cell_recovery_t *s, uint64_t now_ms)
{
    if (s == NULL) {
        return;
    }
    s->last_ok_ms = now_ms;
    s->last_n1_ms = 0ULL;
    s->last_n2_ms = 0ULL;
    s->last_n3_ms = 0ULL;
    s->last_probe_ms = 0ULL;
    s->probe_ok = false;
    s->hold_reported = false;
    s->n3_gap_ms = s->cfg.n3_gap_ms;
}

uint8_t cell_recovery_n3_last_24h(const cell_recovery_t *s, uint64_t now_ms)
{
    if (s == NULL) {
        return 0U;
    }
    uint8_t count = 0U;
    for (size_t i = 0U; i < CELL_RECOVERY_N3_HISTORY; ++i) {
        const uint64_t t = s->n3_history[i];
        if ((t != 0ULL) && (elapsed_ms(now_ms, t) < CELL_MS_PER_DAY)) {
            ++count;
        }
    }
    return count;
}

uint32_t cell_recovery_outage_min(const cell_recovery_t *s, uint64_t now_ms)
{
    if (s == NULL) {
        return 0U;
    }
    return (uint32_t)(elapsed_ms(now_ms, s->last_ok_ms) / CELL_MS_PER_MIN);
}

cell_action_t cell_recovery_decide(cell_recovery_t *s, uint64_t now_ms,
                                   cell_fail_class_t cls, int reg_stat)
{
    if ((s == NULL) || (cls == CELL_FAIL_NONE)) {
        return CELL_ACTION_NONE;
    }
    const cell_recovery_config_t *c = &s->cfg;
    const uint64_t outage = elapsed_ms(now_ms, s->last_ok_ms);

    const bool n3_due = (outage >= c->n3_after_ms) &&
        gap_elapsed(now_ms, s->last_n3_ms, s->n3_gap_ms) &&
        (cell_recovery_n3_last_24h(s, now_ms) < c->n3_max_per_day);
    const bool n2_due = (outage >= c->n2_after_ms) &&
        gap_elapsed(now_ms, s->last_n2_ms, c->n2_min_gap_ms);
    // CEREG 0 = no busca red; 3 = registro denegado: el modem no saldra solo.
    const bool early = (cls == CELL_FAIL_NOT_REGISTERED) &&
        ((reg_stat == 0) || (reg_stat == 3));
    const uint32_t n1_after = early ? c->n1_early_ms : c->n1_after_ms;
    const bool n1_due = (outage >= n1_after) &&
        gap_elapsed(now_ms, s->last_n1_ms, c->n1_min_gap_ms);

    const cell_action_t level = n3_due ? CELL_ACTION_N3
                              : n2_due ? CELL_ACTION_N2
                              : n1_due ? CELL_ACTION_N1
                              : CELL_ACTION_NONE;
    if (level == CELL_ACTION_NONE) {
        return CELL_ACTION_NONE;
    }

    if (cls == CELL_FAIL_DATA) {
        // Registro y PDP arriba: antes de tocar el modem se comprueba si el
        // plano de datos responde. Si responde, el problema es del broker o
        // del servidor y reiniciar el modem no ayuda.
        const bool fresh = (s->last_probe_ms != 0ULL) &&
            (elapsed_ms(now_ms, s->last_probe_ms) < (uint64_t)c->probe_min_gap_ms);
        if (!fresh) {
            return CELL_ACTION_PROBE;
        }
        if (s->probe_ok) {
            const uint32_t gap = (c->probe_ok_n2_after_ms > c->n2_min_gap_ms)
                ? c->probe_ok_n2_after_ms : c->n2_min_gap_ms;
            if ((outage >= c->probe_ok_n2_after_ms) &&
                gap_elapsed(now_ms, s->last_n2_ms, gap)) {
                return CELL_ACTION_N2;
            }
            return CELL_ACTION_HOLD_PROBE_OK;
        }
    }
    return level;
}

void cell_recovery_note_probe(cell_recovery_t *s, uint64_t now_ms, bool ok)
{
    if (s == NULL) {
        return;
    }
    s->last_probe_ms = (now_ms != 0ULL) ? now_ms : 1ULL;
    s->probe_ok = ok;
}

static void push_n3(cell_recovery_t *s, uint64_t now_ms)
{
    size_t oldest = 0U;
    for (size_t i = 1U; i < CELL_RECOVERY_N3_HISTORY; ++i) {
        if (s->n3_history[i] < s->n3_history[oldest]) {
            oldest = i;
        }
    }
    s->n3_history[oldest] = now_ms;
}

void cell_recovery_note_action(cell_recovery_t *s, uint64_t now_ms,
                               cell_action_t action)
{
    if (s == NULL) {
        return;
    }
    switch (action) {
        case CELL_ACTION_N3:
            if (s->last_n3_ms != 0ULL) {
                // El N3 anterior de este corte no lo resolvio: espaciar.
                const uint64_t doubled = 2ULL * (uint64_t)s->n3_gap_ms;
                s->n3_gap_ms = (doubled > s->cfg.n3_max_gap_ms)
                    ? s->cfg.n3_max_gap_ms : (uint32_t)doubled;
            }
            s->last_n3_ms = now_ms;
            s->last_n2_ms = now_ms;
            s->last_n1_ms = now_ms;
            push_n3(s, now_ms);
            ++s->n3_total;
            break;
        case CELL_ACTION_N2:
            s->last_n2_ms = now_ms;
            s->last_n1_ms = now_ms;
            ++s->n2_total;
            break;
        case CELL_ACTION_N1:
            s->last_n1_ms = now_ms;
            ++s->n1_total;
            break;
        case CELL_ACTION_HOLD_PROBE_OK:
            if (!s->hold_reported) {
                s->hold_reported = true;
                ++s->holds_total;
            }
            return;
        default:
            return;
    }
    // El estado del modem cambio: la sonda previa ya no vale.
    s->last_probe_ms = 0ULL;
    s->probe_ok = false;
    s->hold_reported = false;
}

const char *cell_recovery_action_name(cell_action_t action)
{
    switch (action) {
        case CELL_ACTION_NONE:          return "NONE";
        case CELL_ACTION_PROBE:         return "PROBE";
        case CELL_ACTION_HOLD_PROBE_OK: return "HOLD";
        case CELL_ACTION_N1:            return "N1";
        case CELL_ACTION_N2:            return "N2";
        case CELL_ACTION_N3:            return "N3";
        default:                        return "?";
    }
}

/* ── Autoprueba ───────────────────────────────────────────────────────── */

#define T(min) (1000ULL + (uint64_t)((min) * (double)CELL_MS_PER_MIN))
#define EXPECT(step, cond) do { if (!(cond)) { return (step); } } while (0)

int cell_recovery_selftest(void)
{
    const cell_recovery_config_t cfg = {
        .n1_after_ms = 600000U,
        .n1_early_ms = 180000U,
        .n1_min_gap_ms = 1200000U,
        .n2_after_ms = 1800000U,
        .n2_min_gap_ms = 3600000U,
        .n3_after_ms = 5400000U,
        .n3_gap_ms = 7200000U,
        .n3_max_gap_ms = 28800000U,
        .n3_max_per_day = 6U,
        .probe_min_gap_ms = 300000U,
        .probe_ok_n2_after_ms = 7200000U,
    };
    cell_recovery_t s;
    cell_action_t a;

    /* Corte sin registro, como E4-01. */
    cell_recovery_init(&s, &cfg, T(0));
    EXPECT(1, cell_recovery_decide(&s, T(2), CELL_FAIL_NOT_REGISTERED, 0) == CELL_ACTION_NONE);
    a = cell_recovery_decide(&s, T(3.5), CELL_FAIL_NOT_REGISTERED, 0);
    EXPECT(2, a == CELL_ACTION_N1);          /* CEREG 0: N1 adelantado */
    cell_recovery_note_action(&s, T(3.5), a);
    EXPECT(3, cell_recovery_decide(&s, T(10), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_NONE);
    a = cell_recovery_decide(&s, T(24), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(4, a == CELL_ACTION_N1);
    cell_recovery_note_action(&s, T(24), a);
    a = cell_recovery_decide(&s, T(30), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(5, a == CELL_ACTION_N2);
    cell_recovery_note_action(&s, T(30), a);
    EXPECT(6, cell_recovery_decide(&s, T(45), CELL_FAIL_PDP, 1) == CELL_ACTION_NONE);
    a = cell_recovery_decide(&s, T(50), CELL_FAIL_PDP, 1);
    EXPECT(7, a == CELL_ACTION_N1);
    cell_recovery_note_action(&s, T(50), a);
    a = cell_recovery_decide(&s, T(90), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(8, a == CELL_ACTION_N3);
    cell_recovery_note_action(&s, T(90), a);
    EXPECT(9, cell_recovery_decide(&s, T(100), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_NONE);
    a = cell_recovery_decide(&s, T(110), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(10, a == CELL_ACTION_N1);
    cell_recovery_note_action(&s, T(110), a);
    a = cell_recovery_decide(&s, T(150), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(11, a == CELL_ACTION_N2);
    cell_recovery_note_action(&s, T(150), a);
    a = cell_recovery_decide(&s, T(170), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(12, a == CELL_ACTION_N1);
    cell_recovery_note_action(&s, T(170), a);
    a = cell_recovery_decide(&s, T(190), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(13, a == CELL_ACTION_N1);
    cell_recovery_note_action(&s, T(190), a);
    a = cell_recovery_decide(&s, T(210), CELL_FAIL_NOT_REGISTERED, 2);
    EXPECT(14, a == CELL_ACTION_N3);         /* 2 h despues del primer N3 */
    cell_recovery_note_action(&s, T(210), a);
    EXPECT(15, s.n3_gap_ms == 14400000U);    /* segundo N3: intervalo 4 h */
    EXPECT(16, cell_recovery_decide(&s, T(211), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_NONE);
    EXPECT(17, (s.n1_total == 6U) && (s.n2_total == 2U) && (s.n3_total == 2U));

    /* Publicacion confirmada: la escalera vuelve a cero. */
    cell_recovery_note_ok(&s, T(215));
    EXPECT(18, s.n3_gap_ms == 7200000U);
    EXPECT(19, cell_recovery_decide(&s, T(220), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_NONE);

    /* Corte del broker: la sonda responde y no se toca el modem. */
    EXPECT(20, cell_recovery_decide(&s, T(226), CELL_FAIL_DATA, 1) == CELL_ACTION_PROBE);
    cell_recovery_note_probe(&s, T(226), true);
    a = cell_recovery_decide(&s, T(226), CELL_FAIL_DATA, 1);
    EXPECT(21, a == CELL_ACTION_HOLD_PROBE_OK);
    cell_recovery_note_action(&s, T(226), a);
    a = cell_recovery_decide(&s, T(230), CELL_FAIL_DATA, 1);
    EXPECT(22, a == CELL_ACTION_HOLD_PROBE_OK);  /* sonda vigente: sin repetir */
    cell_recovery_note_action(&s, T(230), a);
    EXPECT(23, s.holds_total == 1U);             /* se registra una vez por corte */
    EXPECT(24, cell_recovery_decide(&s, T(232), CELL_FAIL_DATA, 1) == CELL_ACTION_PROBE);
    cell_recovery_note_probe(&s, T(232), true);
    EXPECT(25, cell_recovery_decide(&s, T(232), CELL_FAIL_DATA, 1) == CELL_ACTION_HOLD_PROBE_OK);
    EXPECT(26, cell_recovery_decide(&s, T(336), CELL_FAIL_DATA, 1) == CELL_ACTION_PROBE);
    cell_recovery_note_probe(&s, T(336), true);
    a = cell_recovery_decide(&s, T(336), CELL_FAIL_DATA, 1);
    EXPECT(27, a == CELL_ACTION_N2);             /* > 2 h con sonda correcta */
    cell_recovery_note_action(&s, T(336), a);

    /* Registro y PDP arriba pero sin datos (modem trabado): escala. */
    cell_recovery_note_ok(&s, T(400));
    EXPECT(28, cell_recovery_decide(&s, T(411), CELL_FAIL_DATA, 1) == CELL_ACTION_PROBE);
    cell_recovery_note_probe(&s, T(411), false);
    EXPECT(29, cell_recovery_decide(&s, T(411), CELL_FAIL_DATA, 1) == CELL_ACTION_N1);

    /* Tope de 6 N3 por 24 h, aun en cortes distintos. */
    cell_recovery_init(&s, &cfg, T(1000));
    for (unsigned i = 0U; i < 6U; ++i) {
        cell_recovery_note_ok(&s, T(1000 + (i * 100U)));
        a = cell_recovery_decide(&s, T(1090 + (i * 100U)), CELL_FAIL_NOT_REGISTERED, 2);
        EXPECT(30, a == CELL_ACTION_N3);
        cell_recovery_note_action(&s, T(1090 + (i * 100U)), a);
    }
    cell_recovery_note_ok(&s, T(1600));
    EXPECT(31, cell_recovery_decide(&s, T(1690), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_N2);
    EXPECT(32, cell_recovery_n3_last_24h(&s, T(1690)) == 6U);
    EXPECT(33, cell_recovery_decide(&s, T(1090 + 1441), CELL_FAIL_NOT_REGISTERED, 2) == CELL_ACTION_N3);
    return 0;
}
