/*
 * Persistent state: saved ESL targets, image library, recent texts, settings.
 * Targets/texts live on LittleFS, settings in NVS (Preferences).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include "proto.h"

#define TT_MAX_TARGETS     16
#define TT_MAX_TEXTS       10
#define TT_NAME_LEN        16
#define TT_TEXT_LEN        63
#define TT_IMG_DIR         "/img"

struct Target {
    String name;
    String barcode;
    uint8_t plid[4];
    TagTinkerTagProfile profile;
    uint16_t custom_w = 0;
    uint16_t custom_h = 0;
    uint8_t custom_color = 0; /* TagTinkerTagColor */
};

struct Settings {
    uint8_t data_repeats = 2;   /* repeats per image data frame (1..10) */
    uint8_t compression = 0;    /* TagTinkerCompressionMode */
    int ir_pin = 19;            /* internal IR LED of the StickC Plus2 */
    bool ir_invert = false;
    uint8_t brightness = 120;
    bool sound = true;
    String ap_ssid;             /* empty = auto "TagTinker-XXXX" */
    String ap_pass = "tagtinker";
    String sta_ssid;
    String sta_pass;
    /* Text payload options */
    bool txt_invert = false;
    bool txt_accent = false;
    uint8_t txt_padding = 5;
    uint8_t page = 1;           /* UI page 1..8 for text / image */
    /* Broadcast options */
    uint8_t bc_page = 0;
    uint16_t bc_duration = 15;
    bool bc_forever = false;
    uint16_t bc_repeats = 200;
};

struct ImageInfo {
    String name;
    uint32_t size = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    uint16_t bpp = 0;
};

extern std::vector<Target> g_targets;
extern std::vector<String> g_texts;
extern Settings g_settings;

bool store_begin();

/* Targets */
bool barcode_ok(const String& bc);
void target_refresh_profile(Target& t);
int target_add(const String& barcode, const String& name, String& err);
bool target_delete(int idx);
bool target_rename(int idx, const String& name);
bool target_set_custom(int idx, uint16_t w, uint16_t h, uint8_t color);
bool target_supports_graphics(const Target& t);
bool target_supports_accent(const Target& t);
void target_glass_size(const Target& t, uint16_t& w, uint16_t& h);
bool targets_save();

/* Recent texts */
void texts_add(const String& text);
bool texts_delete(int idx);

/* Settings */
void settings_save();
String ap_ssid_effective();

/* Images */
std::vector<ImageInfo> images_list();
bool image_info(const String& name, ImageInfo& info);
bool image_delete(const String& name);
String image_path(const String& name);
String image_sanitize_name(const String& name);

/* Helpers */
const char* kind_label(TagTinkerTagKind k);
const char* color_label(TagTinkerTagColor c);
String json_escape(const String& s);
