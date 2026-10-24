/*
 * Web control panel served from the StickC Plus2 access point.
 *
 *   /        control panel (targets, text, images, broadcast, settings)
 *   /prep    TagTinker Image Prep tool with a "Send to M5" panel
 *   /api/*   JSON API used by both pages
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "web.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "store.h"
#include "tx.h"
#include "web_assets.h"

static WebServer server(80);
static DNSServer dns;
static File s_upload;
static String s_upload_name;
static String s_upload_err;
static bool s_reboot_pending = false;
static uint32_t s_reboot_at = 0;

String web_ap_ip() {
    return WiFi.softAPIP().toString();
}
bool web_sta_connected() {
    return WiFi.status() == WL_CONNECTED;
}
String web_sta_ip() {
    return web_sta_connected() ? WiFi.localIP().toString() : String();
}
String web_ap_ssid() {
    return ap_ssid_effective();
}

/* ---------------------------------------------------------------- helpers */

static void send_json(int code, const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", body);
}

static void ok_json() {
    send_json(200, "{\"ok\":true}");
}

static void err_json(const String& msg) {
    send_json(400, String("{\"ok\":false,\"error\":\"") + json_escape(msg) + "\"}");
}

static String profile_json(const TagTinkerTagProfile& p) {
    uint16_t gw = p.width, gh = p.height;
    tagtinker_profile_glass_size(&p, &gw, &gh);
    String s = "{";
    s += "\"type\":" + String(p.type_code);
    s += ",\"known\":" + String(p.known ? "true" : "false");
    s += ",\"w\":" + String(gw);
    s += ",\"h\":" + String(gh);
    s += ",\"kind\":\"" + String(kind_label(p.kind)) + "\"";
    s += ",\"color\":\"" + String(color_label(p.color)) + "\"";
    s += ",\"model\":\"" + json_escape(p.model_name ? p.model_name : "") + "\"";
    s += "}";
    return s;
}

/* ---------------------------------------------------------------- pages */

static void handle_root() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PANEL_HTML);
}

static void handle_prep() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PREP_HTML, PREP_HTML_LEN);
}

/* ---------------------------------------------------------------- state API */

