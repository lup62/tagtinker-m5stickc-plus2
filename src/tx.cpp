/*
 * Transmit engine.
 *
 * Faithful port of the Flipper transmit scene: same frame order, repeat
 * counts and settle delays (ping -> image params -> data frames -> refresh,
 * wake x400 for SmartTAG Color 2.6, chunked text for very large tags).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "tx.h"

#include <LittleFS.h>
#include "esp_heap_caps.h"

#include "ir.h"
#include "proto.h"
#include "store.h"
#include "tt_font.h"

#define TX_FULL_JOB_PIXEL_LIMIT 49152U
#define TX_COLOR26_WAKE_REPEATS 400U
#define TX_COLOR26_TEXT_ROWS 16U
#define TX_COLOR26_PACKED_BYTES \
    (((size_t)TAGTINKER_COLOR26_GLASS_W * (size_t)TAGTINKER_COLOR26_GLASS_H + 7U) / 8U)
#define TX_BMP_MAX_FILE (256U * 1024U)

static TaskHandle_t s_task = nullptr;
static QueueHandle_t s_queue = nullptr;
static SemaphoreHandle_t s_lock = nullptr;
static TxStatus s_status;
static volatile bool s_cancel = false;
static TxJob s_job;      /* job being executed (owned by the task) */
static Target s_target;  /* snapshot of the target for this job */

/* ------------------------------------------------------------- status */

static void st_set(bool busy, TxJobType type) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = busy;
    s_status.type = type;
    xSemaphoreGive(s_lock);
}

static void st_progress(uint32_t done, uint32_t total) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.done = done;
    s_status.total = total;
    xSemaphoreGive(s_lock);
}

static void st_msg(const char* m) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_status.msg, m, sizeof(s_status.msg));
    xSemaphoreGive(s_lock);
}

TxStatus tx_status() {
    TxStatus st;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_status;
    xSemaphoreGive(s_lock);
    return st;
}

const char* tx_job_label(TxJobType t) {
    switch(t) {
    case TX_PING_FLASH: return "Ping / Flash";
    case TX_BROADCAST_PAGE: return "Broadcast page";
    case TX_BROADCAST_DEBUG: return "Debug screen";
    case TX_TEXT: return "Text";
    case TX_IMAGE: return "Image";
    case TX_IR_TEST: return "IR test";
    default: return "-";
    }
}

static void* big_alloc(size_t n) {
    void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!p) p = calloc(1, n);
    return p;
}

/* ------------------------------------------------------------- frames */

static bool tx_send_frame(const uint8_t* frame, size_t len, uint16_t repeats, uint8_t delay = 1) {
    if(s_cancel) return false;
    return ir_transmit(frame, len, repeats & 0x7FFFU, delay);
}

static void tx_wait(uint32_t ms) {
    /* Interruptible delay. */
    uint32_t start = millis();
    while(!s_cancel && (millis() - start) < ms) vTaskDelay(pdMS_TO_TICKS(ms > 10 ? 10 : ms));
}

static bool tx_send_ping(const uint8_t plid[4]) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    size_t len = tagtinker_make_ping_frame(frame, plid);
    return tx_send_frame(frame, len, 80);
}

static bool tx_send_wake(const uint8_t plid[4], uint16_t repeats) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    size_t len = tagtinker_make_wake_frame(frame, plid);
    return tx_send_frame(frame, len, repeats);
}

static bool tx_send_refresh(const uint8_t plid[4]) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    size_t len = tagtinker_make_refresh_frame(frame, plid);
    return tx_send_frame(frame, len, 20);
}

/* Shared progress counters across multi-chunk jobs. */
static uint32_t s_prog_done = 0;
static uint32_t s_prog_total = 0;

