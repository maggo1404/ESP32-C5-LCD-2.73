/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host test for battery.c. Build/run: make -C firmware/main/test_battery */
#include <math.h>
#include <stdio.h>

#include "../battery.h"

static int fails;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
            fails++;                                                      \
        }                                                                 \
    } while (0)

static void test_type(int type, float vfull, float vempty, const float *v, const int *p, int n)
{
    CHECK(board_battery_percent(type, vfull) == 100);
    CHECK(board_battery_percent(type, vfull + 0.5f) == 100);
    CHECK(board_battery_percent(type, vempty) == 0);
    CHECK(board_battery_percent(type, vempty - 0.3f) == 0);
    for (int i = 0; i < n; i++) {
        CHECK(board_battery_percent(type, v[i]) == p[i]);
    }
    int prev = 101;
    for (float x = vfull + 0.1f; x >= BATTERY_ABSENT_VOLTS; x -= 0.001f) { /* monotonic, always 0..100 */
        int c = board_battery_percent(type, x);
        CHECK(c >= 0 && c <= 100 && c <= prev);
        prev = c;
    }
}

int main(void)
{
    const float lv[] = {4.00f, 3.85f, 3.75f, 3.65f, 3.50f, 3.90f};
    const int lp[] = {80, 60, 40, 20, 5, 67};
    test_type(BATTERY_LIPO, 4.20f, 3.30f, lv, lp, 6);
    const float fv[] = {3.32f, 3.30f, 3.27f, 3.22f, 3.10f, 3.31f};
    const int fp[] = {80, 60, 40, 20, 5, 70};
    test_type(BATTERY_LIFEPO4, 3.40f, 3.00f, fv, fp, 6);

    /* invalid readings */
    CHECK(board_battery_percent(BATTERY_LIPO, -1.0f) == -1);
    CHECK(board_battery_percent(BATTERY_LIFEPO4, 0.0f) == -1);
    CHECK(board_battery_percent(BATTERY_LIPO, NAN) == -1);
    CHECK(board_battery_percent(BATTERY_LIPO, INFINITY) == -1);
    /* no battery on USB: floating/low BAT input must count as absent */
    CHECK(board_battery_percent(BATTERY_LIPO, 0.4f) == -1);
    CHECK(board_battery_percent(BATTERY_LIFEPO4, 1.99f) == -1);
    CHECK(board_battery_percent(BATTERY_LIFEPO4, 2.5f) == 0);
    /* unknown type behaves like LiPo */
    CHECK(board_battery_percent(7, 3.85f) == 60);
    /* LiFePO4 resting 3.3 V is mid-range, would be empty on LiPo */
    CHECK(board_battery_percent(BATTERY_LIFEPO4, 3.30f) > 50 && board_battery_percent(BATTERY_LIPO, 3.30f) == 0);

    /* thresholds ordered: warn > crit > shutdown */
    for (int t = 0; t < BATTERY_TYPE_COUNT; t++) {
        CHECK(battery_warn_volts(t) > battery_crit_volts(t));
        CHECK(battery_crit_volts(t) > battery_shutdown_volts(t));
    }
    CHECK(battery_shutdown_volts(BATTERY_LIFEPO4) == 3.05f);
    CHECK(battery_shutdown_volts(BATTERY_LIPO) == 3.35f);

    /* ambiguous band (ADC reference sags with the cell) */
    CHECK(battery_reading_ambiguous(3.36f) && battery_reading_ambiguous(3.33f));
    CHECK(!battery_reading_ambiguous(3.20f) && !battery_reading_ambiguous(3.65f) && !battery_reading_ambiguous(-1.0f));

    battery_guard_t g;
    const int64_t H = BATTERY_SHUTDOWN_HOLD_MS; /* 60 s */
    /* sustained, flat low triggers only once the full hold time has passed */
    battery_guard_init(&g);
    int64_t t0 = 1000;
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t0 + 10001)); /* old 10 s limit: no */
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t0 + H - 1));
    CHECK(battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t0 + H));
    /* rising voltage (USB charging a flat cell): no trigger, window restarts */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.02f, 30000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.04f, H)); /* +40 mV over min */
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.04f, H + 30000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.04f, 2 * H - 1)); /* new window not over */
    CHECK(battery_guard_update(&g, BATTERY_LIFEPO4, 3.04f, 2 * H));      /* flat again: trigger */
    /* small noise (< 20 mV) is not "rising" */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.31f, 30000));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.315f, H));
    /* short low dip never triggers */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 3000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 6000)); /* 2 ok -> reset */
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 100000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 100000 + H - 1));
    CHECK(battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 100000 + H));
    /* one noisy high reading does not reset the timer */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.50f, 3000));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 6000));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.30f, H + 500));
    /* invalid readings: fewer than N in a row are ignored (no reset, no trigger) */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    for (int i = 1; i < BATTERY_INVALID_RESET_N; i++)
        CHECK(!battery_guard_update(&g, BATTERY_LIPO, -1.0f, 1000 * i));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, H - 1));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.30f, H));
    /* N in a row reset the guard; an invalid reading itself never triggers */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    for (int i = 1; i <= BATTERY_INVALID_RESET_N; i++)
        CHECK(!battery_guard_update(&g, BATTERY_LIPO, -1.0f, 1000 * i));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 10 * H)); /* window restarted here */
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 10 * H + H - 1));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.30f, 10 * H + H));
    /* invalid readings never trigger even when long past the hold time */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, -1.0f, 10 * H));
    /* USB without battery: low floating reading for minutes never triggers */
    battery_guard_init(&g);
    for (int64_t t = 0; t <= 300000; t += 1000)
        CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 0.6f, t));
    /* threshold is per type: 3.20 V is fine for LiFePO4, low for LiPo */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 2 * H));
    /* opt-in: disabled guard never triggers and keeps no state */
    battery_guard_init(&g);
    for (int64_t t = 0; t <= 5 * H; t += 1000)
        CHECK(!battery_guard_poll(&g, false, BATTERY_LIFEPO4, 3.00f, t));
    CHECK(!battery_guard_poll(&g, true, BATTERY_LIFEPO4, 3.00f, 6 * H)); /* starts fresh */
    CHECK(battery_guard_poll(&g, true, BATTERY_LIFEPO4, 3.00f, 7 * H));

    if (fails) {
        printf("%d check(s) failed\n", fails);
        return 1;
    }
    printf("battery tests passed\n");
    return 0;
}