static void handle_state() {
    String s = "{";
    s += "\"version\":\"" TT_VERSION "\"";
    s += ",\"ap\":\"" + json_escape(web_ap_ssid()) + "\"";
    s += ",\"ap_ip\":\"" + web_ap_ip() + "\"";
    s += ",\"sta\":" + String(web_sta_connected() ? "true" : "false");
    s += ",\"sta_ip\":\"" + web_sta_ip() + "\"";
    s += ",\"sta_ssid\":\"" + json_escape(g_settings.sta_ssid) + "\"";
    s += ",\"heap\":" + String((unsigned)ESP.getFreeHeap());

    TxStatus st = tx_status();
    s += ",\"tx\":{";
    s += "\"busy\":" + String(st.busy ? "true" : "false");
    s += ",\"finished\":" + String(st.finished ? "true" : "false");
    s += ",\"ok\":" + String(st.ok ? "true" : "false");
    s += ",\"cancelled\":" + String(st.cancelled ? "true" : "false");
    s += ",\"type\":\"" + String(tx_job_label(st.type)) + "\"";
    s += ",\"done\":" + String((unsigned)st.done);
    s += ",\"total\":" + String((unsigned)st.total);
    s += ",\"seq\":" + String((unsigned)st.seq);
    s += ",\"msg\":\"" + json_escape(st.msg) + "\"}";

    s += ",\"targets\":[";
    for(size_t i = 0; i < g_targets.size(); i++) {
        Target& t = g_targets[i];
        if(i) s += ",";
        s += "{\"i\":" + String((unsigned)i);
        s += ",\"name\":\"" + json_escape(t.name) + "\"";
        s += ",\"barcode\":\"" + json_escape(t.barcode) + "\"";
        s += ",\"graphics\":" + String(target_supports_graphics(t) ? "true" : "false");
        s += ",\"profile\":" + profile_json(t.profile);
        s += "}";
    }
    s += "]";

    s += ",\"texts\":[";
    for(size_t i = 0; i < g_texts.size(); i++) {
        if(i) s += ",";
        s += "\"" + json_escape(g_texts[i]) + "\"";
    }
    s += "]";

    s += ",\"images\":[";
    auto imgs = images_list();
    for(size_t i = 0; i < imgs.size(); i++) {
        if(i) s += ",";
        s += "{\"name\":\"" + json_escape(imgs[i].name) + "\"";
        s += ",\"size\":" + String((unsigned)imgs[i].size);
        s += ",\"w\":" + String(imgs[i].w);
        s += ",\"h\":" + String(imgs[i].h);
        s += ",\"bpp\":" + String(imgs[i].bpp) + "}";
    }
    s += "]";

    s += ",\"settings\":{";
    s += "\"data_repeats\":" + String(g_settings.data_repeats);
    s += ",\"compression\":" + String(g_settings.compression);
    s += ",\"ir_pin\":" + String(g_settings.ir_pin);
    s += ",\"ir_invert\":" + String(g_settings.ir_invert ? "true" : "false");
    s += ",\"brightness\":" + String(g_settings.brightness);
    s += ",\"sound\":" + String(g_settings.sound ? "true" : "false");
    s += ",\"txt_invert\":" + String(g_settings.txt_invert ? "true" : "false");
    s += ",\"txt_accent\":" + String(g_settings.txt_accent ? "true" : "false");
    s += ",\"txt_padding\":" + String(g_settings.txt_padding);
    s += ",\"page\":" + String(g_settings.page);
    s += ",\"bc_page\":" + String(g_settings.bc_page);
    s += ",\"bc_duration\":" + String(g_settings.bc_duration);
    s += ",\"bc_forever\":" + String(g_settings.bc_forever ? "true" : "false");
    s += ",\"bc_repeats\":" + String(g_settings.bc_repeats);
    s += ",\"ap_ssid\":\"" + json_escape(g_settings.ap_ssid) + "\"";
    s += "}";

    s += "}";
    send_json(200, s);
}

/* ---------------------------------------------------------------- targets API */

static void handle_target_add() {
    String err;
    int idx = target_add(server.arg("barcode"), server.arg("name"), err);
    if(idx < 0) {
        err_json(err);
        return;
    }
    send_json(200, "{\"ok\":true,\"index\":" + String(idx) + "}");
}

static void handle_target_delete() {
    if(!target_delete(server.arg("i").toInt())) {
        err_json("Delete failed");
        return;
    }
    ok_json();
}

static void handle_target_rename() {
    if(!target_rename(server.arg("i").toInt(), server.arg("name"))) {
        err_json("Rename failed");
        return;
    }
    ok_json();
}

static void handle_target_custom() {
    bool ok = target_set_custom(
        server.arg("i").toInt(),
        (uint16_t)server.arg("w").toInt(),
        (uint16_t)server.arg("h").toInt(),
        (uint8_t)server.arg("color").toInt());
    if(!ok) {
        err_json("Custom size rejected (8..800, multiple of 8)");
        return;
    }
    ok_json();
}

/* ---------------------------------------------------------------- texts API */

static void handle_text_add() {
    texts_add(server.arg("text"));
    ok_json();
}

static void handle_text_delete() {
    texts_delete(server.arg("i").toInt());
    ok_json();
}

/* ---------------------------------------------------------------- settings API */