static bool tx_send_color26_payload(const uint8_t plid[4], uint8_t page, const TagTinkerImagePayload* payload) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    size_t frame_count = payload->byte_count / TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME;
    s_prog_done = 0;
    s_prog_total = frame_count + 3;
    st_progress(0, s_prog_total);

    bool ok = tx_send_wake(plid, TX_COLOR26_WAKE_REPEATS);
    st_progress(++s_prog_done, s_prog_total);
    if(ok) tx_wait(50);
    if(ok) {
        size_t len = tagtinker_make_image_param_frame(
            frame,
            plid,
            (uint16_t)payload->byte_count,
            payload->comp_type,
            page,
            TAGTINKER_COLOR26_WIRE_W,
            TAGTINKER_COLOR26_WIRE_H,
            0,
            0);
        ok = tx_send_frame(frame, len, 1);
        st_progress(++s_prog_done, s_prog_total);
    }
    if(ok) tx_wait(50);

    for(size_t i = 0; ok && i < frame_count; i++) {
        size_t len = tagtinker_make_image_data_frame(
            frame, plid, (uint16_t)i, &payload->data[i * TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME]);
        ok = tx_send_frame(frame, len, 1);
        st_progress(++s_prog_done, s_prog_total);
        if(ok && (i + 1U) < frame_count) tx_wait(50);
    }

    if(ok) tx_wait(50);
    if(ok) {
        size_t len = tagtinker_make_refresh_frame(frame, plid);
        ok = tx_send_frame(frame, len, 1);
        st_progress(++s_prog_done, s_prog_total);
    }
    return ok;
}

static bool tx_send_image_start(
    const uint8_t plid[4],
    uint16_t byte_count,
    uint8_t comp_type,
    uint8_t page,
    uint16_t width,
    uint16_t height,
    uint16_t pos_x,
    uint16_t pos_y) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    size_t len = tagtinker_make_image_param_frame(
        frame, plid, byte_count, comp_type, page, width, height, pos_x, pos_y);
    return tx_send_frame(frame, len, 15);
}

static bool tx_send_payload_frames(
    const uint8_t plid[4],
    const TagTinkerImagePayload* payload,
    uint8_t page,
    uint16_t width,
    uint16_t height,
    uint16_t pos_x,
    uint16_t pos_y) {
    uint8_t frame[TAGTINKER_MAX_FRAME_SIZE];
    if(payload->byte_count > 0xFFFFU) {
        st_msg("Image too complex for one upload");
        return false;
    }
    bool ok = tx_send_image_start(
        plid, (uint16_t)payload->byte_count, payload->comp_type, page, width, height, pos_x, pos_y);
    st_progress(++s_prog_done, s_prog_total);
    if(ok) tx_wait(50);

    size_t frame_count = payload->byte_count / TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME;
    for(size_t i = 0; ok && i < frame_count; i++) {
        size_t len = tagtinker_make_image_data_frame(
            frame, plid, (uint16_t)i, &payload->data[i * TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME]);
        ok = tx_send_frame(frame, len, g_settings.data_repeats);
        st_progress(++s_prog_done, s_prog_total);
        if(ok && ((i + 1U) % 32U) == 0U && (i + 1U) < frame_count) vTaskDelay(1);
    }
    return ok;
}

/* ping -> params -> data -> refresh */
static bool tx_send_full_payload(
    const uint8_t plid[4],
    const TagTinkerImagePayload* payload,
    uint8_t page,
    uint16_t width,
    uint16_t height,
    uint16_t pos_x,
    uint16_t pos_y) {
    s_prog_done = 0;
    s_prog_total = payload->byte_count / TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME + 3;
    st_progress(0, s_prog_total);
    bool ok = tx_send_ping(plid);
    st_progress(++s_prog_done, s_prog_total);
    if(ok) tx_wait(50);
    if(ok) ok = tx_send_payload_frames(plid, payload, page, width, height, pos_x, pos_y);
    if(ok) tx_wait(50);
    if(ok) ok = tx_send_refresh(plid);
    st_progress(++s_prog_done, s_prog_total);
    return ok;
}

/* ------------------------------------------------------------- pages */

