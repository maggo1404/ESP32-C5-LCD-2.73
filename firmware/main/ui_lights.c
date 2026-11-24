/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_lights.h"

#include <stdio.h>
#include <string.h>

#include "ui_common.h"

#define ROW_H      26
#define INFO_H     36
#define MAX_ITEMS  (ITS_MAX_LANES + 24)
#define SPAT_STALE_US (6 * 1000000LL)

typedef struct {
    uint8_t is_header;
    uint8_t lane_id;
    uint8_t approach;
    uint8_t lane_type;
    uint16_t lane_mnv;
    uint8_t n;
    struct {
        uint16_t mnv;
        uint8_t light; /* its_light_t */
        uint8_t sg;
        int16_t secs;  /* -1 unknown */
    } c[ITS_MAX_CONNECTIONS];
} item_t;

static item_t s_items[MAX_ITEMS];
static int s_n_items;
static int s_top;
static bool s_map_view;
static int64_t s_now_us;
static const its_store_intersection_t *s_is;

static lv_obj_t *s_cont, *s_info, *s_lanes, *s_map, *s_msg;
static int s_body_h;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static lv_color_t light_color(uint8_t light, int64_t now_us)
{
    switch (light) {
    case ITS_LIGHT_RED:            return lv_color_hex(0xe5392f);
    case ITS_LIGHT_RED_YELLOW:     return lv_color_hex(0xf0883e);
    case ITS_LIGHT_YELLOW:         return lv_color_hex(0xf2c230);
    case ITS_LIGHT_GREEN:          return lv_color_hex(0x2fbf4a);
    case ITS_LIGHT_FLASHING_YELLOW:
        return ((now_us / 500000) & 1) ? lv_color_hex(0xf2c230) : lv_color_hex(0x5a4a10);
    default:                       return lv_color_hex(0x555d6b);
    }
}

static const its_signal_state_t *find_state(const its_store_intersection_t *is, uint8_t sg, int64_t now_us)
{
    if (!is->has_spat || sg == 0 || now_us - is->spat_rx_us > SPAT_STALE_US) {
        return NULL;
    }
    for (int i = 0; i < is->spat.n_states && i < ITS_MAX_SIGNAL_STATES; i++) {
        if (is->spat.states[i].signal_group == sg) {
            return &is->spat.states[i];
        }
    }
    return NULL;
}

static void state_info(const its_store_intersection_t *is, uint8_t sg, int64_t now_us, uint8_t *light, int16_t *secs)
{
    *light = ITS_LIGHT_OFF;
    *secs = -1;
    const its_signal_state_t *st = find_state(is, sg, now_us);
    if (!st) {
        return;
    }
    *light = (uint8_t)its_phase_light(st->phase);
    uint16_t tm = st->likely_time != ITS_TIMEMARK_UNKNOWN ? st->likely_time : st->min_end_time;
    if (tm != ITS_TIMEMARK_UNKNOWN) {
        int32_t t = its_timemark_remaining_tenths(is, tm, now_us);
        if (t >= 0) {
            int32_t s = (t + 9) / 10;
            *secs = (int16_t)(s > 999 ? 999 : s);
        }
    }
}

static bool lane_listed(const its_lane_t *l)
{
    return (l->direction & ITS_LANE_INGRESS) || l->lane_type == ITS_LANE_CROSSWALK;
}

static uint8_t appr_key(const its_lane_t *l)
{
    return l->ingress_approach ? l->ingress_approach : 255;
}