static void handle_settings_save() {
    Settings& s = g_settings;
    if(server.hasArg("data_repeats")) s.data_repeats = constrain(server.arg("data_repeats").toInt(), 1, 10);
    if(server.hasArg("compression")) s.compression = constrain(server.arg("compression").toInt(), 0, 2);
    if(server.hasArg("ir_pin")) s.ir_pin = server.arg("ir_pin").toInt();
    if(server.hasArg("ir_invert")) s.ir_invert = server.arg("ir_invert") == "1";
    if(server.hasArg("brightness")) s.brightness = constrain(server.arg("brightness").toInt(), 10, 255);
    if(server.hasArg("sound")) s.sound = server.arg("sound") == "1";
    if(server.hasArg("txt_invert")) s.txt_invert = server.arg("txt_invert") == "1";
    if(server.hasArg("txt_accent")) s.txt_accent = server.arg("txt_accent") == "1";
    if(server.hasArg("txt_padding")) s.txt_padding = constrain(server.arg("txt_padding").toInt(), 0, 45);
    if(server.hasArg("page")) s.page = constrain(server.arg("page").toInt(), 1, 8);
    if(server.hasArg("bc_page")) s.bc_page = constrain(server.arg("bc_page").toInt(), 0, 7);
    if(server.hasArg("bc_duration")) s.bc_duration = constrain(server.arg("bc_duration").toInt(), 2, 1200);
    if(server.hasArg("bc_forever")) s.bc_forever = server.arg("bc_forever") == "1";
    if(server.hasArg("bc_repeats")) s.bc_repeats = constrain(server.arg("bc_repeats").toInt(), 1, 2000);
    if(server.hasArg("ap_ssid")) s.ap_ssid = server.arg("ap_ssid");
    settings_save();
    ok_json();
}

static void handle_wifi_sta() {
    g_settings.sta_ssid = server.arg("ssid");
    g_settings.sta_pass = server.arg("pass");
    settings_save();
    if(g_settings.sta_ssid.length()) {
        WiFi.begin(g_settings.sta_ssid.c_str(), g_settings.sta_pass.c_str());
    } else {
        WiFi.disconnect();
    }
    ok_json();
}

/* ---------------------------------------------------------------- transmit API */

static void fill_common(TxJob& job) {
    if(server.hasArg("page")) job.page = constrain(server.arg("page").toInt(), 1, 8);
    else job.page = g_settings.page;
    job.spam = server.arg("spam") == "1";
}

static void handle_tx() {
    String what = server.arg("what");
    TxJob job;
    if(what == "text") {
        job.type = TX_TEXT;
        job.target = server.arg("target").toInt();
        job.text = server.arg("text");
        job.invert = server.hasArg("invert") ? server.arg("invert") == "1" : g_settings.txt_invert;
        job.accent = server.hasArg("accent") ? server.arg("accent") == "1" : g_settings.txt_accent;
        job.padding = server.hasArg("padding") ? server.arg("padding").toInt() : g_settings.txt_padding;
        fill_common(job);
        if(job.text.length()) texts_add(job.text);
    } else if(what == "image") {
        job.type = TX_IMAGE;
        job.target = server.arg("target").toInt();
        job.image = server.arg("image");
        job.accent = server.hasArg("accent") ? server.arg("accent") == "1" : g_settings.txt_accent;
        fill_common(job);
    } else if(what == "ping") {
        job.type = TX_PING_FLASH;
        job.target = server.arg("target").toInt();
    } else if(what == "broadcast") {
        job.type = TX_BROADCAST_PAGE;
        job.page = constrain(server.arg("page").toInt(), 0, 7);
        job.forever = server.arg("forever") == "1";
        job.duration = constrain(server.arg("duration").toInt(), 2, 1200);
        job.repeats = constrain(server.arg("repeats").toInt(), 1, 2000);
    } else if(what == "debug") {
        job.type = TX_BROADCAST_DEBUG;
        job.repeats = constrain(server.arg("repeats").toInt(), 1, 2000);
    } else if(what == "irtest") {
        job.type = TX_IR_TEST;
    } else {
        err_json("Unknown transmit type");
        return;
    }
    String err;
    if(!tx_start(job, err)) {
        err_json(err);
        return;
    }
    ok_json();
}

static void handle_tx_stop() {
    tx_stop();
    ok_json();
}