static uint8_t resolve_page(uint8_t ui_page) {
    if(tagtinker_profile_uses_ui_page(&s_target.profile)) {
        return tagtinker_color26_resolve_page(ui_page);
    }
    uint8_t p = (ui_page > 0U) ? (uint8_t)(ui_page - 1U) : 0U;
    return p > 7U ? 7U : p;
}

/* ------------------------------------------------------------- text */

static uint16_t tx_pick_chunk_height(uint16_t width, uint16_t height) {
    if(width == 0U || height == 0U) return 1U;
    size_t per_plane_budget = 8192U;
    uint16_t chunk_h = (uint16_t)(per_plane_budget / width);
    if(chunk_h == 0U) chunk_h = 1U;
    if(chunk_h > height) chunk_h = height;
    if(chunk_h >= 16U) {
        chunk_h = (uint16_t)(chunk_h & ~7U);
        if(chunk_h == 0U) chunk_h = 8U;
    }
    if(chunk_h > height) chunk_h = height;
    if(chunk_h == 0U) chunk_h = 1U;
    return chunk_h;
}

static uint32_t tx_chunk_settle_delay_ms(uint16_t width, uint16_t height) {
    size_t work_pixels = (size_t)width * height;
    uint32_t delay_ms = 500U + (uint32_t)(work_pixels / 20U);
    if(delay_ms < 800U) delay_ms = 800U;
    if(delay_ms > 2000U) delay_ms = 2000U;
    return delay_ms;
}

static uint8_t tx_packed_bit(const uint8_t* packed, size_t idx) {
    return (packed[idx / 8U] >> (7U - (idx % 8U))) & 1U;
}

static void tx_packed_set(uint8_t* packed, size_t idx, uint8_t val) {
    size_t byte = idx / 8U;
    uint8_t mask = (uint8_t)(1U << (7U - (idx % 8U)));
    if(val) packed[byte] |= mask;
    else packed[byte] &= (uint8_t)~mask;
}

struct Color26TextCtx {
    const uint8_t* p1;
    const uint8_t* p2;
};

static uint8_t tx_color26_text_pixel(size_t idx, void* ctx) {
    const Color26TextCtx* c = (const Color26TextCtx*)ctx;
    size_t count = (size_t)TAGTINKER_COLOR26_WIRE_W * (size_t)TAGTINKER_COLOR26_WIRE_H;
    uint8_t plane = 0;
    if(idx >= count) {
        plane = 1;
        idx -= count;
    }
    uint16_t px = (uint16_t)(idx % TAGTINKER_COLOR26_WIRE_W);
    uint16_t py = (uint16_t)(idx / TAGTINKER_COLOR26_WIRE_W);
    uint16_t bx = 0, by = 0;
    tagtinker_color26_proto_to_glass(TAGTINKER_COLOR26_WIRE_W, px, py, &bx, &by);
    size_t si = (size_t)by * TAGTINKER_COLOR26_GLASS_W + bx;
    if(plane == 0) return tx_packed_bit(c->p1, si);
    if(!c->p2) return 1U;
    return tx_packed_bit(c->p2, si);
}

static bool tx_render_color26_text_plane(
    uint8_t* packed, const char* text, uint8_t bg, uint8_t fg, uint8_t padding_pct) {
    const uint16_t w = TAGTINKER_COLOR26_GLASS_W;
    const uint16_t h = TAGTINKER_COLOR26_GLASS_H;
    memset(packed, bg ? 0xFF : 0x00, TX_COLOR26_PACKED_BYTES);
    uint8_t* row = (uint8_t*)malloc((size_t)w * TX_COLOR26_TEXT_ROWS);
    if(!row) return false;
    for(uint16_t y = 0; y < h; y = (uint16_t)(y + TX_COLOR26_TEXT_ROWS)) {
        uint16_t rh = (uint16_t)(h - y);
        if(rh > TX_COLOR26_TEXT_ROWS) rh = TX_COLOR26_TEXT_ROWS;
        render_text_region_ex(row, w, h, y, rh, text, bg, fg, padding_pct);
        for(uint16_t ry = 0; ry < rh; ry++)
            for(uint16_t x = 0; x < w; x++)
                tx_packed_set(packed, (size_t)(y + ry) * w + x, row[(size_t)ry * w + x]);
    }
    free(row);
    return true;
}

