#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#include <stdint.h>
#include <stdbool.h>

// Pokemon Red/Blue data structure constants
#define POKEMON_DATA_SIZE 44  // Core Pokemon data (without nickname/OT name)
#define POKEMON_NAME_LENGTH 11
#define POKEMON_OT_NAME_LENGTH 11
#define MAX_STORED_POKEMON 256

// Link cable trading protocol constants
#define TRADE_SYNC_BYTE 0x55
#define TRADE_ACK_BYTE 0x99
#define TRADE_CONFIRM_BYTE 0x66
#define TRADE_CANCEL_BYTE 0x77

// Trading states
typedef enum {
    TRADE_STATE_IDLE,
    TRADE_STATE_WAITING_FOR_PARTNER,
    TRADE_STATE_CONNECTED,
    TRADE_STATE_RECEIVING_POKEMON,
    TRADE_STATE_SENDING_POKEMON,
    TRADE_STATE_CONFIRMING,
    TRADE_STATE_COMPLETE,
    TRADE_STATE_ERROR
} trade_state_t;

// Pokemon data structure (Gen 1 format) - Matches official Bulbapedia specification
typedef struct __attribute__((packed)) {
    uint8_t species;               // 0x00: Pokemon species ID (1 byte)
    uint16_t current_hp;          // 0x01: Current HP (2 bytes)
    uint8_t level;                // 0x03: Pokemon level (1 byte)
    uint8_t status;               // 0x04: Status conditions (1 byte)
    uint8_t type1;                // 0x05: Primary type (1 byte)
    uint8_t type2;                // 0x06: Secondary type (1 byte)
    uint8_t catch_rate;           // 0x07: Catch rate/held item (1 byte)
    uint8_t moves[4];             // 0x08-0x0B: Move IDs (4 bytes)
    uint16_t original_trainer_id; // 0x0C: Original trainer ID (2 bytes)
    uint8_t experience[3];        // 0x0E: Experience points (3 bytes)
    uint16_t hp_exp;             // 0x11: HP stat experience (2 bytes)
    uint16_t attack_exp;         // 0x13: Attack stat experience (2 bytes)
    uint16_t defense_exp;        // 0x15: Defense stat experience (2 bytes)
    uint16_t speed_exp;          // 0x17: Speed stat experience (2 bytes)
    uint16_t special_exp;        // 0x19: Special stat experience (2 bytes)
    uint8_t iv_data[2];          // 0x1B: IV data (2 bytes)
    uint8_t move_pp[4];          // 0x1D-0x20: PP for each move (4 bytes)
    uint8_t level_copy;          // 0x21: Level (duplicate) (1 byte)
    uint16_t max_hp;             // 0x22: Maximum HP (2 bytes)
    uint16_t attack;             // 0x24: Attack stat (2 bytes)
    uint16_t defense;            // 0x26: Defense stat (2 bytes)
    uint16_t speed;              // 0x28: Speed stat (2 bytes)
    uint16_t special;            // 0x2A: Special stat (2 bytes)
} pokemon_core_data_t;

// Complete Pokemon data including nickname and trainer info (stored separately per Gen I spec)
typedef struct {
    pokemon_core_data_t core;                      // Core 44-byte Pokemon data
    char nickname[POKEMON_NAME_LENGTH];            // Pokemon nickname (11 bytes, stored separately)
    char ot_name[POKEMON_OT_NAME_LENGTH];          // Original trainer name (11 bytes, stored separately)
} pokemon_data_t;

// Storage slot for Pokemon
typedef struct {
    bool occupied;
    uint32_t timestamp;
    pokemon_data_t pokemon;
    char game_version[16];  // Which game it came from
    uint8_t checksum;       // Data integrity check
} pokemon_slot_t;

// Trading session info with enhanced trainer data handling
typedef struct {
    trade_state_t state;
    uint32_t session_start_time;
    pokemon_data_t incoming_pokemon;
    pokemon_data_t outgoing_pokemon;
    bool has_incoming_data;
    bool trade_confirmed;
    uint8_t error_count;
    char partner_name[16];
    bool needs_internal_reset; // Flag to reset internal static states
    
    // Enhanced trainer/party information
    uint16_t local_trainer_id;                     // Our trainer ID for outgoing Pokemon
    char local_trainer_name[POKEMON_OT_NAME_LENGTH]; // Our trainer name for outgoing Pokemon
    uint8_t local_party_count;                     // Number of Pokemon in our party
    bool bidirectional_mode;                       // True when both sides send simultaneously
} trade_session_t;

// Function declarations
bool pokemon_validate_data(const pokemon_data_t* pokemon);
uint8_t pokemon_calculate_checksum(const pokemon_data_t* pokemon);
const char* pokemon_get_species_name(uint8_t species_id);
const char* pokemon_get_type_name(uint8_t type_id);
const char* pokemon_get_move_name(uint8_t move_id);
const char* trade_state_to_string(trade_state_t state);

#endif // POKEMON_DATA_H 