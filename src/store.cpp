/*
 * Persistent state for TagTinker M5.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "store.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

std::vector<Target> g_targets;
std::vector<String> g_texts;
Settings g_settings;

static const char* TARGETS_FILE = "/targets.txt";
static const char* TEXTS_FILE = "/texts.txt";

/* ---------------------------------------------------------------- helpers */

const char* kind_label(TagTinkerTagKind k) {
    switch(k) {
    case TagTinkerTagKindDotMatrix: return "Graphic";
    case TagTinkerTagKindSegment: return "Segment";
    default: return "Unknown";
    }
}

const char* color_label(TagTinkerTagColor c) {
    switch(c) {
    case TagTinkerTagColorMono: return "Mono";
    case TagTinkerTagColorRed: return "Red";
    case TagTinkerTagColorYellow: return "Yellow";
    default: return "Unknown";
    }
}

String json_escape(const String& s) {
    String o;
    o.reserve(s.length() + 4);
    for(size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if(c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if((uint8_t)c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", (unsigned)c);
            o += b;
        } else {
            o += c;
        }
    }
    return o;
}

/* Field separator for the line based files; stripped from user strings. */
static String clean_field(const String& s, size_t max_len) {
    String o;
    for(size_t i = 0; i < s.length() && o.length() < max_len; i++) {
        char c = s[i];
        if(c == '|' || c == '\n' || c == '\r') continue;
        if((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) continue; /* 5x7 font is ASCII */
        o += c;
    }
    o.trim();
    return o;
}

/* ---------------------------------------------------------------- targets */

bool barcode_ok(const String& bc) {
    if(bc.length() != 17) return false;
    /* Characters 3..17 carry the PLID and type code and must be digits. */
    for(int i = 2; i < 17; i++) {
        if(bc[i] < '0' || bc[i] > '9') return false;
    }
    return true;
}

void target_refresh_profile(Target& t) {
    memset(&t.profile, 0, sizeof(t.profile));
    tagtinker_barcode_to_profile(t.barcode.c_str(), &t.profile);
    if(!t.profile.known && t.custom_w > 0 && t.custom_h > 0) {
        t.profile.width = t.custom_w;
        t.profile.height = t.custom_h;
        t.profile.color = (TagTinkerTagColor)t.custom_color;
        t.profile.kind = TagTinkerTagKindDotMatrix;
        t.profile.model_name = "Custom";
        t.profile.known = true;
    }
    tagtinker_barcode_to_plid(t.barcode.c_str(), t.plid);
}

bool target_supports_graphics(const Target& t) {
    return t.profile.known && t.profile.kind == TagTinkerTagKindDotMatrix && t.profile.width &&
           t.profile.height;
}

bool target_supports_accent(const Target& t) {
    return t.profile.color == TagTinkerTagColorRed || t.profile.color == TagTinkerTagColorYellow;
}

void target_glass_size(const Target& t, uint16_t& w, uint16_t& h) {
    tagtinker_profile_glass_size(&t.profile, &w, &h);
}

bool targets_save() {
    File f = LittleFS.open(TARGETS_FILE, "w");
    if(!f) return false;
    for(auto& t : g_targets) {
        f.printf(
            "%s|%s|%u|%u|%u\n",
            t.barcode.c_str(),
            t.name.c_str(),
            t.custom_w,
            t.custom_h,
            t.custom_color);
    }
    f.close();
    return true;
}

static void targets_load() {
    g_targets.clear();
    File f = LittleFS.open(TARGETS_FILE, "r");
    if(!f) return;
    while(f.available() && g_targets.size() < TT_MAX_TARGETS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if(!line.length()) continue;
        String parts[5];
        int n = 0, start = 0;
        for(int i = 0; i <= (int)line.length() && n < 5; i++) {
            if(i == (int)line.length() || line[i] == '|') {
                parts[n++] = line.substring(start, i);
                start = i + 1;
            }
        }
        if(n < 2 || !barcode_ok(parts[0])) continue;
        Target t;
        t.barcode = parts[0];
        t.name = parts[1];
        if(n >= 5) {
            t.custom_w = (uint16_t)parts[2].toInt();
            t.custom_h = (uint16_t)parts[3].toInt();
            t.custom_color = (uint8_t)parts[4].toInt();
        }
        target_refresh_profile(t);
        g_targets.push_back(t);
    }
    f.close();
}

int target_add(const String& barcode_in, const String& name_in, String& err) {
    String bc = barcode_in;
    bc.trim();
    bc.toUpperCase();
    if(!barcode_ok(bc)) {
        err = "Barcode must be 17 characters (digits from the 3rd on)";
        return -1;
    }
    for(size_t i = 0; i < g_targets.size(); i++) {
        if(g_targets[i].barcode == bc) return (int)i; /* already saved */
    }
    if(g_targets.size() >= TT_MAX_TARGETS) {
        err = "Target list full (16). Delete a tag first";
        return -1;
    }
    Target t;
    t.barcode = bc;
    t.name = clean_field(name_in, TT_NAME_LEN);
    if(!t.name.length()) t.name = "tag" + String(g_targets.size() + 1);
    target_refresh_profile(t);
    g_targets.push_back(t);
    targets_save();
    return (int)g_targets.size() - 1;
}

bool target_delete(int idx) {
    if(idx < 0 || idx >= (int)g_targets.size()) return false;
    g_targets.erase(g_targets.begin() + idx);
    return targets_save();
}

bool target_rename(int idx, const String& name) {
    if(idx < 0 || idx >= (int)g_targets.size()) return false;
    String n = clean_field(name, TT_NAME_LEN);
    if(!n.length()) return false;
    g_targets[idx].name = n;
    return targets_save();
}

bool target_set_custom(int idx, uint16_t w, uint16_t h, uint8_t color) {
    if(idx < 0 || idx >= (int)g_targets.size()) return false;
    if(w && (w < TAGTINKER_CUSTOM_SIZE_MIN || w > TAGTINKER_CUSTOM_SIZE_MAX)) return false;
    if(h && (h < TAGTINKER_CUSTOM_SIZE_MIN || h > TAGTINKER_CUSTOM_SIZE_MAX)) return false;
    if(color > TagTinkerTagColorYellow) color = 0;
    Target& t = g_targets[idx];
    t.custom_w = (uint16_t)(w & ~7U); /* byte aligned width */
    t.custom_h = h;
    t.custom_color = color;
    target_refresh_profile(t);
    return targets_save();
}

/* ---------------------------------------------------------------- texts */

static void texts_save() {
    File f = LittleFS.open(TEXTS_FILE, "w");
    if(!f) return;
    for(auto& s : g_texts) f.println(s);
    f.close();
}

static void texts_load() {
    g_texts.clear();
    File f = LittleFS.open(TEXTS_FILE, "r");
    if(f) {
        while(f.available() && g_texts.size() < TT_MAX_TEXTS) {
            String l = f.readStringUntil('\n');
            l.trim();
            if(l.length()) g_texts.push_back(l);
        }
        f.close();
    }
    if(g_texts.empty()) {
        g_texts.push_back("Hello from M5!");
        g_texts.push_back("TagTinker");
        g_texts.push_back("0.99");
    }
}

void texts_add(const String& text) {
    String t = clean_field(text, TT_TEXT_LEN);
    if(!t.length()) return;
    for(size_t i = 0; i < g_texts.size(); i++) {
        if(g_texts[i] == t) {
            g_texts.erase(g_texts.begin() + i);
            break;
        }
    }
    g_texts.insert(g_texts.begin(), t);
    while(g_texts.size() > TT_MAX_TEXTS) g_texts.pop_back();
    texts_save();
}

bool texts_delete(int idx) {
    if(idx < 0 || idx >= (int)g_texts.size()) return false;
    g_texts.erase(g_texts.begin() + idx);
    texts_save();
    return true;
}

/* ---------------------------------------------------------------- settings */

static void settings_load() {
    Preferences p;
    p.begin("tagtinker", true);
    Settings& s = g_settings;
    s.data_repeats = p.getUChar("drep", s.data_repeats);
    s.compression = p.getUChar("comp", s.compression);
    s.ir_pin = p.getInt("irpin", s.ir_pin);
    s.ir_invert = p.getBool("irinv", s.ir_invert);
    s.brightness = p.getUChar("bright", s.brightness);
    s.sound = p.getBool("sound", s.sound);
    s.ap_ssid = p.getString("apssid", s.ap_ssid);
    s.ap_pass = p.getString("appass", s.ap_pass);
    s.sta_ssid = p.getString("stassid", s.sta_ssid);
    s.sta_pass = p.getString("stapass", s.sta_pass);
    s.txt_invert = p.getBool("tinv", s.txt_invert);
    s.txt_accent = p.getBool("tacc", s.txt_accent);
    s.txt_padding = p.getUChar("tpad", s.txt_padding);
    s.page = p.getUChar("page", s.page);
    s.bc_page = p.getUChar("bcpage", s.bc_page);
    s.bc_duration = p.getUShort("bcdur", s.bc_duration);
    s.bc_forever = p.getBool("bcfor", s.bc_forever);
    s.bc_repeats = p.getUShort("bcrep", s.bc_repeats);
    p.end();

    if(s.data_repeats < 1) s.data_repeats = 1;
    if(s.data_repeats > 10) s.data_repeats = 10;
    if(s.compression > TagTinkerCompressionRle) s.compression = 0;
    if(s.page < 1 || s.page > 8) s.page = 1;
    if(s.bc_page > 7) s.bc_page = 0;
    if(s.txt_padding > 45) s.txt_padding = 45;
    if(s.ap_pass.length() < 8) s.ap_pass = "tagtinker";
}

void settings_save() {
    Preferences p;
    p.begin("tagtinker", false);
    const Settings& s = g_settings;
    p.putUChar("drep", s.data_repeats);
    p.putUChar("comp", s.compression);
    p.putInt("irpin", s.ir_pin);
    p.putBool("irinv", s.ir_invert);
    p.putUChar("bright", s.brightness);
    p.putBool("sound", s.sound);
    p.putString("apssid", s.ap_ssid);
    p.putString("appass", s.ap_pass);
    p.putString("stassid", s.sta_ssid);
    p.putString("stapass", s.sta_pass);
    p.putBool("tinv", s.txt_invert);
    p.putBool("tacc", s.txt_accent);
    p.putUChar("tpad", s.txt_padding);
    p.putUChar("page", s.page);
    p.putUChar("bcpage", s.bc_page);
    p.putUShort("bcdur", s.bc_duration);
    p.putBool("bcfor", s.bc_forever);
    p.putUShort("bcrep", s.bc_repeats);
    p.end();
}

String ap_ssid_effective() {
    if(g_settings.ap_ssid.length()) return g_settings.ap_ssid;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "TagTinker-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

/* ---------------------------------------------------------------- images */

String image_sanitize_name(const String& name_in) {
    String base = name_in;
    int slash = base.lastIndexOf('/');
    if(slash >= 0) base = base.substring(slash + 1);
    String o;
    for(size_t i = 0; i < base.length() && o.length() < 28; i++) {
        char c = base[i];
        if(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o += c;
        else if(c == ' ') o += '_';
    }
    if(!o.length()) o = "image";
    String lower = o;
    lower.toLowerCase();
    if(!lower.endsWith(".bmp")) o += ".bmp";
    return o;
}

String image_path(const String& name) {
    return String(TT_IMG_DIR) + "/" + image_sanitize_name(name);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool image_info(const String& name, ImageInfo& info) {
    File f = LittleFS.open(image_path(name), "r");
    if(!f) return false;
    uint8_t h[54];
    bool ok = f.read(h, sizeof(h)) == sizeof(h) && h[0] == 'B' && h[1] == 'M';
    info.name = image_sanitize_name(name);
    info.size = f.size();
    if(ok) {
        info.bpp = h[28] | (h[29] << 8);
        uint32_t w = le32(&h[18]);
        uint32_t hh = le32(&h[22]);
        if(hh & 0x80000000U) hh = ~hh + 1U;
        info.w = (uint16_t)w;
        info.h = (uint16_t)hh;
    }
    f.close();
    return ok;
}

std::vector<ImageInfo> images_list() {
    std::vector<ImageInfo> out;
    File dir = LittleFS.open(TT_IMG_DIR);
    if(!dir || !dir.isDirectory()) return out;
    File f = dir.openNextFile();
    while(f) {
        if(!f.isDirectory()) {
            ImageInfo info;
            String n = f.name();
            f.close();
            if(image_info(n, info)) out.push_back(info);
        } else {
            f.close();
        }
        f = dir.openNextFile();
    }
    std::sort(out.begin(), out.end(), [](const ImageInfo& a, const ImageInfo& b) {
        return a.name < b.name;
    });
    return out;
}

bool image_delete(const String& name) {
    return LittleFS.remove(image_path(name));
}

/* ---------------------------------------------------------------- init */

bool store_begin() {
    bool ok = LittleFS.begin(true);
    if(ok && !LittleFS.exists(TT_IMG_DIR)) LittleFS.mkdir(TT_IMG_DIR);
    settings_load();
    if(ok) {
        targets_load();
        texts_load();
    }
    return ok;
}
