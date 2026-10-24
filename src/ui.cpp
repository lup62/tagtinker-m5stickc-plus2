/*
 * On-device UI.
 *
 * Buttons (M5StickC Plus2):
 *   BtnA  (front "M5")  -> select / confirm
 *   BtnB  (top side)    -> next item
 *   BtnPWR(top right)   -> back  (hold ~3 s powers the unit off, handled by PMIC)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "ui.h"

#include <M5Unified.h>
#include <vector>

#include "store.h"
#include "tx.h"
#include "web.h"

enum Screen {
    SC_HOME,
    SC_MENU,
    SC_PICK_TARGET,
    SC_PICK_TEXT,
    SC_PICK_IMAGE,
    SC_CONFIRM,
    SC_TX,
    SC_INFO,
};

static Screen s_screen = SC_HOME;
static int s_sel = 0;        /* highlighted list row */
static int s_top = 0;        /* first visible row (scrolling) */
static bool s_dirty = true;

/* pending job being assembled through the pick screens */
static TxJobType s_pending = TX_NONE;
static int s_pick_target = -1;
static String s_pick_text;
static String s_pick_image;
static uint32_t s_last_seq = 0;

/* colours */
#define COL_BG 0x0000
#define COL_HEAD 0x6B7F
#define COL_SEL 0x3B2F
#define COL_SELTX 0xFFFF
#define COL_TX 0xCE59
#define COL_DIM 0x7BEF
#define COL_OK 0x5E6C
#define COL_ERR 0xF9A6

static const char* kMenu[] = {
    "Send Text", "Send Image", "Ping / Flash", "Broadcast Page",
    "Debug Screen", "IR LED Test", "Wi-Fi Info",
};
static const int kMenuN = sizeof(kMenu) / sizeof(kMenu[0]);

/* --------------------------------------------------------------- drawing */

static void header(const char* title) {
    auto& d = M5.Display;
    d.fillRect(0, 0, d.width(), 22, 0x18E3);
    d.setTextColor(COL_HEAD, 0x18E3);
    d.setTextDatum(middle_left);
    d.setFont(&fonts::Font2);
    d.drawString(title, 6, 11);
}

static void footer(const char* a, const char* b, const char* pwr) {
    auto& d = M5.Display;
    int y = d.height() - 16;
    d.fillRect(0, y, d.width(), 16, 0x10A2);
    d.setFont(&fonts::Font0);
    d.setTextColor(COL_DIM, 0x10A2);
    d.setTextDatum(middle_left);
    char line[48];
    snprintf(line, sizeof(line), "A:%s  B:%s  P:%s", a, b, pwr);
    d.drawString(line, 4, y + 8);
}

/* Generic scrolling list. Returns nothing; uses s_sel / s_top. */
static void draw_list(const char* title, const std::vector<String>& items, const char* empty) {
    auto& d = M5.Display;
    d.fillScreen(COL_BG);
    header(title);
    d.setFont(&fonts::Font2);
    d.setTextDatum(middle_left);

    const int row_h = 26;
    const int top_y = 26;
    const int avail = d.height() - top_y - 18;
    const int per_page = avail / row_h;

    if(items.empty()) {
        d.setTextColor(COL_DIM, COL_BG);
        d.drawString(empty, 8, top_y + 16);
        return;
    }
    if(s_sel < 0) s_sel = items.size() - 1;
    if(s_sel >= (int)items.size()) s_sel = 0;
    if(s_sel < s_top) s_top = s_sel;
    if(s_sel >= s_top + per_page) s_top = s_sel - per_page + 1;

    for(int i = 0; i < per_page && (s_top + i) < (int)items.size(); i++) {
        int idx = s_top + i;
        int y = top_y + i * row_h;
        bool sel = idx == s_sel;
        if(sel) d.fillRoundRect(3, y + 2, d.width() - 6, row_h - 3, 4, COL_SEL);
        d.setTextColor(sel ? COL_SELTX : COL_TX, sel ? COL_SEL : COL_BG);
        String t = items[idx];
        if(t.length() > 18) t = t.substring(0, 17) + "~";
        d.drawString(t, 10, y + row_h / 2);
    }
    /* scroll hint */
    if((int)items.size() > per_page) {
        d.setFont(&fonts::Font0);
        d.setTextColor(COL_DIM, COL_BG);
        d.setTextDatum(top_right);
        char c[16];
        snprintf(c, sizeof(c), "%d/%d", s_sel + 1, (int)items.size());
        d.drawString(c, d.width() - 4, 26);
        d.setTextDatum(middle_left);
    }
}

