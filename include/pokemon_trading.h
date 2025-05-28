#ifndef POKEMON_TRADING_H
#define POKEMON_TRADING_H

#include <stddef.h>
#include "pokemon_data.h"
#include "linkcable.h"

// Trading protocol responses
#define TRADE_RESPONSE_SUCCESS 0x00
#define TRADE_RESPONSE_ERROR 0xFF
#define TRADE_RESPONSE_BUSY 0xFE
#define TRADE_RESPONSE_STORAGE_FULL 0xFD

// Function declarations
void pokemon_trading_init(void);
void pokemon_trading_update(void);
void pokemon_trading_reset(void);

// Protocol handlers
uint8_t pokemon_handle_trade_request(uint8_t command, uint8_t* data, size_t length);
void pokemon_send_trade_response(uint8_t response_code);

// Storage management
bool pokemon_store_received(const pokemon_data_t* pokemon, const char* source_game);
pokemon_slot_t* pokemon_get_stored_list(void);
size_t pokemon_get_stored_count(void);
bool pokemon_delete_stored(size_t index);
bool pokemon_send_stored(size_t index);

// Trading state management
trade_state_t pokemon_get_trade_state(void);
const char* pokemon_get_last_error(void);
trade_session_t* pokemon_get_current_session(void);

// Diagnostic functions
void pokemon_log_trade_event(const char* event, const char* details);
const char* pokemon_get_trade_log(void);

#endif // POKEMON_TRADING_H 