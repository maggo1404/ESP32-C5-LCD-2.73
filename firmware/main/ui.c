/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LVGL user interface (German, 320x320, dark).
 *
 * Everything here runs in the LVGL task. Data from the decoder store is
 * copied under its_store_lock() into static snapshots (short lock time),
 * LVGL is only called afterwards.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_attr.h"
#include "buttons.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "its.h"
#include "rx_11p.h"
#include "sd_log.h"
#include "settings.h"
#include "ui_common.h"
#include "ui_lights.h"
#include "usb_out.h"
#include "app_config.h"

/* ------------------------------------------------------------------ */
/* layout                                                              */
/* ------------------------------------------------------------------ */
#define BAR_H       24
#define TITLE_Y     BAR_H
#define TITLE_H     28
#define CONTENT_Y   (TITLE_Y + TITLE_H)         /* 52  */
#define TAB_H       30
#define TAB_Y       (UI_H - TAB_H)              /* 290 */
#define CONTENT_H   (TAB_Y - CONTENT_Y)         /* 238 */
#define T_HDR_H     22
#define T_ROW_H     24
#define T_ROWS      9                           /* 22 + 9*24 = 238 */
#define T_MAXCOLS   5
#define REFRESH_MS  250

typedef enum { PAGE_STATUS, PAGE_STATIONS, PAGE_LIGHTS, PAGE_WARN, PAGE_SETTINGS, PAGE_COUNT } page_t;
typedef enum { NAV_TOP, NAV_LIST, NAV_DETAIL, NAV_EDIT } nav_t;

static const char *const TAB_NAMES[PAGE_COUNT] = {"Status", "Stat.", "Ampeln", "Warn.", "Einst."};

static QueueHandle_t s_btn_q;
static page_t s_page = PAGE_STATUS;
static nav_t s_nav = NAV_TOP;

/* ------------------------------------------------------------------ */
/* small widgets                                                       */
/* ------------------------------------------------------------------ */

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int w, const lv_font_t *f, lv_color_t col,
                          lv_text_align_t al)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_text(l, "");
    return l;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void set_color(lv_obj_t *l, lv_color_t c)
{
    lv_obj_set_style_text_color(l, c, 0);
}