static std::vector<String> target_labels(bool graphics_only) {
    std::vector<String> v;
    for(auto& t : g_targets) {
        if(graphics_only && !target_supports_graphics(t)) continue;
        String s = t.name;
        if(t.profile.known) s += " " + String(t.profile.width) + "x" + String(t.profile.height);
        v.push_back(s);
    }
    return v;
}
static std::vector<int> target_index_map(bool graphics_only) {
    std::vector<int> v;
    for(size_t i = 0; i < g_targets.size(); i++)
        if(!graphics_only || target_supports_graphics(g_targets[i])) v.push_back(i);
    return v;
}

static void draw_home() {
    auto& d = M5.Display;
    d.fillScreen(COL_BG);
    header("TagTinker");
    d.setTextDatum(top_left);

    d.setFont(&fonts::Font2);
    d.setTextColor(COL_TX, COL_BG);
    d.drawString("ESL IR research", 8, 30);

    d.setFont(&fonts::Font0);
    d.setTextColor(COL_DIM, COL_BG);
    d.drawString("Wi-Fi network:", 8, 58);
    d.setFont(&fonts::Font2);
    d.setTextColor(COL_OK, COL_BG);
    d.drawString(web_ap_ssid(), 8, 70);

    d.setFont(&fonts::Font0);
    d.setTextColor(COL_DIM, COL_BG);
    d.drawString("Password:", 8, 96);
    d.setTextColor(COL_TX, COL_BG);
    d.drawString(g_settings.ap_pass, 8, 108);

    d.setTextColor(COL_DIM, COL_BG);
    d.drawString("Then open:", 8, 132);
    d.setFont(&fonts::Font2);
    d.setTextColor(COL_HEAD, COL_BG);
    d.drawString("http://" + web_ap_ip(), 8, 144);

    d.setFont(&fonts::Font0);
    d.setTextColor(COL_DIM, COL_BG);
    if(web_sta_connected())
        d.drawString("LAN: http://" + web_sta_ip(), 8, 170);
    d.drawString(String(g_targets.size()) + " targets saved", 8, 186);

    footer("Menu", "Menu", "-");
}

static void draw_info() {
    auto& d = M5.Display;
    d.fillScreen(COL_BG);
    header("Wi-Fi Info");
    d.setFont(&fonts::Font2);
    d.setTextDatum(top_left);
    int y = 30;
    auto line = [&](const char* k, const String& v, uint16_t c) {
        d.setFont(&fonts::Font0);
        d.setTextColor(COL_DIM, COL_BG);
        d.drawString(k, 8, y);
        y += 12;
        d.setFont(&fonts::Font2);
        d.setTextColor(c, COL_BG);
        d.drawString(v, 8, y);
        y += 24;
    };
    line("AP SSID", web_ap_ssid(), COL_OK);
    line("AP password", g_settings.ap_pass, COL_TX);
    line("Panel URL", "http://" + web_ap_ip(), COL_HEAD);
    if(web_sta_connected()) line("On your LAN", "http://" + web_sta_ip(), COL_HEAD);
    line("Free heap", String(ESP.getFreeHeap() / 1024) + " KB", COL_DIM);
    footer("Back", "Back", "Back");
}

