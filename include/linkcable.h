#ifndef _LINKCABLE_H_INCLUDE_
#define _LINKCABLE_H_INCLUDE_

#include <stddef.h> // For size_t
#include "hardware/pio.h"
#include "pokemon_data.h" // For trade_block_t definition

#define LINKCABLE_PIO       pio0
#define LINKCABLE_SM        0

#define LINKCABLE_BITS      8

static inline uint8_t linkcable_receive(void) {
    return pio_sm_get(LINKCABLE_PIO, LINKCABLE_SM);
}

static inline void linkcable_send(uint8_t data) {
    pio_sm_put(LINKCABLE_PIO, LINKCABLE_SM, data);
}

void linkcable_reset(void);
void linkcable_init(irq_handler_t onReceive);

// Function to send a block of data
void linkcable_send_data(const uint8_t* data, size_t length);

// Function to prepare and send a Pokemon trade block with necessary byte swapping
void linkcable_send_trade_block(const trade_block_t* trade_block);

#endif