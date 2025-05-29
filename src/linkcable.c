#include <stdint.h>
#include <stddef.h> // For size_t
#include <string.h> // For memcpy
#include <stdbool.h> // For bool, true, false

#include "hardware/pio.h"
#include "hardware/irq.h"

#include "linkcable.h"

#include "linkcable.pio.h"

static irq_handler_t linkcable_irq_handler = NULL;
static uint32_t linkcable_pio_initial_pc = 0;

static void linkcable_isr(void) {
    if (linkcable_irq_handler) linkcable_irq_handler();
    if (pio_interrupt_get(LINKCABLE_PIO, 0)) pio_interrupt_clear(LINKCABLE_PIO, 0);
}

void linkcable_reset(void) {
    pio_sm_set_enabled(LINKCABLE_PIO, LINKCABLE_SM, false);
    pio_sm_clear_fifos(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_restart(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_clkdiv_restart(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_exec(LINKCABLE_PIO, LINKCABLE_SM, pio_encode_jmp(linkcable_pio_initial_pc));
    pio_sm_set_enabled(LINKCABLE_PIO, LINKCABLE_SM, true);
}

void linkcable_init(irq_handler_t onDataReceive) {
    linkcable_program_init(LINKCABLE_PIO, LINKCABLE_SM, linkcable_pio_initial_pc = pio_add_program(LINKCABLE_PIO, &linkcable_program));

    // Put initial value in TX FIFO so PIO can respond
    pio_sm_put_blocking(LINKCABLE_PIO, LINKCABLE_SM, 0x00);
    pio_enable_sm_mask_in_sync(LINKCABLE_PIO, (1u << LINKCABLE_SM));

    if (onDataReceive) {
        linkcable_irq_handler = onDataReceive;
        pio_set_irq0_source_enabled(LINKCABLE_PIO, pis_interrupt0, true);
        irq_set_exclusive_handler(PIO0_IRQ_0, linkcable_isr);
        irq_set_enabled(PIO0_IRQ_0, true);
    }
}

void linkcable_send_data(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        // The existing linkcable_send is static inline in the .h file.
        // We need to use the PIO function directly here or make linkcable_send non-inline.
        // For now, using pio_sm_put_blocking for simplicity to ensure byte is sent.
        pio_sm_put_blocking(LINKCABLE_PIO, LINKCABLE_SM, data[i]);
        // TODO: Add a small delay or check PIO state if needed, 
        // especially if the other side is slow or for protocol timing.
    }
}

void linkcable_send_trade_block(const trade_block_t* trade_block) {
    if (!trade_block) return;

    trade_block_t temp_block;
    memcpy(&temp_block, trade_block, sizeof(trade_block_t));

    // We only expect one Pokemon in the trade block for a single trade
    // Perform byte swapping for uint16_t fields in the first Pokemon's data
    // The bswap16 function is in pokemon_data.h, which linkcable.h now includes.
    // It's static inline, so it should be available.

    pokemon_core_data_t* pkmn_core = &temp_block.pokemon_data[0];

    pkmn_core->current_hp           = bswap16(pkmn_core->current_hp);
    pkmn_core->original_trainer_id  = bswap16(pkmn_core->original_trainer_id);
    pkmn_core->hp_exp                = bswap16(pkmn_core->hp_exp);
    pkmn_core->attack_exp            = bswap16(pkmn_core->attack_exp);
    pkmn_core->defense_exp           = bswap16(pkmn_core->defense_exp);
    pkmn_core->speed_exp             = bswap16(pkmn_core->speed_exp);
    pkmn_core->special_exp           = bswap16(pkmn_core->special_exp);
    // iv_data is uint8_t[2], no swap needed for the array itself.
    // individual IVs are packed nibbles, no swap needed.
    pkmn_core->max_hp               = bswap16(pkmn_core->max_hp);
    pkmn_core->attack          = bswap16(pkmn_core->attack);
    pkmn_core->defense         = bswap16(pkmn_core->defense);
    pkmn_core->speed           = bswap16(pkmn_core->speed);
    pkmn_core->special         = bswap16(pkmn_core->special);

    // Species, level, status, types, catch_rate, moves, exp, pp, level_copy are uint8_t or uint8_t arrays - no swap.

    // Now send the modified block
    linkcable_send_data((const uint8_t*)&temp_block, sizeof(trade_block_t));
}