/*
 * TagTinker for M5StickC Plus2
 * -----------------------------
 * Infrared ESL (electronic shelf label) research toolkit, ported from the
 * Flipper Zero app i12bp8/TagTinker to the M5StickC Plus2 (ESP32-PICO).
 *
 * The on-device UI drives the common actions; a Wi-Fi access point serves a
 * full control panel and the original Image Prep tool at http://192.168.4.1/.
 *
 * STRICTLY for educational research and digital art on hardware you own.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <M5Unified.h>

#include "store.h"
#include "tx.h"
#include "ui.h"
#include "web.h"

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    M5.begin(cfg);
    M5.Display.setRotation(0);
    M5.Display.fillScreen(0);
    M5.Display.setTextColor(0xFFFF, 0);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setCursor(6, 10);
    M5.Display.println("TagTinker");
    M5.Display.println("starting...");

    store_begin();          /* LittleFS + saved state + settings */
    tx_begin();             /* transmit task on core 1 */
    web_begin();            /* Wi-Fi AP + control panel */
    ui_begin();             /* on-device menus */
}

void loop() {
    web_loop();
    ui_loop();
    delay(5);
}