static bool tx_send_color26_text(const TxJob& job, uint8_t page) {
    bool accent_capable = target_supports_accent(s_target);
    bool accent_text = accent_capable && job.accent;
    TagTinkerTagColor accent_color = s_target.profile.color;
    uint8_t* primary = (uint8_t*)malloc(TX_COLOR26_PACKED_BYTES);
    uint8_t* secondary = nullptr;
    if(!primary) return false;
    if(accent_text) {
        secondary = (uint8_t*)malloc(TX_COLOR26_PACKED_BYTES);
        if(!secondary) accent_text = false;
    }
    const char* text = job.text.c_str();
    uint8_t bg = job.invert ? 0 : 1;
    uint8_t fg = job.invert ? 1 : 0;
    bool ok;
    if(accent_text) {
        uint8_t fg_primary = (accent_color == TagTinkerTagColorYellow) ? 0 : 1;
        ok = tx_render_color26_text_plane(primary, text, bg, fg_primary, job.padding);
        if(ok) ok = tx_render_color26_text_plane(secondary, text, 1, 0, job.padding);
    } else {
        ok = tx_render_color26_text_plane(primary, text, bg, fg, job.padding);
    }
    if(!ok) {
        free(primary);
        free(secondary);
        return false;
    }
    Color26TextCtx ctx = {primary, secondary};
    TagTinkerImagePayload payload;
    ok = tagtinker_encode_fn_payload(
        tx_color26_text_pixel,
        &ctx,
        (size_t)TAGTINKER_COLOR26_WIRE_W * (size_t)TAGTINKER_COLOR26_WIRE_H * 2U,
        (TagTinkerCompressionMode)g_settings.compression,
        &payload);
    free(primary);
    free(secondary);
    if(!ok) return false;
    ok = tx_send_color26_payload(s_target.plid, page, &payload);
    tagtinker_free_image_payload(&payload);
    return ok;
}

static void render_text_planes(
    const TxJob& job,
    uint8_t* primary,
    uint8_t* secondary,
    uint16_t w,
    uint16_t full_h,
    uint16_t y,
    uint16_t rh,
    bool accent_text,
    TagTinkerTagColor accent_color) {
    const char* text = job.text.c_str();
    if(accent_text) {
        uint8_t bg_primary = job.invert ? 0 : 1;
        uint8_t fg_primary = (accent_color == TagTinkerTagColorYellow) ? 0 : 1;
        render_text_region_ex(primary, w, full_h, y, rh, text, bg_primary, fg_primary, job.padding);
        render_text_region_ex(secondary, w, full_h, y, rh, text, 1, 0, job.padding);
    } else {
        render_text_region_ex(
            primary, w, full_h, y, rh, text, job.invert ? 0 : 1, job.invert ? 1 : 0, job.padding);
        if(secondary) memset(secondary, 1, (size_t)w * rh);
    }
}

