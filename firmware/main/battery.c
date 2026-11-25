/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "battery.h"

#include <math.h>

typedef struct {
    float v;
    float pct;
} pt_t;

/* Approximate REST-voltage curves (descending voltage). Generic cell data, not
 * measured on this hardware; refine with a logged discharge of the real cell. */
static const pt_t LIPO[] = {
    {4.20f, 100}, {4.00f, 80}, {3.85f, 60}, {3.75f, 40}, {3.65f, 20}, {3.50f, 5}, {3.30f, 0},
};
static const pt_t LIFEPO4[] = {
    {3.40f, 100}, {3.32f, 80}, {3.30f, 60}, {3.27f, 40}, {3.22f, 20}, {3.10f, 5}, {3.00f, 0},
};

bool battery_present(float volts) { return isfinite(volts) && volts >= BATTERY_ABSENT_VOLTS; }

const char *battery_type_name(int type) { return type == BATTERY_LIFEPO4 ? "LiFePO4" : "LiPo"; }

int board_battery_percent(int type, float volts)
{
    if (!battery_present(volts)) {
        return -1;
    }
    const pt_t *t = type == BATTERY_LIFEPO4 ? LIFEPO4 : LIPO;
    int n = type == BATTERY_LIFEPO4 ? (int)(sizeof(LIFEPO4) / sizeof(LIFEPO4[0])) : (int)(sizeof(LIPO) / sizeof(LIPO[0]));
    if (volts >= t[0].v) {
        return 100;
    }
    if (volts <= t[n - 1].v) {
        return 0;
    }
    for (int i = 1; i < n; i++) {
        if (volts >= t[i].v) {
            float f = (volts - t[i].v) / (t[i - 1].v - t[i].v);
            return (int)lroundf(t[i].pct + f * (t[i - 1].pct - t[i].pct));
        }
    }
    return 0;
}

float battery_warn_volts(int type) { return type == BATTERY_LIFEPO4 ? 3.15f : 3.65f; }
float battery_crit_volts(int type) { return type == BATTERY_LIFEPO4 ? 3.10f : 3.50f; }
float battery_shutdown_volts(int type) { return type == BATTERY_LIFEPO4 ? 3.05f : 3.35f; }

bool battery_reading_ambiguous(float volts)
{
    return volts >= BATTERY_AMBIGUOUS_MIN_V && volts <= BATTERY_AMBIGUOUS_MAX_V;
}

void battery_guard_init(battery_guard_t *g)
{
    g->low_since_ms = -1;
    g->low_min_v = 0.0f;
    g->ok_run = 0;
    g->invalid_run = 0;
}

bool battery_guard_update(battery_guard_t *g, int type, float volts, int64_t now_ms)
{
    if (!battery_present(volts)) {
        if (++g->invalid_run >= BATTERY_INVALID_RESET_N) {
            battery_guard_init(g); /* sustained: no battery / sensor failure */
            g->invalid_run = BATTERY_INVALID_RESET_N;
        }
        return false; /* never shut down on an invalid reading */
    }
    g->invalid_run = 0;
    if (volts < battery_shutdown_volts(type)) {
        g->ok_run = 0;
        if (g->low_since_ms < 0) {
            g->low_since_ms = now_ms;
            g->low_min_v = volts;
            return false;
        }
        if (volts < g->low_min_v) {
            g->low_min_v = volts;
        }
        if (now_ms - g->low_since_ms < BATTERY_SHUTDOWN_HOLD_MS) {
            return false;
        }
        if (volts > g->low_min_v + BATTERY_RISE_MV / 1000.0f) {
            /* voltage is rising: charging, restart the window */
            g->low_since_ms = now_ms;
            g->low_min_v = volts;
            return false;
        }
        return true;
    }
    if (++g->ok_run >= 2) {
        g->low_since_ms = -1;
        g->ok_run = 2;
    }
    return false;
}

bool battery_guard_poll(battery_guard_t *g, bool enabled, int type, float volts, int64_t now_ms)
{
    if (!enabled) {
        battery_guard_init(g);
        return false;
    }
    return battery_guard_update(g, type, volts, now_ms);
}