static void build_items(const its_store_intersection_t *is, int64_t now_us)
{
    s_n_items = 0;
    if (!is || !is->has_map) {
        return;
    }
    const its_map_intersection_t *m = &is->map;
    uint8_t order[ITS_MAX_LANES];
    int n = 0;
    for (int i = 0; i < m->n_lanes && i < ITS_MAX_LANES; i++) {
        if (!lane_listed(&m->lanes[i])) {
            continue;
        }
        /* insertion sort by (approach, lane id) */
        int j = n++;
        while (j > 0) {
            const its_lane_t *p = &m->lanes[order[j - 1]];
            const its_lane_t *c = &m->lanes[i];
            if (appr_key(p) > appr_key(c) || (appr_key(p) == appr_key(c) && p->lane_id > c->lane_id)) {
                order[j] = order[j - 1];
                j--;
            } else {
                break;
            }
        }
        order[j] = (uint8_t)i;
    }
    int last_appr = -1;
    for (int k = 0; k < n && s_n_items < MAX_ITEMS - 1; k++) {
        const its_lane_t *l = &m->lanes[order[k]];
        if (appr_key(l) != last_appr) {
            last_appr = appr_key(l);
            item_t *h = &s_items[s_n_items++];
            memset(h, 0, sizeof(*h));
            h->is_header = 1;
            h->approach = l->ingress_approach;
        }
        item_t *it = &s_items[s_n_items++];
        memset(it, 0, sizeof(*it));
        it->lane_id = l->lane_id;
        it->approach = l->ingress_approach;
        it->lane_type = l->lane_type;
        it->lane_mnv = l->maneuvers;
        it->n = l->n_connections > ITS_MAX_CONNECTIONS ? ITS_MAX_CONNECTIONS : l->n_connections;
        for (int c = 0; c < it->n; c++) {
            it->c[c].mnv = l->conn[c].connecting_maneuver ? l->conn[c].connecting_maneuver : l->maneuvers;
            it->c[c].sg = l->conn[c].signal_group;
            state_info(is, l->conn[c].signal_group, now_us, &it->c[c].light, &it->c[c].secs);
        }
    }
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_text(lv_draw_ctx_t *ctx, int x, int y, int w, const char *s, lv_color_t col, const lv_font_t *f,
                      lv_text_align_t al)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = col;
    d.font = f;
    d.align = al;
    lv_area_t a = {x, y, x + w - 1, y + f->line_height};
    lv_draw_label(ctx, &d, &a, s, NULL);
}

static void draw_line(lv_draw_ctx_t *ctx, int x1, int y1, int x2, int y2, int w, lv_color_t col, bool round)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col;
    d.width = w;
    d.round_start = round;
    d.round_end = round;
    lv_point_t p1 = {x1, y1}, p2 = {x2, y2};
    lv_draw_line(ctx, &d, &p1, &p2);
}

static void draw_dot(lv_draw_ctx_t *ctx, int cx, int cy, int r, lv_color_t col)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = col;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 1;
    d.border_color = lv_color_hex(0x2a303b);
    lv_area_t a = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(ctx, &d, &a);
}

/* one manoeuvre arrow in a 16x16 cell centred at (cx, cy) */
static void draw_arrow(lv_draw_ctx_t *ctx, int cx, int cy, uint16_t kind, lv_color_t c)
{
    const int w = 2;
    switch (kind) {
    case ITS_MNV_STRAIGHT:
        draw_line(ctx, cx, cy + 7, cx, cy - 7, w, c, true);
        draw_line(ctx, cx, cy - 7, cx - 4, cy - 3, w, c, true);
        draw_line(ctx, cx, cy - 7, cx + 4, cy - 3, w, c, true);
        break;
    case ITS_MNV_LEFT:
        draw_line(ctx, cx + 2, cy + 7, cx + 2, cy, w, c, true);
        draw_line(ctx, cx + 2, cy, cx - 7, cy, w, c, true);
        draw_line(ctx, cx - 7, cy, cx - 3, cy - 4, w, c, true);
        draw_line(ctx, cx - 7, cy, cx - 3, cy + 4, w, c, true);
        break;
    case ITS_MNV_RIGHT:
        draw_line(ctx, cx - 2, cy + 7, cx - 2, cy, w, c, true);
        draw_line(ctx, cx - 2, cy, cx + 7, cy, w, c, true);
        draw_line(ctx, cx + 7, cy, cx + 3, cy - 4, w, c, true);
        draw_line(ctx, cx + 7, cy, cx + 3, cy + 4, w, c, true);
        break;
    case ITS_MNV_UTURN:
        draw_line(ctx, cx + 4, cy + 7, cx + 4, cy - 3, w, c, true);
        draw_line(ctx, cx + 4, cy - 3, cx, cy - 7, w, c, true);
        draw_line(ctx, cx, cy - 7, cx - 4, cy - 3, w, c, true);
        draw_line(ctx, cx - 4, cy - 3, cx - 4, cy + 4, w, c, true);
        draw_line(ctx, cx - 4, cy + 4, cx - 7, cy + 1, w, c, true);
        draw_line(ctx, cx - 4, cy + 4, cx - 1, cy + 1, w, c, true);
        break;
    default:
        break;
    }
}

