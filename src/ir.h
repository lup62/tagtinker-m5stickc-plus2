/*
 * IR transmitter for M5StickC Plus2 (ESP32 RMT peripheral).
 *
 * Replaces the Flipper TIM1/DWT implementation: the RMT block generates the
 * 1.25 MHz carrier and the PP4 symbol timing in hardware, so frames are
 * jitter-free even with Wi-Fi running.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool ir_init(int gpio, bool invert);
void ir_deinit(void);

/* Sends the frame (repeats + 1) times. delay = gap between repeats in 500 us
 * units, same semantics as tagtinker_ir_transmit() on the Flipper. */
bool ir_transmit(const uint8_t* data, size_t len, uint16_t repeats, uint8_t delay);

/* Plain carrier bursts for a camera check of the LED (no ESL frame). */
bool ir_test_carrier(uint32_t ms);

void ir_request_stop(void);
void ir_clear_stop(void);
bool ir_stop_requested(void);