static bool tx_text_job(const TxJob& job) {
    if(!target_supports_graphics(s_target)) {
        st_msg("Tag has no known graphic size");
        return false;
    }
    if(!job.text.length()) {
        st_msg("Empty text");
        return false;
    }
    uint8_t page = resolve_page(job.page);
    if(tagtinker_profile_needs_wh_swap(&s_target.profile)) {
        return tx_send_color26_text(job, page);
    }

    uint16_t w = s_target.profile.width;
    uint16_t h = s_target.profile.height;
    bool accent_capable = target_supports_accent(s_target);
    bool accent_text = accent_capable && job.accent;
    bool use_second_plane = accent_capable || job.accent;
    TagTinkerTagColor accent_color = s_target.profile.color;
    size_t pixel_count = (size_t)w * h;

    /* Full job when it fits (same threshold as the Flipper app). */
    if(pixel_count * (use_second_plane ? 2U : 1U) <= TX_FULL_JOB_PIXEL_LIMIT) {
        uint8_t* primary = (uint8_t*)big_alloc(pixel_count);
        uint8_t* secondary = use_second_plane ? (uint8_t*)big_alloc(pixel_count) : nullptr;
        if(!primary || (use_second_plane && !secondary)) {
            free(primary);
            free(secondary);
            st_msg("Out of memory");
            return false;
        }
        render_text_planes(job, primary, secondary, w, h, 0, h, accent_text, accent_color);
        TagTinkerImagePayload payload;
        bool ok = tagtinker_encode_planes_payload(
            primary, secondary, pixel_count, (TagTinkerCompressionMode)g_settings.compression, &payload);
        free(primary);
        free(secondary);
        if(!ok) {
            st_msg("Encode failed");
            return false;
        }
        ok = tx_send_full_payload(s_target.plid, &payload, page, w, h, 0, 0);
        tagtinker_free_image_payload(&payload);
        return ok;
    }

    /* Large tag: stream in horizontal chunks, refresh after each. */
    uint16_t chunk_h = tx_pick_chunk_height(w, h);
    uint8_t* primary = (uint8_t*)big_alloc((size_t)w * chunk_h);
    uint8_t* secondary = use_second_plane ? (uint8_t*)big_alloc((size_t)w * chunk_h) : nullptr;
    if(!primary || (use_second_plane && !secondary)) {
        free(primary);
        free(secondary);
        st_msg("Out of memory");
        return false;
    }

    bool ok = true;
    uint32_t chunks = (h + chunk_h - 1) / chunk_h;
    uint32_t chunk_idx = 0;
    for(uint16_t y = 0; ok && y < h; y = (uint16_t)(y + chunk_h)) {
        uint16_t actual_h = h - y;
        if(actual_h > chunk_h) actual_h = chunk_h;
        render_text_planes(job, primary, secondary, w, h, y, actual_h, accent_text, accent_color);

        TagTinkerImagePayload payload;
        ok = tagtinker_encode_planes_payload(
            primary,
            secondary,
            (size_t)w * actual_h,
            (TagTinkerCompressionMode)g_settings.compression,
            &payload);
        if(!ok) {
            st_msg("Encode failed");
            break;
        }
        s_prog_done = 0;
        s_prog_total = payload.byte_count / TAGTINKER_IMAGE_DATA_BYTES_PER_FRAME + 3;
        char m[48];
        snprintf(m, sizeof(m), "Chunk %lu/%lu", (unsigned long)(chunk_idx + 1), (unsigned long)chunks);
        st_msg(m);

        ok = tx_send_ping(s_target.plid);
        if(ok) tx_wait(10);
        if(ok) ok = tx_send_payload_frames(s_target.plid, &payload, page, w, actual_h, 0, y);
        tagtinker_free_image_payload(&payload);
        if(ok) tx_wait(10);
        if(ok) ok = tx_send_refresh(s_target.plid);
        chunk_idx++;
        if(ok && (uint16_t)(y + actual_h) < h) tx_wait(tx_chunk_settle_delay_ms(w, actual_h));
    }
    free(primary);
    free(secondary);
    return ok;
}

/* ------------------------------------------------------------- BMP */

struct BmpInfo {
    uint32_t data_offset;
    uint32_t row_stride;
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    bool top_down;
};

