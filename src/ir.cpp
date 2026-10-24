/*
 * IR transmitter: PP4 over RMT.
 *
 * Timing is taken 1:1 from the Flipper implementation (tagtinker_ir.c):
 *   carrier  : 64 MHz / 51  -> ~1.25 MHz, 50 % duty
 *   burst    : 2581 cycles  -> 40.3 us
 *   gaps     : symbol 0 = 60.5 us, 1 = 241.9 us, 2 = 120.9 us, 3 = 181.4 us
 * Two bits per symbol, LSB pair first, closing burst at the end.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "ir.h"

#include <Arduino.h>
#include "driver/rmt_tx.h"
#include "driver/gpio.h"

#define IR_RES_HZ       10000000UL /* 0.1 us per tick */
#define IR_CARRIER_HZ   1250000UL
#define IR_BURST_TICKS  403U
static const uint16_t kGapTicks[4] = {605U, 2419U, 1209U, 1814U};

#define TAGTINKER_IR_MAX_BYTES 128U
#define IR_MAX_SYMBOLS  (TAGTINKER_IR_MAX_BYTES * 4U + 1U)

static rmt_channel_handle_t s_chan = nullptr;
static rmt_encoder_handle_t s_enc = nullptr;
static volatile bool s_stop = false;
static rmt_symbol_word_t s_symbols[IR_MAX_SYMBOLS];

bool ir_init(int gpio, bool invert) {
    if(s_chan) return true;

    rmt_tx_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.gpio_num = (gpio_num_t)gpio;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = IR_RES_HZ;
    cfg.mem_block_symbols = 256; /* a whole ESL frame fits, no refill IRQs */
    cfg.trans_queue_depth = 4;
    cfg.flags.invert_out = invert ? 1 : 0;
    cfg.flags.with_dma = 0;
    if(rmt_new_tx_channel(&cfg, &s_chan) != ESP_OK) {
        /* Fall back to a single memory block if others are in use. */
        cfg.mem_block_symbols = 64;
        if(rmt_new_tx_channel(&cfg, &s_chan) != ESP_OK) {
            s_chan = nullptr;
            return false;
        }
    }

    rmt_carrier_config_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.frequency_hz = IR_CARRIER_HZ;
    cc.duty_cycle = 0.5f;
    cc.flags.polarity_active_low = 0;
    if(rmt_apply_carrier(s_chan, &cc) != ESP_OK) {
        rmt_del_channel(s_chan);
        s_chan = nullptr;
        return false;
    }

    rmt_copy_encoder_config_t ec;
    memset(&ec, 0, sizeof(ec));
    if(rmt_new_copy_encoder(&ec, &s_enc) != ESP_OK) {
        rmt_del_channel(s_chan);
        s_chan = nullptr;
        return false;
    }

    if(rmt_enable(s_chan) != ESP_OK) {
        rmt_del_encoder(s_enc);
        rmt_del_channel(s_chan);
        s_enc = nullptr;
        s_chan = nullptr;
        return false;
    }
    s_stop = false;
    return true;
}

void ir_deinit(void) {
    if(!s_chan) return;
    rmt_tx_wait_all_done(s_chan, 200);
    rmt_disable(s_chan);
    rmt_del_encoder(s_enc);
    rmt_del_channel(s_chan);
    s_enc = nullptr;
    s_chan = nullptr;
}

static size_t build_symbols(const uint8_t* data, size_t len) {
    size_t n = 0;
    for(size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for(int s = 0; s < 4; s++) {
            uint8_t sym = b & 0x03U;
            b >>= 2;
            s_symbols[n].level0 = 1;
            s_symbols[n].duration0 = IR_BURST_TICKS;
            s_symbols[n].level1 = 0;
            s_symbols[n].duration1 = kGapTicks[sym];
            n++;
        }
    }
    /* Closing burst. */
    s_symbols[n].level0 = 1;
    s_symbols[n].duration0 = IR_BURST_TICKS;
    s_symbols[n].level1 = 0;
    s_symbols[n].duration1 = 10;
    n++;
    return n;
}

static bool send_symbols(size_t n) {
    rmt_transmit_config_t tc;
    memset(&tc, 0, sizeof(tc));
    tc.loop_count = 0;
    tc.flags.eot_level = 0;
    if(rmt_transmit(s_chan, s_enc, s_symbols, n * sizeof(rmt_symbol_word_t), &tc) != ESP_OK)
        return false;
    return rmt_tx_wait_all_done(s_chan, 1000) == ESP_OK;
}

static void gap_delay(uint8_t delay) {
    if(delay == 0) return;
    uint32_t us = (uint32_t)delay * 500U;
    if(us >= 2000U) {
        vTaskDelay(pdMS_TO_TICKS(us / 1000U));
    } else {
        delayMicroseconds(us);
    }
}

bool ir_transmit(const uint8_t* data, size_t len, uint16_t repeats_raw, uint8_t delay) {
    if(!s_chan || !data || len == 0 || len > TAGTINKER_IR_MAX_BYTES) return false;
    uint32_t repeats = repeats_raw & 0x7FFFU;
    size_t n = build_symbols(data, len);

    for(uint32_t rep = 0; rep <= repeats; rep++) {
        if(s_stop) return false;
        if(!send_symbols(n)) return false;
        if(rep < repeats) {
            gap_delay(delay);
            /* Keep the idle task (and its watchdog) fed on long bursts. */
            if((rep % 20U) == 19U) vTaskDelay(1);
        }
    }
    return true;
}

bool ir_test_carrier(uint32_t ms) {
    if(!s_chan) return false;
    s_stop = false;
    uint32_t start = millis();
    for(size_t i = 0; i < 8; i++) {
        s_symbols[i].level0 = 1;
        s_symbols[i].duration0 = 30000; /* 3 ms carrier */
        s_symbols[i].level1 = 0;
        s_symbols[i].duration1 = 10000; /* 1 ms off */
    }
    while(!s_stop && (millis() - start) < ms) {
        if(!send_symbols(8)) return false;
        vTaskDelay(1);
    }
    return !s_stop;
}

void ir_request_stop(void) {
    s_stop = true;
}

bool ir_stop_requested(void) {
    return s_stop;
}

void ir_clear_stop(void) {
    s_stop = false;
}