/* ---------------------------------------------------------------- images API */

static void handle_image_delete() {
    if(!image_delete(server.arg("name"))) {
        err_json("Delete failed");
        return;
    }
    ok_json();
}

static void handle_upload_data() {
    HTTPUpload& up = server.upload();
    if(up.status == UPLOAD_FILE_START) {
        s_upload_err = "";
        s_upload_name = image_sanitize_name(up.filename);
        LittleFS.mkdir(TT_IMG_DIR);
        s_upload = LittleFS.open(image_path(s_upload_name), "w");
        if(!s_upload) s_upload_err = "Cannot create file (storage full?)";
    } else if(up.status == UPLOAD_FILE_WRITE) {
        if(s_upload) {
            if(s_upload.write(up.buf, up.currentSize) != up.currentSize)
                s_upload_err = "Write failed (storage full?)";
        }
    } else if(up.status == UPLOAD_FILE_END) {
        if(s_upload) s_upload.close();
        if(s_upload_err.length()) LittleFS.remove(image_path(s_upload_name));
    } else if(up.status == UPLOAD_FILE_ABORTED) {
        if(s_upload) s_upload.close();
        LittleFS.remove(image_path(s_upload_name));
        s_upload_err = "Upload aborted";
    }
}

static void handle_upload_done() {
    if(s_upload_err.length()) {
        err_json(s_upload_err);
        return;
    }
    ImageInfo info;
    if(!image_info(s_upload_name, info)) {
        image_delete(s_upload_name);
        err_json("Not a valid 1-bit / 2-plane BMP");
        return;
    }
    send_json(
        200,
        "{\"ok\":true,\"name\":\"" + json_escape(info.name) + "\",\"w\":" + String(info.w) +
            ",\"h\":" + String(info.h) + "}");
}

static void handle_reboot() {
    ok_json();
    s_reboot_pending = true;
    s_reboot_at = millis() + 400;
}

static void handle_not_found() {
    /* Captive-portal style: bounce unknown hosts to the panel. */
    server.sendHeader("Location", String("http://") + web_ap_ip() + "/", true);
    server.send(302, "text/plain", "");
}

/* ---------------------------------------------------------------- setup */

void web_begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(web_ap_ssid().c_str(), g_settings.ap_pass.c_str());
    delay(100);
    if(g_settings.sta_ssid.length()) {
        WiFi.begin(g_settings.sta_ssid.c_str(), g_settings.sta_pass.c_str());
    }
    dns.start(53, "*", WiFi.softAPIP());
    MDNS.begin("tagtinker");

    server.on("/", handle_root);
    server.on("/prep", handle_prep);
    server.on("/api/state", handle_state);
    server.on("/api/target/add", HTTP_POST, handle_target_add);
    server.on("/api/target/delete", HTTP_POST, handle_target_delete);
    server.on("/api/target/rename", HTTP_POST, handle_target_rename);
    server.on("/api/target/custom", HTTP_POST, handle_target_custom);
    server.on("/api/text/add", HTTP_POST, handle_text_add);
    server.on("/api/text/delete", HTTP_POST, handle_text_delete);
    server.on("/api/settings", HTTP_POST, handle_settings_save);
    server.on("/api/wifi", HTTP_POST, handle_wifi_sta);
    server.on("/api/tx", HTTP_POST, handle_tx);
    server.on("/api/tx/stop", HTTP_POST, handle_tx_stop);
    server.on("/api/image/delete", HTTP_POST, handle_image_delete);
    server.on("/api/image/upload", HTTP_POST, handle_upload_done, handle_upload_data);
    server.on("/api/reboot", HTTP_POST, handle_reboot);
    server.onNotFound(handle_not_found);

    server.begin();
}

void web_loop() {
    dns.processNextRequest();
    server.handleClient();
    if(s_reboot_pending && (int32_t)(millis() - s_reboot_at) >= 0) {
        ESP.restart();
    }
}