struct BmpCtx {
    const uint8_t* file;
    size_t file_len;
    BmpInfo info;
    uint16_t tx_w;
    uint16_t tx_h;
    bool second_plane;
};

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool bmp_parse(const uint8_t* m, size_t len, BmpInfo& info) {
    if(len < 54 || m[0] != 'B' || m[1] != 'M') return false;
    uint16_t bpp = m[28] | (m[29] << 8);
    if(!(bpp == 1 || bpp == 2)) return false;
    info.bpp = bpp;
    uint32_t raw_w = le32(&m[18]);
    uint32_t raw_h = le32(&m[22]);
    info.top_down = (raw_h & 0x80000000U) != 0U;
    uint32_t abs_h = info.top_down ? (~raw_h + 1U) : raw_h;
    if(raw_w == 0U || raw_w > UINT16_MAX || abs_h == 0U || abs_h > UINT16_MAX) return false;
    info.width = (uint16_t)raw_w;
    info.height = (uint16_t)abs_h;
    info.data_offset = le32(&m[10]);
    info.row_stride = ((info.width + 31U) / 32U) * 4U;
    return true;
}

/* BMP bit 1 = white -> ESL value 0? No: the Flipper maps bit set -> 0. */
static inline uint8_t bmp_bit(const BmpCtx* c, uint16_t sx, uint16_t sy, uint8_t plane) {
    const BmpInfo& in = c->info;
    if(sx >= in.width) sx = in.width - 1;
    if(sy >= in.height) sy = in.height - 1;
    uint16_t actual_row = in.top_down ? sy : (uint16_t)(in.height - 1U - sy);
    uint32_t off = in.data_offset +
                   ((uint32_t)actual_row + (uint32_t)plane * (uint32_t)in.height) * in.row_stride +
                   sx / 8U;
    if(off >= c->file_len) return 1U;
    uint8_t bit = (c->file[off] >> (7U - (sx % 8U))) & 1U;
    return bit ? 0U : 1U;
}

static inline uint16_t map_coord(uint16_t out, uint16_t tx, uint16_t src) {
    if(tx == 0U || src == 0U) return 0U;
    uint32_t v = (uint32_t)out * (uint32_t)src / (uint32_t)tx;
    if(v >= src) v = src - 1U;
    return (uint16_t)v;
}

/* Nearest-neighbour rescale of the BMP onto the target size. */
static uint8_t bmp_pixel_cb(size_t idx, void* ctx) {
    const BmpCtx* c = (const BmpCtx*)ctx;
    size_t count = (size_t)c->tx_w * c->tx_h;
    uint8_t plane = 0;
    if(idx >= count) {
        plane = 1;
        idx -= count;
    }
    if(plane && c->info.bpp != 2) return 1U;
    uint16_t x = (uint16_t)(idx % c->tx_w);
    uint16_t y = (uint16_t)(idx / c->tx_w);
    return bmp_bit(c, map_coord(x, c->tx_w, c->info.width), map_coord(y, c->tx_h, c->info.height), plane);
}

static void color26_src_xy(const BmpInfo& info, uint16_t px, uint16_t py, uint16_t& sx, uint16_t& sy) {
    if(info.width == TAGTINKER_COLOR26_WIRE_W && info.height == TAGTINKER_COLOR26_WIRE_H) {
        sx = px;
        sy = py;
        return;
    }
    uint16_t gx = 0, gy = 0;
    tagtinker_color26_proto_to_glass(TAGTINKER_COLOR26_WIRE_W, px, py, &gx, &gy);
    if(info.width == TAGTINKER_COLOR26_GLASS_W && info.height == TAGTINKER_COLOR26_GLASS_H) {
        sx = gx;
        sy = gy;
        return;
    }
    sx = map_coord(gx, TAGTINKER_COLOR26_GLASS_W, info.width);
    sy = map_coord(gy, TAGTINKER_COLOR26_GLASS_H, info.height);
}

static uint8_t color26_bmp_cb(size_t idx, void* ctx) {
    const BmpCtx* c = (const BmpCtx*)ctx;
    size_t count = (size_t)TAGTINKER_COLOR26_WIRE_W * (size_t)TAGTINKER_COLOR26_WIRE_H;
    uint8_t plane = 0;
    if(idx >= count) {
        plane = 1;
        idx -= count;
    }
    uint16_t px = (uint16_t)(idx % TAGTINKER_COLOR26_WIRE_W);
    uint16_t py = (uint16_t)(idx / TAGTINKER_COLOR26_WIRE_W);
    uint16_t sx = 0, sy = 0;
    color26_src_xy(c->info, px, py, sx, sy);
    if(plane && c->info.bpp != 2) return 1U;
    return bmp_bit(c, sx, sy, plane);
}

