/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Battery chemistry helpers: state of charge curve, warning/shutdown thresholds
 * and the low-voltage guard. Pure C, no ESP-IDF dependencies, so it can be unit
 * tested on the host (see test_battery/). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BATTERY_LIPO = 0,    /* 1S LiPo / Li-Ion, 4.20 V full */
    BATTERY_LIFEPO4 = 1, /* 1S LiFePO4, 3.40 V resting "full" for a 3.6 V charger */
    BATTERY_NONE = 2,    /* USB-only build, no cell fitted: no measurement, no guard */
    BATTERY_TYPE_COUNT
} battery_type_t;

/* Below this the BAT input is treated as "no battery connected" (floating or
 * pulled-down divider while running from USB). No usable 1S LiPo/LiFePO4 cell
 * is ever this low, so the guard must never shut down on such a reading. */
#define BATTERY_ABSENT_VOLTS 2.0f
bool battery_present(float volts);

/* Display name: "LiPo" / "LiFePO4" / "Kein Akku (USB)" (invalid type -> "LiPo"). */
const char *battery_type_name(int type);

/* Approximate state of charge 0..100 % from the (resting) cell voltage, by
 * piecewise-linear interpolation of a per-chemistry table. Under load the
 * voltage is lower, so the value reads a bit pessimistic; the LiFePO4 curve is
 * very flat (3.2..3.3 V covers ~20..60 %), treat it as a rough indication.
 * Returns -1 if no battery is present (volts not finite or < BATTERY_ABSENT_VOLTS). Clamps to
 * 0/100 outside the table. An unknown type is handled as LiPo. */
int board_battery_percent(int type, float volts);

/* Voltage below which the UI shows the battery in yellow / red. */
float battery_warn_volts(int type);
float battery_crit_volts(int type);
/* Voltage below which the guard shuts the device down. */
float battery_shutdown_volts(int type);

/* The ADC reference of the CH32V003 is the 3V3 rail. Once the cell is below the
 * MP1605 dropout (~3.37 V) that rail follows the cell, so the computed voltage
 * saturates around 3.36 V no matter how low the cell really is. Readings in
 * this band cannot be told apart (full LiFePO4 .. empty LiPo): the UI flags
 * them and the guard cannot see a low cell. See README (Akku-ADC). */
#define BATTERY_AMBIGUOUS_MIN_V 3.33f
#define BATTERY_AMBIGUOUS_MAX_V 3.45f /* reading in dropout can exceed 3.40 V under load */
bool battery_reading_ambiguous(float volts);

/* The low-voltage guard shuts down only if the voltage stayed below the
 * threshold this long and did not rise (USB charging) in that time. */
#define BATTERY_SHUTDOWN_HOLD_MS 60000
#define BATTERY_RISE_MV 20 /* a rise above the window minimum by more than this = charging */
/* Consecutive invalid readings before the guard forgets its state; fewer are ignored. */
#define BATTERY_INVALID_RESET_N 5

typedef struct {
    int64_t low_since_ms; /* start of the current low window, -1: not low */
    float low_min_v;      /* lowest reading in the window */
    int ok_run;           /* consecutive readings at/above the threshold */
    int invalid_run;      /* consecutive invalid readings */
} battery_guard_t;

void battery_guard_init(battery_guard_t *g);
/* Feed one reading. Returns true once the voltage has been below the shutdown
 * threshold for at least BATTERY_SHUTDOWN_HOLD_MS without rising by more than
 * BATTERY_RISE_MV above the window minimum (a rising voltage restarts the
 * window: charging). An invalid reading never triggers; it is ignored unless
 * BATTERY_INVALID_RESET_N arrive in a row, then the guard resets. One reading
 * above the threshold does not reset it (needs 2 in a row). */
bool battery_guard_update(battery_guard_t *g, int type, float volts, int64_t now_ms);

/* Call-site wrapper: the automatic shutdown is opt-in. With enabled == false the
 * guard is held in its initial state and never triggers. */
bool battery_guard_poll(battery_guard_t *g, bool enabled, int type, float volts, int64_t now_ms);