static void show(lv_obj_t *o, bool vis)
{
    if (vis) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- table: header + T_ROWS rows with up to T_MAXCOLS columns ---- */
typedef struct {
    lv_obj_t *cont;
    lv_obj_t *hdr[T_MAXCOLS];
    lv_obj_t *row[T_ROWS];
    lv_obj_t *cell[T_ROWS][T_MAXCOLS];
    int ncols;
} table_t;

static void table_create(table_t *t, lv_obj_t *parent, int ncols, const int *x, const int *w, const char *const *hdr)
{
    t->ncols = ncols;
    t->cont = plain(parent, 0, 0, UI_W, CONTENT_H);
    for (int c = 0; c < ncols; c++) {
        t->hdr[c] = mk_label(t->cont, x[c], 3, w[c], ui_f12, C_DIM, LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(t->hdr[c], hdr[c]);
    }
    for (int r = 0; r < T_ROWS; r++) {
        t->row[r] = plain(t->cont, 0, T_HDR_H + r * T_ROW_H, UI_W, T_ROW_H);
        lv_obj_set_style_bg_color(t->row[r], C_SEL, 0);
        lv_obj_set_style_bg_opa(t->row[r], LV_OPA_TRANSP, 0);
        for (int c = 0; c < ncols; c++) {
            t->cell[r][c] = mk_label(t->row[r], x[c], (T_ROW_H - ui_f14->line_height) / 2, w[c], ui_f14, C_TEXT,
                                     LV_TEXT_ALIGN_LEFT);
        }
        lv_obj_add_flag(t->row[r], LV_OBJ_FLAG_HIDDEN);
    }
}

static void table_row(table_t *t, int r, bool sel, bool visible)
{
    show(t->row[r], visible);
    lv_obj_set_style_bg_opa(t->row[r], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

/* keep `sel` inside the window [top, top+T_ROWS) */
static int window_top(int top, int sel, int count)
{
    if (sel < top) top = sel;
    if (sel >= top + T_ROWS) top = sel - T_ROWS + 1;
    if (top > count - T_ROWS) top = count - T_ROWS;
    if (top < 0) top = 0;
    return top;
}

/* ------------------------------------------------------------------ */
/* snapshots of the decoder store                                      */
/* ------------------------------------------------------------------ */
static int64_t s_now;

static struct {
    uint32_t total, its, secured, errors;
    uint32_t per_msg[ITS_MSG_COUNT];
    float rate;
    int64_t last_rx_us;
    int8_t last_rssi;
} S;

static EXT_RAM_BSS_ATTR its_station_t s_st[ITS_STORE_MAX_STATIONS];
static uint8_t s_st_order[ITS_STORE_MAX_STATIONS];
static int s_st_n;

typedef struct {
    uint16_t region, id;
    char name[ITS_NAME_LEN];
    bool has_map, has_spat;
    int64_t map_rx_us, spat_rx_us;
} li_sum_t;
static li_sum_t s_li[ITS_STORE_MAX_INTERSECTIONS];
static int s_li_n;
static EXT_RAM_BSS_ATTR its_store_intersection_t s_sel_is; /* full copy of the open intersection */
static bool s_sel_valid;

static its_store_denm_t s_dn[ITS_STORE_MAX_DENMS];
static uint8_t s_dn_order[ITS_STORE_MAX_DENMS];
static int s_dn_n;

/* selection per page: index + key (so lists can change underneath) */
static int s_sel[PAGE_COUNT];
static int s_top[PAGE_COUNT];
static uint32_t s_key_st;
static uint32_t s_key_li; /* region<<16 | id */
static uint32_t s_key_dn_station, s_key_dn_seq;
static bool s_key_st_valid, s_key_li_valid, s_key_dn_valid;

static bool station_used(const its_station_t *s)
{
    return s->packets != 0; /* store convention: slot in use iff packets > 0 */
}

static void take_snapshot(void)
{
    s_now = esp_timer_get_time();
    bool want_st = s_page == PAGE_STATIONS;
    bool want_li = s_page == PAGE_LIGHTS;
    bool want_dn = s_page == PAGE_WARN;
    bool want_sel = want_li && s_nav == NAV_DETAIL;

    its_store_lock();
    const its_store_t *st = its_store_get();
    S.total = st->total_frames;
    S.its = st->its_frames;
    S.secured = st->secured_frames;
    S.errors = st->decode_errors;
    memcpy(S.per_msg, st->per_msg, sizeof(S.per_msg));
    S.rate = st->rate_per_s;
    S.last_rx_us = st->last_rx_us;
    S.last_rssi = st->last_rssi;
    if (want_st) {
        memcpy(s_st, st->stations, sizeof(s_st));
    }
    s_sel_valid = false;
    if (want_li) {
        s_li_n = 0;
        for (int i = 0; i < ITS_STORE_MAX_INTERSECTIONS; i++) {
            const its_store_intersection_t *is = &st->intersections[i];
            if (!is->used) {
                continue;
            }
            int pos = s_li_n;
            li_sum_t *o = &s_li[s_li_n++];
            o->region = is->region;
            o->id = is->id;
            memcpy(o->name, is->name, sizeof(o->name));
            o->has_map = is->has_map;
            o->has_spat = is->has_spat;
            o->map_rx_us = is->map_rx_us;
            o->spat_rx_us = is->spat_rx_us;
            bool match = s_key_li_valid ? (is->region == (s_key_li >> 16) && is->id == (s_key_li & 0xffff))
                                        : (pos == s_sel[PAGE_LIGHTS]);
            if (want_sel && match) {
                memcpy(&s_sel_is, is, sizeof(s_sel_is));
                s_sel_valid = true;
            }
        }
    }
    if (want_dn) {
        memcpy(s_dn, st->denms, sizeof(s_dn));
    }
    its_store_unlock();

    if (want_st) {
        s_st_n = 0;
        for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
            if (!station_used(&s_st[i])) {
                continue;
            }
            /* insertion sort: most recently heard first */
            int j = s_st_n++;
            while (j > 0 && s_st[s_st_order[j - 1]].last_seen_us < s_st[i].last_seen_us) {
                s_st_order[j] = s_st_order[j - 1];
                j--;
            }
            s_st_order[j] = (uint8_t)i;
        }
    }
    if (want_dn) {
        s_dn_n = 0;
        for (int i = 0; i < ITS_STORE_MAX_DENMS; i++) {
            if (!s_dn[i].used) {
                continue;
            }
            int j = s_dn_n++;
            while (j > 0 && s_dn[s_dn_order[j - 1]].rx_us < s_dn[i].rx_us) {
                s_dn_order[j] = s_dn_order[j - 1];
                j--;
            }
            s_dn_order[j] = (uint8_t)i;
        }
    }
}

/* ------------------------------------------------------------------ */
/* formatting helpers                                                  */
/* ------------------------------------------------------------------ */

static void fmt_age(char *b, size_t n, int64_t us)
{
    int64_t s = us / 1000000;
    if (s < 0) s = 0;
    if (s < 100) snprintf(b, n, "%ds", (int)s);
    else if (s < 6000) snprintf(b, n, "%dmin", (int)(s / 60));
    else snprintf(b, n, "%dh", (int)(s / 3600));
}

static void fmt_deg(char *b, size_t n, int32_t v)
{
    int64_t a = v < 0 ? -(int64_t)v : (int64_t)v;
    snprintf(b, n, "%s%d.%05d", v < 0 ? "-" : "", (int)(a / 10000000), (int)((a % 10000000) / 100));
}

static void fmt_uptime(char *b, size_t n)
{
    int64_t s = esp_timer_get_time() / 1000000;
    snprintf(b, n, "%d:%02d:%02d", (int)(s / 3600), (int)((s / 60) % 60), (int)(s % 60));
}

static void msg_letters(uint32_t mask, char *b, size_t n)
{
    size_t k = 0;
    if (k + 1 < n && (mask & (1u << ITS_MSG_CAM))) b[k++] = 'C';
    if (k + 1 < n && (mask & (1u << ITS_MSG_DENM))) b[k++] = 'D';
    if (k + 1 < n && (mask & (1u << ITS_MSG_MAPEM))) b[k++] = 'M';
    if (k + 1 < n && (mask & (1u << ITS_MSG_SPATEM))) b[k++] = 'S';
    uint32_t known = (1u << ITS_MSG_CAM) | (1u << ITS_MSG_DENM) | (1u << ITS_MSG_MAPEM) | (1u << ITS_MSG_SPATEM);
    if (k + 1 < n && (mask & ~known)) b[k++] = '+';
    b[k] = 0;
}

static void msg_names(uint32_t mask, char *b, size_t n)
{
    b[0] = 0;
    for (int id = 1; id < ITS_MSG_COUNT; id++) {
        if (mask & (1u << id)) {
            size_t l = strlen(b);
            snprintf(b + l, n - l, "%s%s", l ? ", " : "", its_msg_name((uint8_t)id));
        }
    }
    if (!b[0]) strlcpy(b, "-", n);
}

static const char *channel_name(uint16_t mhz)
{
    switch (mhz) {
    case 5860: return " (SCH4)";
    case 5870: return " (SCH3)";
    case 5880: return " (SCH1)";
    case 5890: return " (SCH2)";
    case 5900: return " (CCH)";
    default: return "";
    }
}

/* ------------------------------------------------------------------ */
/* widgets                                                             */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_bar_ch, *s_bar_rate, *s_bar_usb, *s_bar_sd, *s_bar_bat;
static lv_obj_t *s_title, *s_hint;
static lv_obj_t *s_tab[PAGE_COUNT];
static lv_obj_t *s_page_cont[PAGE_COUNT];

static lv_obj_t *s_status_lbl;

static table_t s_t_st, s_t_li, s_t_dn, s_t_set;
static lv_obj_t *s_empty_st, *s_empty_li, *s_empty_dn;
static lv_obj_t *s_detail_st, *s_detail_dn, *s_detail_li;

static void build_topbar(lv_obj_t *scr)
{
    lv_obj_t *bar = plain(scr, 0, 0, UI_W, BAR_H);
    lv_obj_set_style_bg_color(bar, C_BAR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    int ty = (BAR_H - ui_f14->line_height) / 2;
    s_bar_ch = mk_label(bar, 6, ty, 100, ui_f14, C_TEXT, LV_TEXT_ALIGN_LEFT);
    s_bar_rate = mk_label(bar, 104, ty, 70, ui_f14, C_ACCENT, LV_TEXT_ALIGN_LEFT);
    s_bar_usb = mk_label(bar, 178, ty, 22, ui_f14, C_DIM, LV_TEXT_ALIGN_CENTER);
    s_bar_sd = mk_label(bar, 200, ty, 22, ui_f14, C_DIM, LV_TEXT_ALIGN_CENTER);
    s_bar_bat = mk_label(bar, 224, ty, 92, ui_f14, C_DIM, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(s_bar_usb, LV_SYMBOL_USB);
    lv_label_set_text(s_bar_sd, LV_SYMBOL_SD_CARD);
}

static void build_titlebar(lv_obj_t *scr)
{
    s_title = mk_label(scr, 6, TITLE_Y + 2, 150, ui_f20, C_TEXT, LV_TEXT_ALIGN_LEFT);
    s_hint = mk_label(scr, 158, TITLE_Y + 8, 158, ui_f12, C_DIM, LV_TEXT_ALIGN_RIGHT);
}

static void build_tabs(lv_obj_t *scr)
{
    int w = UI_W / PAGE_COUNT;
    lv_obj_t *strip = plain(scr, 0, TAB_Y, UI_W, TAB_H);
    lv_obj_set_style_bg_color(strip, C_BAR, 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *t = plain(strip, i * w, 0, w, TAB_H);
        lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
        lv_obj_t *l = mk_label(t, 0, (TAB_H - ui_f14->line_height) / 2, w, ui_f14, C_DIM, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(l, TAB_NAMES[i]);
        s_tab[i] = t;
    }
}

static lv_obj_t *detail_label(lv_obj_t *parent)
{
    lv_obj_t *l = mk_label(parent, 8, 6, UI_W - 16, ui_f14, C_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, 4, 0);
    return l;
}

static lv_obj_t *empty_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = mk_label(parent, 12, 80, UI_W - 24, ui_f14, C_DIM, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, txt);
    return l;
}

static void build_pages(lv_obj_t *scr)
{
    for (int p = 0; p < PAGE_COUNT; p++) {
        s_page_cont[p] = plain(scr, 0, CONTENT_Y, UI_W, CONTENT_H);
        lv_obj_add_flag(s_page_cont[p], LV_OBJ_FLAG_HIDDEN);
    }
    /* Status */
    s_status_lbl = mk_label(s_page_cont[PAGE_STATUS], 8, 6, UI_W - 16, ui_f14, C_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_status_lbl, 3, 0);

    /* Stationen */
    {
        const int x[] = {6, 92, 178, 218, 262};
        const int w[] = {84, 84, 38, 42, 54};
        const char *h[] = {"ID", "Typ", "dBm", "Alter", "Typen"};
        table_create(&s_t_st, s_page_cont[PAGE_STATIONS], 5, x, w, h);
        s_empty_st = empty_label(s_page_cont[PAGE_STATIONS], "Noch keine Stationen empfangen.");
        s_detail_st = detail_label(s_page_cont[PAGE_STATIONS]);
    }
    /* Ampeln */
    {
        const int x[] = {6, 190, 224, 262};
        const int w[] = {180, 30, 34, 54};
        const char *h[] = {"Kreuzung", "MAP", "SPAT", "Alter"};
        table_create(&s_t_li, s_page_cont[PAGE_LIGHTS], 4, x, w, h);
        s_empty_li = empty_label(s_page_cont[PAGE_LIGHTS],
                                 "Noch keine Ampel-Kreuzung empfangen (MAPEM/SPATEM).");
        s_detail_li = ui_lights_create(s_page_cont[PAGE_LIGHTS], UI_W, CONTENT_H);
    }
    /* Warnungen */
    {
        const int x[] = {6, 160, 250, 292};
        const int w[] = {150, 86, 40, 28};
        const char *h[] = {"Ursache", "Station", "Alter", "dBm"};
        table_create(&s_t_dn, s_page_cont[PAGE_WARN], 4, x, w, h);
        s_empty_dn = empty_label(s_page_cont[PAGE_WARN], "Keine Warnungen (DENM) empfangen.");
        s_detail_dn = detail_label(s_page_cont[PAGE_WARN]);
    }
    /* Einstellungen */
    {
        const int x[] = {6, 176};
        const int w[] = {166, 140};
        const char *h[] = {"Einstellung", "Wert"};
        table_create(&s_t_set, s_page_cont[PAGE_SETTINGS], 2, x, w, h);
    }
}

/* ------------------------------------------------------------------ */
/* per page refresh                                                    */
/* ------------------------------------------------------------------ */

static const char *page_title(char *b, size_t n)
{
    switch (s_page) {
    case PAGE_STATUS: return "Status";
    case PAGE_STATIONS: snprintf(b, n, s_nav == NAV_DETAIL ? "Station" : "Stationen (%d)", s_st_n); return b;
    case PAGE_LIGHTS:
        if (s_nav == NAV_DETAIL) return "Ampel";
        snprintf(b, n, "Ampeln (%d)", s_li_n);
        return b;
    case PAGE_WARN: snprintf(b, n, s_nav == NAV_DETAIL ? "Warnung" : "Warnungen (%d)", s_dn_n); return b;
    default: return "Einstellungen";
    }
}

static const char *page_hint(void)
{
    switch (s_nav) {
    case NAV_TOP:
        return s_page == PAGE_STATUS ? "< > Seite wechseln" : "< > Seite  OK: Öffnen";
    case NAV_LIST:
        return s_page == PAGE_SETTINGS ? "< > Wahl  OK: Ändern" : "< > Wahl  OK: Details";
    case NAV_EDIT:
        return "< > Ändern  OK: Fertig";
    default:
        if (s_page == PAGE_LIGHTS) {
            return ui_lights_map_view() ? "OK: Spuren  < > Kreuzung" : "OK: Karte  < > Scrollen";
        }
        return "< > Eintrag  Lang: Zurück";
    }
}

static void update_status(void)
{
    rx_stats_t rs;
    rx_get_stats(&rs);
    sd_log_status_t sd;
    sd_log_get_status(&sd);
    settings_t cfg = settings_get();
    char age[16], up[16], sdtxt[64], usbtxt[64], bat[24];

    if (S.last_rx_us) fmt_age(age, sizeof(age), s_now - S.last_rx_us); else strcpy(age, "--");
    fmt_uptime(up, sizeof(up));

    switch (sd.state) {
    case SD_LOG_RECORDING:
        snprintf(sdtxt, sizeof(sdtxt), "%s, %u kB", sd.file, (unsigned)(sd.bytes / 1024));
        break;
    case SD_LOG_NO_CARD:
        snprintf(sdtxt, sizeof(sdtxt), sd.error ? "Fehler, neuer Versuch ..." : "keine Karte");
        break;
    default:
        snprintf(sdtxt, sizeof(sdtxt), "aus");
    }
    if (!cfg.usb_out) {
        snprintf(usbtxt, sizeof(usbtxt), "aus");
    } else {
        snprintf(usbtxt, sizeof(usbtxt), "%s, %u Pakete", usb_out_connected() ? "verbunden" : "kein Host",
                 (unsigned)rs.usb_frames);
    }
    float v = board_battery_volts();
    if (v > 0) snprintf(bat, sizeof(bat), "%.2f V", (double)v); else strcpy(bat, "n/a");

    uint32_t known = S.per_msg[ITS_MSG_CAM] + S.per_msg[ITS_MSG_DENM] + S.per_msg[ITS_MSG_MAPEM] +
                     S.per_msg[ITS_MSG_SPATEM];
    uint32_t other = S.its > known ? S.its - known : 0;
    char b[640];
    snprintf(b, sizeof(b),
             "Pakete: %u (%.1f/s)\n"
             "ITS: %u, gesichert %u, Fehler %u\n"
             "CAM %u  DENM %u  MAPEM %u  SPATEM %u\n"
             "Andere ITS-Nachrichten: %u\n"
             "Letzter RSSI: %d dBm (vor %s)\n"
             "Kanal: %u MHz%s\n"
             "SD: %s%s\n"
             "USB: %s\n"
             "Akku: %s   Laufzeit: %s\n"
             "Verworfen: %u   Heap: %u kB",
             (unsigned)S.total, (double)S.rate, (unsigned)S.its, (unsigned)S.secured, (unsigned)S.errors,
             (unsigned)S.per_msg[ITS_MSG_CAM], (unsigned)S.per_msg[ITS_MSG_DENM], (unsigned)S.per_msg[ITS_MSG_MAPEM],
             (unsigned)S.per_msg[ITS_MSG_SPATEM], (unsigned)other, (int)S.last_rssi, age, (unsigned)cfg.channel,
             channel_name(cfg.channel), sdtxt, sd.dropped ? " (Verluste!)" : "", usbtxt, bat, up,
             (unsigned)(rs.drop_pool + rs.drop_size + sd.dropped),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    set_text(s_status_lbl, b);
}

static void update_stations(void)
{
    /* resolve key -> index */
    if (s_key_st_valid) {
        for (int i = 0; i < s_st_n; i++) {
            if (s_st[s_st_order[i]].station_id == s_key_st) {
                s_sel[PAGE_STATIONS] = i;
                break;
            }
        }
    }
    if (s_sel[PAGE_STATIONS] >= s_st_n) s_sel[PAGE_STATIONS] = s_st_n - 1;
    if (s_sel[PAGE_STATIONS] < 0) s_sel[PAGE_STATIONS] = 0;
    if (s_st_n) {
        s_key_st = s_st[s_st_order[s_sel[PAGE_STATIONS]]].station_id;
        s_key_st_valid = true;
    }

    bool detail = s_nav == NAV_DETAIL && s_st_n > 0;
    show(s_t_st.cont, !detail);
    show(s_detail_st, detail);
    show(s_empty_st, s_st_n == 0);
    if (detail) {
        const its_station_t *s = &s_st[s_st_order[s_sel[PAGE_STATIONS]]];
        char names[96], age[16], mac[24], lat[24], lon[24], b[400];
        msg_names(s->msg_mask, names, sizeof(names));
        fmt_age(age, sizeof(age), s_now - s->last_seen_us);
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", s->mac[0], s->mac[1], s->mac[2], s->mac[3],
                 s->mac[4], s->mac[5]);
        int n = snprintf(b, sizeof(b), "ID: %u\nTyp: %s\nNachrichten: %s\nRSSI: %d dBm, Pakete: %u\nZuletzt: vor %s\nMAC: %s",
                         (unsigned)s->station_id, its_station_type_name(s->station_type), names, (int)s->rssi,
                         (unsigned)s->packets, age, mac);
        if (s->has_cam && s->cam.lat != ITS_LAT_UNAVAILABLE && s->cam.lon != ITS_LON_UNAVAILABLE) {
            fmt_deg(lat, sizeof(lat), s->cam.lat);
            fmt_deg(lon, sizeof(lon), s->cam.lon);
            n += snprintf(b + n, sizeof(b) - n, "\nPosition: %s, %s", lat, lon);
        }
        if (s->has_cam && s->cam.speed != ITS_SPEED_UNAVAILABLE) {
            int t = (int)((uint32_t)s->cam.speed * 36 / 100); /* 0.1 km/h */
            n += snprintf(b + n, sizeof(b) - n, "\nGeschwindigkeit: %d.%d km/h", t / 10, t % 10);
        }
        if (s->has_cam && s->cam.heading != ITS_HEADING_UNAVAILABLE) {
            snprintf(b + n, sizeof(b) - n, "\nRichtung: %d°", s->cam.heading / 10);
        }
        set_text(s_detail_st, b);
        return;
    }
    int top = window_top(s_top[PAGE_STATIONS], s_sel[PAGE_STATIONS], s_st_n);
    s_top[PAGE_STATIONS] = top;
    for (int r = 0; r < T_ROWS; r++) {
        int i = top + r;
        if (i >= s_st_n) {
            table_row(&s_t_st, r, false, false);
            continue;
        }
        const its_station_t *s = &s_st[s_st_order[i]];
        char id[16], rssi[8], age[16], ty[8];
        snprintf(id, sizeof(id), "%u", (unsigned)s->station_id);
        snprintf(rssi, sizeof(rssi), "%d", (int)s->rssi);
        fmt_age(age, sizeof(age), s_now - s->last_seen_us);
        msg_letters(s->msg_mask, ty, sizeof(ty));
        set_text(s_t_st.cell[r][0], id);
        set_text(s_t_st.cell[r][1], its_station_type_name(s->station_type));
        set_text(s_t_st.cell[r][2], rssi);
        set_text(s_t_st.cell[r][3], age);
        set_text(s_t_st.cell[r][4], ty);
        table_row(&s_t_st, r, s_nav != NAV_TOP && i == s_sel[PAGE_STATIONS], true);
    }
}

static void update_lights(void)
{
    if (s_key_li_valid) {
        for (int i = 0; i < s_li_n; i++) {
            if (((uint32_t)s_li[i].region << 16 | s_li[i].id) == s_key_li) {
                s_sel[PAGE_LIGHTS] = i;
                break;
            }
        }
    }
    if (s_sel[PAGE_LIGHTS] >= s_li_n) s_sel[PAGE_LIGHTS] = s_li_n - 1;
    if (s_sel[PAGE_LIGHTS] < 0) s_sel[PAGE_LIGHTS] = 0;
    if (s_li_n) {
        s_key_li = (uint32_t)s_li[s_sel[PAGE_LIGHTS]].region << 16 | s_li[s_sel[PAGE_LIGHTS]].id;
        s_key_li_valid = true;
    }

    bool detail = s_nav == NAV_DETAIL;
    show(s_t_li.cont, !detail);
    show(s_detail_li, detail);
    show(s_empty_li, !detail && s_li_n == 0);
    if (detail) {
        ui_lights_update(s_sel_valid ? &s_sel_is : NULL, s_now);
        return;
    }
    int top = window_top(s_top[PAGE_LIGHTS], s_sel[PAGE_LIGHTS], s_li_n);
    s_top[PAGE_LIGHTS] = top;
    for (int r = 0; r < T_ROWS; r++) {
        int i = top + r;
        if (i >= s_li_n) {
            table_row(&s_t_li, r, false, false);
            continue;
        }
        const li_sum_t *l = &s_li[i];
        char nm[48], age[16];
        if (l->name[0]) snprintf(nm, sizeof(nm), "%s", l->name);
        else snprintf(nm, sizeof(nm), "Kreuzung %u/%u", (unsigned)l->region, (unsigned)l->id);
        if (l->has_spat) fmt_age(age, sizeof(age), s_now - l->spat_rx_us); else strcpy(age, "--");
        set_text(s_t_li.cell[r][0], nm);
        set_text(s_t_li.cell[r][1], l->has_map ? LV_SYMBOL_OK : "-");
        set_text(s_t_li.cell[r][2], l->has_spat ? LV_SYMBOL_OK : "-");
        set_text(s_t_li.cell[r][3], age);
        set_color(s_t_li.cell[r][1], l->has_map ? C_OK : C_DIM);
        set_color(s_t_li.cell[r][2], l->has_spat ? C_OK : C_DIM);
        table_row(&s_t_li, r, s_nav != NAV_TOP && i == s_sel[PAGE_LIGHTS], true);
    }
}

static void update_warnings(void)
{
    if (s_key_dn_valid) {
        for (int i = 0; i < s_dn_n; i++) {
            const its_denm_t *d = &s_dn[s_dn_order[i]].denm;
            if (d->station_id == s_key_dn_station && d->sequence_number == s_key_dn_seq) {
                s_sel[PAGE_WARN] = i;
                break;
            }
        }
    }
    if (s_sel[PAGE_WARN] >= s_dn_n) s_sel[PAGE_WARN] = s_dn_n - 1;
    if (s_sel[PAGE_WARN] < 0) s_sel[PAGE_WARN] = 0;
    if (s_dn_n) {
        const its_denm_t *d = &s_dn[s_dn_order[s_sel[PAGE_WARN]]].denm;
        s_key_dn_station = d->station_id;
        s_key_dn_seq = d->sequence_number;
        s_key_dn_valid = true;
    }

    bool detail = s_nav == NAV_DETAIL && s_dn_n > 0;
    show(s_t_dn.cont, !detail);
    show(s_detail_dn, detail);
    show(s_empty_dn, s_dn_n == 0);
    if (detail) {
        const its_store_denm_t *e = &s_dn[s_dn_order[s_sel[PAGE_WARN]]];
        char age[16], lat[24], lon[24], b[300];
        fmt_age(age, sizeof(age), s_now - e->rx_us);
        int n = snprintf(b, sizeof(b), "%s\nUrsachencode: %u / %u\nStation: %u (Nr. %u)\nZuletzt: vor %s, RSSI %d dBm\nStatus: %s",
                         its_cause_name(e->denm.cause_code), (unsigned)e->denm.cause_code,
                         (unsigned)e->denm.sub_cause_code, (unsigned)e->denm.station_id,
                         (unsigned)e->denm.sequence_number, age, (int)e->rssi,
                         e->denm.termination ? "beendet" : "aktiv");
        if (e->denm.lat != ITS_LAT_UNAVAILABLE && e->denm.lon != ITS_LON_UNAVAILABLE) {
            fmt_deg(lat, sizeof(lat), e->denm.lat);
            fmt_deg(lon, sizeof(lon), e->denm.lon);
            snprintf(b + n, sizeof(b) - n, "\nPosition: %s, %s", lat, lon);
        }
        set_text(s_detail_dn, b);
        return;
    }
    int top = window_top(s_top[PAGE_WARN], s_sel[PAGE_WARN], s_dn_n);
    s_top[PAGE_WARN] = top;
    for (int r = 0; r < T_ROWS; r++) {
        int i = top + r;
        if (i >= s_dn_n) {
            table_row(&s_t_dn, r, false, false);
            continue;
        }
        const its_store_denm_t *e = &s_dn[s_dn_order[i]];
        char cause[64], st[16], age[16], rssi[8];
        snprintf(cause, sizeof(cause), "%s%s", its_cause_name(e->denm.cause_code), e->denm.termination ? " (Ende)" : "");
        snprintf(st, sizeof(st), "%u", (unsigned)e->denm.station_id);
        fmt_age(age, sizeof(age), s_now - e->rx_us);
        snprintf(rssi, sizeof(rssi), "%d", (int)e->rssi);
        set_text(s_t_dn.cell[r][0], cause);
        set_text(s_t_dn.cell[r][1], st);
        set_text(s_t_dn.cell[r][2], age);
        set_text(s_t_dn.cell[r][3], rssi);
        table_row(&s_t_dn, r, s_nav != NAV_TOP && i == s_sel[PAGE_WARN], true);
    }
}

#define SET_ROWS 5
static int s_edit_ch; /* pending channel while editing */

static void update_settings(void)
{
    settings_t c = settings_get();
    char v[SET_ROWS][32];
    unsigned shown_ch = (s_nav == NAV_EDIT && s_sel[PAGE_SETTINGS] == 0) ? (unsigned)s_edit_ch : (unsigned)c.channel;
    snprintf(v[0], sizeof(v[0]), "%u MHz%s", shown_ch, channel_name((uint16_t)shown_ch));
    snprintf(v[1], sizeof(v[1]), "%s", c.sd_log ? "an" : "aus");
    snprintf(v[2], sizeof(v[2]), "%s", c.usb_out ? "an" : "aus");
    snprintf(v[3], sizeof(v[3]), "%u %%", (unsigned)c.brightness);
    snprintf(v[4], sizeof(v[4]), "OK drücken");
    static const char *const names[SET_ROWS] = {"Kanal", "SD-Log (pcap)", "USB-Ausgabe (SLIP)", "Helligkeit",
                                                "Zähler löschen"};
    for (int r = 0; r < T_ROWS; r++) {
        if (r >= SET_ROWS) {
            table_row(&s_t_set, r, false, false);
            continue;
        }
        bool sel = s_nav != NAV_TOP && r == s_sel[PAGE_SETTINGS];
        bool edit = sel && s_nav == NAV_EDIT;
        char val[64];
        if (edit) snprintf(val, sizeof(val), "< %.31s >", v[r]); else strlcpy(val, v[r], sizeof(val));
        set_text(s_t_set.cell[r][0], names[r]);
        set_text(s_t_set.cell[r][1], val);
        set_color(s_t_set.cell[r][1], edit ? C_WARN : (r == 4 ? C_DIM : C_ACCENT));
        table_row(&s_t_set, r, sel, true);
    }
}

static void update_topbar(void)
{
    char b[32];
    settings_t cfg = settings_get();
    snprintf(b, sizeof(b), "%u MHz", (unsigned)cfg.channel);
    set_text(s_bar_ch, b);
    snprintf(b, sizeof(b), "%.0f/s", (double)S.rate);
    set_text(s_bar_rate, b);

    /* USB: grey = off, green = host connected, amber = enabled but no host */
    set_color(s_bar_usb, !cfg.usb_out ? C_DIM : (usb_out_connected() ? C_OK : C_WARN));
    sd_log_status_t sd;
    sd_log_get_status(&sd);
    lv_color_t sc = C_DIM;
    if (sd.state == SD_LOG_RECORDING) sc = C_OK;
    else if (sd.state == SD_LOG_NO_CARD) sc = sd.error ? C_ERR : C_WARN;
    set_color(s_bar_sd, sc);

    float v = board_battery_volts();
    if (v > 0) {
        const char *sym = v > 4.0f ? LV_SYMBOL_BATTERY_FULL : v > 3.8f ? LV_SYMBOL_BATTERY_3
                        : v > 3.65f ? LV_SYMBOL_BATTERY_2 : v > 3.5f ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(b, sizeof(b), "%.2fV %s", (double)v, sym);
        set_color(s_bar_bat, v < 3.5f ? C_ERR : (v < 3.65f ? C_WARN : C_TEXT));
    } else {
        snprintf(b, sizeof(b), "-- V");
        set_color(s_bar_bat, C_DIM);
    }
    set_text(s_bar_bat, b);
}

static void update_chrome(void)
{
    char tb[40];
    set_text(s_title, page_title(tb, sizeof(tb)));
    set_text(s_hint, page_hint());
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *lbl = lv_obj_get_child(s_tab[i], 0);
        bool cur = i == (int)s_page;
        if (cur && s_nav == NAV_TOP) {
            lv_obj_set_style_bg_color(s_tab[i], C_ACCENT, 0);
            lv_obj_set_style_bg_opa(s_tab[i], LV_OPA_COVER, 0);
            set_color(lbl, lv_color_hex(0x06121f));
        } else if (cur) {
            lv_obj_set_style_bg_color(s_tab[i], C_SEL, 0);
            lv_obj_set_style_bg_opa(s_tab[i], LV_OPA_COVER, 0);
            set_color(lbl, C_ACCENT);
        } else {
            lv_obj_set_style_bg_opa(s_tab[i], LV_OPA_TRANSP, 0);
            set_color(lbl, C_DIM);
        }
    }
    for (int p = 0; p < PAGE_COUNT; p++) {
        show(s_page_cont[p], p == (int)s_page);
    }
}

static void refresh(void)
{
    take_snapshot();
    update_topbar();
    switch (s_page) {
    case PAGE_STATUS: update_status(); break;
    case PAGE_STATIONS: update_stations(); break;
    case PAGE_LIGHTS: update_lights(); break;
    case PAGE_WARN: update_warnings(); break;
    default: update_settings(); break;
    }
    update_chrome(); /* after the page: list sizes feed the title */
}

/* ------------------------------------------------------------------ */
/* navigation                                                          */
/* ------------------------------------------------------------------ */

static int list_count(void)
{
    switch (s_page) {
    case PAGE_STATIONS: return s_st_n;
    case PAGE_LIGHTS: return s_li_n;
    case PAGE_WARN: return s_dn_n;
    case PAGE_SETTINGS: return SET_ROWS;
    default: return 0;
    }
}

static void list_move(int d)
{
    int n = list_count();
    if (n <= 0) {
        return;
    }
    int s = s_sel[s_page] + d;
    s = ((s % n) + n) % n; /* wrap around: two buttons only */
    s_sel[s_page] = s;
    /* drop the key so the new index wins */
    s_key_st_valid = s_key_st_valid && s_page != PAGE_STATIONS;
    s_key_li_valid = s_key_li_valid && s_page != PAGE_LIGHTS;
    s_key_dn_valid = s_key_dn_valid && s_page != PAGE_WARN;
}

static void leave_edit(void)
{
    if (s_sel[PAGE_SETTINGS] == 0 && s_edit_ch != (int)settings_get().channel) {
        settings_set_channel((uint16_t)s_edit_ch);
        rx_set_channel((uint16_t)s_edit_ch);
    }
    settings_save();
    s_nav = NAV_LIST;
}

static void settings_adjust(int d)
{
    settings_t c = settings_get();
    int r = s_sel[PAGE_SETTINGS];
    if (r == 0) {
        /* only changes the pending value; applied when the edit is confirmed */
        int ch = s_edit_ch + d * RX_CHANNEL_STEP_MHZ;
        if (ch < RX_CHANNEL_MIN_MHZ) ch = RX_CHANNEL_MAX_MHZ;
        if (ch > RX_CHANNEL_MAX_MHZ) ch = RX_CHANNEL_MIN_MHZ;
        s_edit_ch = ch;
    } else if (r == 3) {
        int b = (int)c.brightness + d * 10;
        if (b < 10) b = 10;
        if (b > 100) b = 100;
        settings_set_brightness((uint8_t)b);
        board_set_backlight(b);
    }
}

static void settings_activate(void)
{
    settings_t c = settings_get();
    switch (s_sel[PAGE_SETTINGS]) {
    case 0:
        s_edit_ch = c.channel;
        s_nav = NAV_EDIT;
        break;
    case 3:
        s_nav = NAV_EDIT;
        break;
    case 1:
        settings_set_sd_log(!c.sd_log);
        settings_save();
        break;
    case 2:
        settings_set_usb_out(!c.usb_out);
        settings_save();
        break;
    case 4:
        its_store_clear();
        rx_reset_stats();
        sd_log_reset_dropped();
        break;
    }
}

static void enter_pressed(void)
{
    switch (s_nav) {
    case NAV_TOP:
        if (s_page != PAGE_STATUS) {
            s_nav = NAV_LIST;
        }
        break;
    case NAV_LIST:
        if (s_page == PAGE_SETTINGS) {
            settings_activate();
        } else if (list_count() > 0) {
            if (s_page == PAGE_LIGHTS) {
                ui_lights_reset();
            }
            s_nav = NAV_DETAIL;
        }
        break;
    case NAV_DETAIL:
        if (s_page == PAGE_LIGHTS) {
            ui_lights_toggle_view();
        }
        break;
    case NAV_EDIT:
        leave_edit();
        break;
    }
}

static void back_pressed(void)
{
    switch (s_nav) {
    case NAV_EDIT:
        leave_edit();
        break;
    case NAV_DETAIL:
        s_nav = NAV_LIST;
        break;
    case NAV_LIST:
        s_nav = NAV_TOP;
        break;
    default:
        break;
    }
}

static void prev_next(int d)
{
    switch (s_nav) {
    case NAV_TOP:
        s_page = (page_t)(((int)s_page + d + PAGE_COUNT) % PAGE_COUNT);
        break;
    case NAV_LIST:
        list_move(d);
        break;
    case NAV_EDIT:
        settings_adjust(d);
        break;
    case NAV_DETAIL:
        if (s_page == PAGE_LIGHTS) {
            if (ui_lights_map_view()) {
                list_move(d); /* map view: next/previous intersection */
            } else {
                ui_lights_scroll(d);
            }
        } else {
            list_move(d);
        }
        break;
    }
}

static void buttons_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t ev;
    bool changed = false;
    while (xQueueReceive(s_btn_q, &ev, 0) == pdTRUE) {
        changed = true;
        switch (ev) {
        case BTN_EV_PREV: prev_next(-1); break;
        case BTN_EV_NEXT: prev_next(+1); break;
        case BTN_EV_ENTER_SHORT: enter_pressed(); break;
        case BTN_EV_ENTER_LONG: back_pressed(); break;
        default: break;
        }
    }
    if (changed) {
        refresh();
    }
}

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

void ui_init(QueueHandle_t button_queue)
{
    s_btn_q = button_queue;
    ui_fonts_init();

    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_ORANGE),
                                           true, ui_f14);
    lv_disp_set_theme(disp, th);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_topbar(scr);
    build_titlebar(scr);
    build_pages(scr);
    build_tabs(scr);

    refresh();
    lv_timer_create(refresh_timer_cb, REFRESH_MS, NULL);
    lv_timer_create(buttons_timer_cb, 20, NULL);
}