static int arrows_of(uint16_t mnv, uint16_t out[4])
{
    int n = 0;
    if (mnv & ITS_MNV_LEFT) out[n++] = ITS_MNV_LEFT;
    if (mnv & ITS_MNV_STRAIGHT) out[n++] = ITS_MNV_STRAIGHT;
    if (mnv & ITS_MNV_RIGHT) out[n++] = ITS_MNV_RIGHT;
    if (mnv & ITS_MNV_UTURN) out[n++] = ITS_MNV_UTURN;
    return n;
}

static void lanes_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int vis = s_body_h / ROW_H;
    char buf[32];

    for (int r = 0; r < vis; r++) {
        int idx = s_top + r;
        if (idx >= s_n_items) {
            break;
        }
        const item_t *it = &s_items[idx];
        int y = a.y1 + r * ROW_H;
        int cy = y + ROW_H / 2;
        if (it->is_header) {
            if (it->approach) {
                snprintf(buf, sizeof(buf), "Zufahrt %u", it->approach);
            } else {
                snprintf(buf, sizeof(buf), "Fußwege / ohne Zufahrt");
            }
            draw_text(ctx, a.x1 + 4, cy - ui_f14->line_height / 2, 300, buf, lv_color_hex(0x3ea6ff), ui_f14,
                      LV_TEXT_ALIGN_LEFT);
            draw_line(ctx, a.x1 + 4, y + ROW_H - 2, a.x2 - 4, y + ROW_H - 2, 1, lv_color_hex(0x2a3a4e), false);
            continue;
        }
        snprintf(buf, sizeof(buf), "%u", it->lane_id);
        draw_text(ctx, a.x1 + 4, cy - ui_f14->line_height / 2, 32, buf, C_TEXT, ui_f14, LV_TEXT_ALIGN_LEFT);

        int x = a.x1 + 40;
        if (it->lane_type == ITS_LANE_CROSSWALK && it->n == 0) {
            draw_text(ctx, x, cy - ui_f14->line_height / 2, 120, "Fußgänger", C_DIM, ui_f14, LV_TEXT_ALIGN_LEFT);
            continue;
        }
        if (it->n == 0) {
            uint16_t ar[4];
            int na = arrows_of(it->lane_mnv, ar);
            for (int k = 0; k < na; k++) {
                draw_arrow(ctx, x + 8 + k * 18, cy, ar[k], C_TEXT);
            }
            if (!na) {
                draw_text(ctx, x, cy - ui_f14->line_height / 2, 60, "-", C_DIM, ui_f14, LV_TEXT_ALIGN_LEFT);
            }
            continue;
        }
        for (int c = 0; c < it->n; c++) {
            uint16_t ar[4];
            int na = arrows_of(it->c[c].mnv, ar);
            if (it->lane_type == ITS_LANE_CROSSWALK) {
                na = 0;
            }
            int need = (na ? na * 18 : 0) + 6 + 14 + 4 + 32 + 8;
            if (x + need > a.x2) {
                break;
            }
            for (int k = 0; k < na; k++) {
                draw_arrow(ctx, x + 8 + k * 18, cy, ar[k], C_TEXT);
            }
            x += na * 18 + 6;
            draw_dot(ctx, x + 7, cy, 7, light_color(it->c[c].light, s_now_us));
            x += 14 + 4;
            if (it->c[c].secs >= 0) {
                snprintf(buf, sizeof(buf), "%ds", it->c[c].secs);
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            draw_text(ctx, x, cy - ui_f14->line_height / 2, 32, buf, it->c[c].secs >= 0 ? C_TEXT : C_DIM, ui_f14,
                      LV_TEXT_ALIGN_LEFT);
            x += 32 + 8;
        }
    }
}

/* colour of a lane on the map */
static lv_color_t map_lane_color(const its_store_intersection_t *is, const its_lane_t *l, int64_t now_us)
{
    if (l->lane_type == ITS_LANE_CROSSWALK && l->n_connections == 0) {
        return lv_color_hex(0x9aa3b2);
    }
    for (int c = 0; c < l->n_connections && c < ITS_MAX_CONNECTIONS; c++) {
        uint8_t light;
        int16_t secs;
        state_info(is, l->conn[c].signal_group, now_us, &light, &secs);
        if (light != ITS_LIGHT_OFF) {
            return light_color(light, now_us);
        }
    }
    return (l->direction & ITS_LANE_INGRESS) ? lv_color_hex(0x7b8696) : lv_color_hex(0x46556b);
}

