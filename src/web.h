/*
 * Wi-Fi access point + web control panel.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <Arduino.h>

void web_begin();
void web_loop();
String web_ap_ip();
String web_sta_ip();      /* empty when not connected */
String web_ap_ssid();
bool web_sta_connected();