static void draw_tx() {
    auto& d = M5.Display;
    TxStatus st = tx_status();
    d.fillScreen(COL_BG);
    header("Transmitting");
    d.setTextDatum(middle_center);
    d.setFont(&fonts::Font2);
    d.setTextColor(COL_TX, COL_BG);
    d.drawString(tx_job_label(st.type), d.width() / 2, 44);

    /* progress bar */
    int bw = d.width() - 24, bx = 12, by = 74;
    d.drawRoundRect(bx, by, bw, 12, 3, COL_DIM);
    int pct = st.total ? (int)(100 * st.done / st.total) : (st.busy ? 5 : 100);
    d.fillRoundRect(bx + 1, by + 1, (bw - 2) * pct / 100, 10, 3, COL_HEAD);

    d.setFont(&fonts::Font0);
    uint16_t mc = st.busy ? COL_DIM : (st.ok ? COL_OK : (st.cancelled ? COL_DIM : COL_ERR));
    d.setTextColor(mc, COL_BG);
    d.drawString(st.msg, d.width() / 2, 100);

    d.setFont(&fonts::Font2);
    d.setTextColor(COL_DIM, COL_BG);
    if(st.busy) d.drawString("Point at the tag", d.width() / 2, 128);
    else d.drawString(st.ok ? "Flipped ;)" : (st.cancelled ? "Stopped" : "Failed"), d.width() / 2, 128);

    footer(st.busy ? "-" : "OK", "-", st.busy ? "Stop" : "Back");
}

static void redraw() {
    switch(s_screen) {
    case SC_HOME: draw_home(); break;
    case SC_INFO: draw_info(); break;
    case SC_TX: draw_tx(); break;
    case SC_MENU: {
        std::vector<String> v;
        for(int i = 0; i < kMenuN; i++) v.push_back(kMenu[i]);
        draw_list("Menu", v, "");
        footer("Select", "Next", "Back");
        break;
    }
    case SC_PICK_TARGET: {
        bool g = s_pending == TX_TEXT || s_pending == TX_IMAGE;
        draw_list("Pick target", target_labels(g),
                  g ? "No graphic tags.\nAdd one via web." : "No targets.\nAdd one via web.");
        footer("Select", "Next", "Back");
        break;
    }
    case SC_PICK_TEXT: draw_list("Pick text", g_texts, "No saved texts."); footer("Send", "Next", "Back"); break;
    case SC_PICK_IMAGE: {
        std::vector<String> v;
        for(auto& im : images_list()) v.push_back(im.name + " " + String(im.w) + "x" + String(im.h));
        draw_list("Pick image", v, "No images.\nUpload via web.");
        footer("Send", "Next", "Back");
        break;
    }
    default: break;
    }
    s_dirty = false;
}

/* --------------------------------------------------------------- actions */

static void go(Screen s) {
    s_screen = s;
    s_sel = 0;
    s_top = 0;
    s_dirty = true;
}

static void start_job() {
    TxJob job;
    job.type = s_pending;
    job.target = s_pick_target;
    job.page = g_settings.page;
    if(s_pending == TX_TEXT) {
        job.text = s_pick_text;
        job.invert = g_settings.txt_invert;
        job.accent = g_settings.txt_accent;
        job.padding = g_settings.txt_padding;
    } else if(s_pending == TX_IMAGE) {
        job.image = s_pick_image;
        job.accent = g_settings.txt_accent;
    } else if(s_pending == TX_BROADCAST_PAGE) {
        job.page = g_settings.bc_page;
        job.forever = g_settings.bc_forever;
        job.duration = g_settings.bc_duration;
        job.repeats = g_settings.bc_repeats;
    } else if(s_pending == TX_BROADCAST_DEBUG) {
        job.repeats = 50;
    }
    String err;
    if(tx_start(job, err)) {
        if(g_settings.sound) M5.Speaker.tone(1760, 40);
        go(SC_TX);
    } else {
        /* brief error flash on current screen */
        auto& d = M5.Display;
        d.fillRect(0, d.height() / 2 - 12, d.width(), 24, COL_ERR);
        d.setTextColor(0, COL_ERR);
        d.setTextDatum(middle_center);
        d.setFont(&fonts::Font0);
        d.drawString(err.length() > 28 ? err.substring(0, 28) : err, d.width() / 2, d.height() / 2);
        delay(1400);
        s_dirty = true;
    }
}