static bool tx_image_job(const TxJob& job) {
    if(!target_supports_graphics(s_target)) {
        st_msg("Tag has no known graphic size");
        return false;
    }
    File f = LittleFS.open(image_path(job.image), "r");
    if(!f) {
        st_msg("Image not found");
        return false;
    }
    size_t sz = f.size();
    if(sz < 54 || sz > TX_BMP_MAX_FILE) {
        f.close();
        st_msg("Bad BMP size");
        return false;
    }
    uint8_t* mem = (uint8_t*)big_alloc(sz);
    if(!mem) {
        f.close();
        st_msg("Out of memory");
        return false;
    }
    bool loaded = f.read(mem, sz) == sz;
    f.close();
    BmpCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.file = mem;
    ctx.file_len = sz;
    if(!loaded || !bmp_parse(mem, sz, ctx.info)) {
        free(mem);
        st_msg("Not a 1-bit / 2-plane BMP");
        return false;
    }

    uint8_t page = resolve_page(job.page);
    TagTinkerImagePayload payload;
    bool ok;
    if(tagtinker_profile_needs_wh_swap(&s_target.profile)) {
        ok = tagtinker_encode_fn_payload(
            color26_bmp_cb,
            &ctx,
            (size_t)TAGTINKER_COLOR26_WIRE_W * (size_t)TAGTINKER_COLOR26_WIRE_H * 2U,
            (TagTinkerCompressionMode)g_settings.compression,
            &payload);
        free(mem);
        if(!ok) {
            st_msg("Encode failed");
            return false;
        }
        ok = tx_send_color26_payload(s_target.plid, page, &payload);
        tagtinker_free_image_payload(&payload);
        return ok;
    }

    ctx.tx_w = s_target.profile.width;
    ctx.tx_h = s_target.profile.height;
    ctx.second_plane = target_supports_accent(s_target) || job.accent;
    size_t total = (size_t)ctx.tx_w * ctx.tx_h * (ctx.second_plane ? 2U : 1U);
    ok = tagtinker_encode_fn_payload(
        bmp_pixel_cb, &ctx, total, (TagTinkerCompressionMode)g_settings.compression, &payload);
    free(mem);
    if(!ok) {
        st_msg("Encode failed");
        return false;
    }
    ok = tx_send_full_payload(s_target.plid, &payload, page, ctx.tx_w, ctx.tx_h, 0, 0);
    tagtinker_free_image_payload(&payload);
    return ok;
}

/* ------------------------------------------------------------- simple jobs */

static bool tx_ping_flash_job() {
    uint8_t f0[TAGTINKER_MAX_FRAME_SIZE];
    uint8_t f1[TAGTINKER_MAX_FRAME_SIZE];
    size_t l0 = tagtinker_make_ping_frame(f0, s_target.plid);
    /* 0x06 command, 0x49 payload (LED flash, page 1, no forever), 0x0005 duration */
    const uint8_t blink_payload[6] = {0x06, 0x49, 0x00, 0x00, 0x00, 0x05};
    size_t l1 = tagtinker_make_addressed_frame(f1, s_target.plid, blink_payload, 6);
    st_progress(0, 2);
    if(!tx_send_frame(f0, l0, 160, 10)) return false;
    st_progress(1, 2);
    tx_wait(20);
    if(s_cancel) return false;
    bool ok = tx_send_frame(f1, l1, 80, 10);
    st_progress(2, 2);
    return ok;
}

static bool tx_broadcast_job(const TxJob& job) {
    uint8_t f[TAGTINKER_MAX_FRAME_SIZE];
    size_t l = (job.type == TX_BROADCAST_PAGE) ?
                   tagtinker_build_broadcast_page_frame(f, job.page, job.forever, job.duration) :
                   tagtinker_build_broadcast_debug_frame(f);
    st_progress(0, 1);
    bool ok = tx_send_frame(f, l, job.repeats, 10);
    st_progress(1, 1);
    return ok;
}