static void map_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const its_store_intersection_t *is = s_is;
    if (!is || !is->has_map) {
        return;
    }
    const its_map_intersection_t *m = &is->map;

    /* bounding box over all nodes and the reference point */
    int32_t minx = 0, maxx = 0, miny = 0, maxy = 0;
    int nodes = 0;
    for (int i = 0; i < m->n_lanes && i < ITS_MAX_LANES; i++) {
        const its_lane_t *l = &m->lanes[i];
        for (int k = 0; k < l->n_nodes && k < ITS_MAX_LANE_NODES; k++) {
            if (l->node_x_cm[k] < minx) minx = l->node_x_cm[k];
            if (l->node_x_cm[k] > maxx) maxx = l->node_x_cm[k];
            if (l->node_y_cm[k] < miny) miny = l->node_y_cm[k];
            if (l->node_y_cm[k] > maxy) maxy = l->node_y_cm[k];
            nodes++;
        }
    }
    if (!nodes) {
        return;
    }
    const int margin = 14;
    int pw = (a.x2 - a.x1 + 1) - 2 * margin;
    int ph = (a.y2 - a.y1 + 1) - 2 * margin;
    int64_t wcm = maxx - minx, hcm = maxy - miny;
    if (wcm < 1000) wcm = 1000; /* at least 10 m so tiny maps are not blown up */
    if (hcm < 1000) hcm = 1000;
    /* scale in 1/1024 px per cm */
    int64_t sx = ((int64_t)pw * 1024) / wcm;
    int64_t sy = ((int64_t)ph * 1024) / hcm;
    int64_t s = sx < sy ? sx : sy;
    int64_t used_w = (maxx - minx) * s / 1024, used_h = (maxy - miny) * s / 1024;
    int ox = a.x1 + margin + (int)((pw - used_w) / 2);
    int oy = a.y1 + margin + (int)((ph - used_h) / 2);

#define PX(xcm) (ox + (int)(((int64_t)((xcm) - minx)) * s / 1024))
#define PY(ycm) (oy + (int)(((int64_t)(maxy - (ycm))) * s / 1024))

    /* pass 0: non-ingress, pass 1: ingress on top */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < m->n_lanes && i < ITS_MAX_LANES; i++) {
            const its_lane_t *l = &m->lanes[i];
            bool ingress = (l->direction & ITS_LANE_INGRESS) && l->lane_type != ITS_LANE_CROSSWALK;
            if ((pass == 1) != ingress || l->n_nodes < 1) {
                continue;
            }
            lv_color_t col = map_lane_color(is, l, s_now_us);
            int w = ingress ? 6 : (l->lane_type == ITS_LANE_CROSSWALK ? 2 : 3);
            for (int k = 0; k + 1 < l->n_nodes && k + 1 < ITS_MAX_LANE_NODES; k++) {
                draw_line(ctx, PX(l->node_x_cm[k]), PY(l->node_y_cm[k]), PX(l->node_x_cm[k + 1]),
                          PY(l->node_y_cm[k + 1]), w, col, true);
            }
            if (ingress) {
                /* stop line end (first node) as a white cap, lane id at the far end */
                int last = l->n_nodes - 1 < ITS_MAX_LANE_NODES ? l->n_nodes - 1 : ITS_MAX_LANE_NODES - 1;
                draw_dot(ctx, PX(l->node_x_cm[0]), PY(l->node_y_cm[0]), 3, lv_color_hex(0xffffff));
                if (last > 0) {
                    char b[8];
                    snprintf(b, sizeof(b), "%u", l->lane_id);
                    draw_text(ctx, PX(l->node_x_cm[last]) + 4, PY(l->node_y_cm[last]) - 7, 24, b, C_DIM, ui_f14,
                              LV_TEXT_ALIGN_LEFT);
                }
            }
        }
    }
    /* reference point */
    draw_dot(ctx, PX(0), PY(0), 2, lv_color_hex(0x3ea6ff));
    draw_text(ctx, a.x1 + 4, a.y1 + 2, 20, "N", C_DIM, ui_f14, LV_TEXT_ALIGN_LEFT);
    draw_line(ctx, a.x1 + 10, a.y1 + 22, a.x1 + 10, a.y1 + 34, 1, C_DIM, false);
