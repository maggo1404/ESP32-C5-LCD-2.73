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

    /* guard: sustained low triggers only after > 10 s */
    battery_guard_t g;
    battery_guard_init(&g);
    int64_t t = 1000;
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t + 10000));
    CHECK(battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, t + 10001));
    /* single low dip never triggers */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 3000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 6000)); /* 2 ok -> reset */
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 20000));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 25000));
    CHECK(battery_guard_update(&g, BATTERY_LIFEPO4, 3.00f, 30001));
    /* one noisy high reading does not reset the timer */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.50f, 3000));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 6000));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.30f, 10500));
    /* invalid reading resets and never triggers */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, -1.0f, 5000));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, -1.0f, 60000));
    /* USB without battery: low floating reading for minutes never triggers */
    battery_guard_init(&g);
    for (int64_t t = 0; t <= 300000; t += 1000)
        CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 0.6f, t));
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 61000)); /* timer restarted */
    CHECK(!battery_guard_update(&g, BATTERY_LIPO, 3.30f, 70000));
    CHECK(battery_guard_update(&g, BATTERY_LIPO, 3.30f, 71500));
    /* threshold is per type: 3.20 V is fine for LiFePO4 guard, low for LiPo */
    battery_guard_init(&g);
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 0));
    CHECK(!battery_guard_update(&g, BATTERY_LIFEPO4, 3.20f, 60000));

    if (fails) {
        printf("%d check(s) failed\n", fails);
        return 1;
    }
    printf("battery tests passed\n");
    return 0;
}
