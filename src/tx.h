/*
 * Transmit engine: runs ESL jobs on a dedicated FreeRTOS task.
 * Port of scenes/tagtinker_scene_transmit.c (Flipper) to ESP32.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <Arduino.h>

enum TxJobType {
    TX_NONE = 0,
    TX_PING_FLASH,
    TX_BROADCAST_PAGE,
    TX_BROADCAST_DEBUG,
    TX_TEXT,
    TX_IMAGE,
    TX_IR_TEST,
};

struct TxJob {
    TxJobType type = TX_NONE;
    int target = -1;        /* index into g_targets */
    uint8_t page = 1;       /* UI page 1..8 for text/image, 0..7 for broadcast */
    bool forever = false;
    uint16_t duration = 15;
    uint16_t repeats = 200;
    bool spam = false;      /* keep repeating until stopped */
    String text;
    bool invert = false;
    bool accent = false;    /* "color clear": draw text in the accent colour */
    uint8_t padding = 5;
    String image;           /* image file name in the library */
};

struct TxStatus {
    bool busy;
    bool finished;          /* last job finished (ok / failed / cancelled) */
    bool ok;
    bool cancelled;
    TxJobType type;
    uint32_t done;
    uint32_t total;
    uint32_t seq;           /* increments at every job end */
    char msg[72];
};

void tx_begin();
bool tx_start(const TxJob& job, String& err);
void tx_stop();
TxStatus tx_status();
const char* tx_job_label(TxJobType t);