#undef PX
#undef PY
}

/* ------------------------------------------------------------------ */
/* public                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *plain_obj(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_lights_create(lv_obj_t *parent, int w, int h)
{
    s_cont = plain_obj(parent, 0, 0, w, h);
    s_body_h = h - INFO_H;

    s_info = lv_label_create(s_cont);
    lv_obj_set_pos(s_info, 6, 2);
    lv_obj_set_width(s_info, w - 12);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_info, ui_f14, 0);
    lv_obj_set_style_text_color(s_info, C_TEXT, 0);
    lv_label_set_text(s_info, "");

    s_lanes = plain_obj(s_cont, 0, INFO_H, w, s_body_h);
    lv_obj_add_event_cb(s_lanes, lanes_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    s_map = plain_obj(s_cont, 0, INFO_H, w, s_body_h);
    lv_obj_add_event_cb(s_map, map_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_flag(s_map, LV_OBJ_FLAG_HIDDEN);

    s_msg = lv_label_create(s_cont);
    lv_obj_set_width(s_msg, w - 24);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_msg, ui_f14, 0);
    lv_obj_set_style_text_color(s_msg, C_DIM, 0);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    return s_cont;
}

static void fmt_age(char *b, size_t n, int64_t us)
{
    int64_t s = us / 1000000;
    if (s < 0) s = 0;
    if (s < 100) snprintf(b, n, "%ds", (int)s);
    else if (s < 6000) snprintf(b, n, "%dmin", (int)(s / 60));
    else snprintf(b, n, "%dh", (int)(s / 3600));
}

void ui_lights_update(const its_store_intersection_t *is, int64_t now_us)
{
    s_is = is;
    s_now_us = now_us;
    build_items(is, now_us);

    int vis = s_body_h / ROW_H;
    if (s_top > s_n_items - vis) s_top = s_n_items - vis;
    if (s_top < 0) s_top = 0;

    if (!is) {
        lv_label_set_text(s_info, "");
        lv_label_set_text(s_msg, "Keine Kreuzung ausgewählt");
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char b[128], a1[16], a2[16];
    if (is->has_map) fmt_age(a1, sizeof(a1), now_us - is->map_rx_us); else strcpy(a1, "--");
    if (is->has_spat) fmt_age(a2, sizeof(a2), now_us - is->spat_rx_us); else strcpy(a2, "--");
    const char *name = is->name[0] ? is->name : NULL;
    char nm[48];
    if (name) snprintf(nm, sizeof(nm), "%s", name);
    else snprintf(nm, sizeof(nm), "Kreuzung %u/%u", (unsigned)is->region, (unsigned)is->id);
    char dropped[32] = "";
    if (is->has_map && is->map.lanes_dropped > 0) {
        snprintf(dropped, sizeof(dropped), " +%u fehlen", (unsigned)is->map.lanes_dropped);
    }
    snprintf(b, sizeof(b), "%s\nMAP %s  SPAT %s  %s%s%s", nm, a1, a2, s_map_view ? "Ansicht: Karte" : "Spuren",
             (!s_map_view && s_n_items > vis) ? "  (<> scrollen)" : "", dropped);
    lv_label_set_text(s_info, b);

    if (!is->has_map) {
        lv_label_set_text(s_msg, "Noch keine MAP-Daten (Spurgeometrie) empfangen.");
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    } else if (!is->has_spat) {
        /* still draw lanes (grey dots) but tell the user */
        lv_label_set_text(s_msg, "");
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_lanes);
    lv_obj_invalidate(s_map);
}

void ui_lights_scroll(int delta)
{
    s_top += delta;
    int vis = s_body_h / ROW_H;
    if (s_top > s_n_items - vis) s_top = s_n_items - vis;
    if (s_top < 0) s_top = 0;
    lv_obj_invalidate(s_lanes);
}

void ui_lights_toggle_view(void)
{
    s_map_view = !s_map_view;
    if (s_map_view) {
        lv_obj_add_flag(s_lanes, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_map, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_lanes, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_map, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_lights_map_view(void) { return s_map_view; }

void ui_lights_reset(void)
{
    s_top = 0;
    if (s_map_view) {
        ui_lights_toggle_view();
    }
}