/* ------------------------------------------------------------- task */

static bool run_job(const TxJob& job) {
    switch(job.type) {
    case TX_PING_FLASH: return tx_ping_flash_job();
    case TX_BROADCAST_PAGE:
    case TX_BROADCAST_DEBUG: return tx_broadcast_job(job);
    case TX_TEXT: return tx_text_job(job);
    case TX_IMAGE: return tx_image_job(job);
    case TX_IR_TEST: {
        st_progress(0, 1);
        bool ok = ir_test_carrier(3000);
        st_progress(1, 1);
        return ok;
    }
    default: return false;
    }
}

static void tx_task(void*) {
    for(;;) {
        uint8_t dummy;
        if(xQueueReceive(s_queue, &dummy, portMAX_DELAY) != pdTRUE) continue;

        st_msg("Transmitting");
        bool ok = ir_init(g_settings.ir_pin, g_settings.ir_invert);
        if(!ok) {
            st_msg("IR init failed");
        } else {
            ir_clear_stop();
            vTaskDelay(pdMS_TO_TICKS(100));
            uint32_t loops = 0;
            do {
                ok = run_job(s_job);
                loops++;
                if(s_job.spam && !s_cancel && ok) {
                    char m[40];
                    snprintf(m, sizeof(m), "Repeating... (%lu)", (unsigned long)loops);
                    st_msg(m);
                    tx_wait(50);
                }
            } while(s_job.spam && !s_cancel && ok);
            ir_deinit();
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.busy = false;
        s_status.finished = true;
        s_status.cancelled = s_cancel;
        s_status.ok = ok && !s_cancel;
        if(s_cancel) strlcpy(s_status.msg, "Stopped", sizeof(s_status.msg));
        else if(ok) strlcpy(s_status.msg, "Done ;)", sizeof(s_status.msg));
        else if(!strcmp(s_status.msg, "Transmitting") || !strncmp(s_status.msg, "Repeating", 9) ||
                !strncmp(s_status.msg, "Chunk", 5))
            strlcpy(s_status.msg, "Send failed", sizeof(s_status.msg));
        s_status.seq++;
        xSemaphoreGive(s_lock);
        s_cancel = false;
    }
}

void tx_begin() {
    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.msg, "Idle", sizeof(s_status.msg));
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(1, sizeof(uint8_t));
    /* Core 1, above loop() priority; it sleeps while RMT runs a frame. */
    xTaskCreatePinnedToCore(tx_task, "tt_tx", 12288, nullptr, 5, &s_task, 1);
}

bool tx_start(const TxJob& job, String& err) {
    TxStatus st = tx_status();
    if(st.busy) {
        err = "Busy: a transmission is running";
        return false;
    }
    bool needs_target = job.type == TX_PING_FLASH || job.type == TX_TEXT || job.type == TX_IMAGE;
    if(needs_target) {
        if(job.target < 0 || job.target >= (int)g_targets.size()) {
            err = "Unknown target";
            return false;
        }
        s_target = g_targets[job.target];
        if((job.type == TX_TEXT || job.type == TX_IMAGE) && !target_supports_graphics(s_target)) {
            err = "This tag type has no known graphic size (set a custom size)";
            return false;
        }
    }
    s_job = job;
    s_cancel = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = true;
    s_status.finished = false;
    s_status.ok = false;
    s_status.cancelled = false;
    s_status.type = job.type;
    s_status.done = 0;
    s_status.total = 0;
    strlcpy(s_status.msg, "Starting", sizeof(s_status.msg));
    xSemaphoreGive(s_lock);
    uint8_t one = 1;
    xQueueSend(s_queue, &one, 0);
    return true;
}

void tx_stop() {
    if(!tx_status().busy) return;
    s_cancel = true;
    ir_request_stop();
}
