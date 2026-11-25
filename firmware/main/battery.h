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
    BATTERY_TYPE_COUNT
} battery_type_t;

/* Below this the BAT input is treated as "no battery connected" (floating or
 * pulled-down divider while running from USB). No usable 1S LiPo/LiFePO4 cell
 * is ever this low, so the guard must never shut down on such a reading. */
#define BATTERY_ABSENT_VOLTS 2.0f
bool battery_present(float volts);

/* Display name: "LiPo" / "LiFePO4" (invalid type -> "LiPo"). */
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

/* Duration the voltage must stay below the shutdown threshold. */
#define BATTERY_SHUTDOWN_HOLD_MS 10000

typedef struct {
    int64_t low_since_ms; /* -1: not currently low */
    int ok_run;           /* consecutive readings at/above the threshold */
} battery_guard_t;

void battery_guard_init(battery_guard_t *g);
/* Feed one reading. Returns true once the voltage has been below the shutdown
 * threshold for more than BATTERY_SHUTDOWN_HOLD_MS. An invalid reading
 * (volts <= 0 or not finite) resets the guard and never triggers; a single
 * reading above the threshold does not reset it (needs 2 in a row), a single
 * low reading cannot trigger (needs the hold time). */
bool battery_guard_update(battery_guard_t *g, int type, float volts, int64_t now_ms);