static void select_current() {
    switch(s_screen) {
    case SC_HOME: go(SC_MENU); break;
    case SC_INFO: go(SC_HOME); break;
    case SC_MENU:
        switch(s_sel) {
        case 0: s_pending = TX_TEXT; go(SC_PICK_TARGET); break;
        case 1: s_pending = TX_IMAGE; go(SC_PICK_TARGET); break;
        case 2: s_pending = TX_PING_FLASH; go(SC_PICK_TARGET); break;
        case 3: s_pending = TX_BROADCAST_PAGE; start_job(); break;
        case 4: s_pending = TX_BROADCAST_DEBUG; start_job(); break;
        case 5: s_pending = TX_IR_TEST; start_job(); break;
        case 6: go(SC_INFO); break;
        }
        break;
    case SC_PICK_TARGET: {
        bool g = s_pending == TX_TEXT || s_pending == TX_IMAGE;
        auto map = target_index_map(g);
        if(map.empty()) break;
        if(s_sel >= (int)map.size()) s_sel = 0;
        s_pick_target = map[s_sel];
        if(s_pending == TX_TEXT) go(SC_PICK_TEXT);
        else if(s_pending == TX_IMAGE) go(SC_PICK_IMAGE);
        else start_job(); /* ping */
        break;
    }
    case SC_PICK_TEXT:
        if(g_texts.empty()) break;
        s_pick_text = g_texts[constrain(s_sel, 0, (int)g_texts.size() - 1)];
        start_job();
        break;
    case SC_PICK_IMAGE: {
        auto imgs = images_list();
        if(imgs.empty()) break;
        s_pick_image = imgs[constrain(s_sel, 0, (int)imgs.size() - 1)].name;
        start_job();
        break;
    }
    case SC_TX: {
        TxStatus st = tx_status();
        if(!st.busy) go(SC_HOME);
        break;
    }
    default: break;
    }
}

static void back() {
    switch(s_screen) {
    case SC_MENU: go(SC_HOME); break;
    case SC_INFO: go(SC_MENU); break;
    case SC_PICK_TARGET: go(SC_MENU); break;
    case SC_PICK_TEXT:
    case SC_PICK_IMAGE: go(SC_PICK_TARGET); break;
    case SC_TX: {
        TxStatus st = tx_status();
        if(st.busy) tx_stop();
        else go(SC_HOME);
        break;
    }
    default: break;
    }
}

static void next_item() {
    switch(s_screen) {
    case SC_HOME: go(SC_MENU); break;
    case SC_INFO: go(SC_MENU); break;
    case SC_MENU:
    case SC_PICK_TARGET:
    case SC_PICK_TEXT:
    case SC_PICK_IMAGE:
        s_sel++;
        s_dirty = true;
        break;
    default: break;
    }
}

/* --------------------------------------------------------------- loop */

void ui_begin() {
    M5.Display.setRotation(0);     /* portrait, USB at the bottom */
    M5.Display.setBrightness(g_settings.brightness);
    M5.Display.fillScreen(COL_BG);
    s_screen = SC_HOME;
    s_dirty = true;
}

void ui_loop() {
    M5.update();

    if(M5.BtnA.wasClicked()) select_current();
    if(M5.BtnB.wasClicked()) next_item();
    if(M5.BtnPWR.wasClicked()) back();

    TxStatus st = tx_status();
    if(s_screen == SC_TX) {
        static uint32_t last = 0;
        if(millis() - last > 150) {  /* animate progress */
            last = millis();
            s_dirty = true;
        }
        if(!st.busy && st.seq != s_last_seq) {
            s_last_seq = st.seq;
            if(g_settings.sound) M5.Speaker.tone(st.ok ? 2093 : 440, 90);
        }
    } else {
        s_last_seq = st.seq;
    }

    if(s_dirty) redraw();
}
