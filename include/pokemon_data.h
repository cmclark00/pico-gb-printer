#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t

// Pokemon Red/Blue data structure constants
#define POKEMON_DATA_SIZE 44  // Core Pokemon data (without nickname/OT name)
#define POKEMON_NAME_LENGTH 11
#define POKEMON_OT_NAME_LENGTH 11
#define MAX_STORED_POKEMON 256

// Link Cable Protocol Bytes (Gen 1 Focus)
#define PKMN_MASTER         0x01 // Master device identification
#define PKMN_SLAVE          0x02 // Slave device identification
#define PKMN_BLANK          0x00 // Blank / Continue / Padding byte
// Specific menu/action bytes observed in traces or from Flipper code
#define PKMN_CONNECTED      0x60 // Handshake byte after master/slave established, often for Trade Center entry
#define PKMN_ACTION_START   0x62 // Often signifies start of an action like trade confirmation

// Gen I Menu Bytes (Cable Club)
#define PKMN_MENU_TRADE_CENTRE_HIGHLIGHTED 0xD0
#define PKMN_MENU_COLOSSEUM_HIGHLIGHTED    0xD1
#define PKMN_MENU_CANCEL_HIGHLIGHTED       0xD2
// Values 0xD3 might be other menu highlights if applicable
#define PKMN_MENU_TRADE_CENTRE_SELECTED    0xD4
#define PKMN_MENU_COLOSSEUM_SELECTED       0xD5
#define PKMN_MENU_CANCEL_SELECTED          0xD6

// Gen 1 Trade Sequence Constants (from Flipper Zero trade.c)
#define SERIAL_PREAMBLE_BYTE               0xFD
#define SERIAL_RNS_LENGTH                  10 // Number of initial 0xFDs, also number of random numbers
#define SERIAL_TRADE_BLOCK_PREAMBLE_LENGTH 9  // Number of 0xFDs after random numbers, before main block
#define SERIAL_PATCH_LIST_PART_TERMINATOR  0xFF
#define SERIAL_NO_DATA_BYTE                0xFE // Byte value that needs patching if in data

// Gen 1 Trade Action Bytes (from Flipper)
#define PKMN_TRADE_ACCEPT       0x62 // Player accepts the trade (Gen I: PKMN_TRADE_ACCEPT_GEN_I)
#define PKMN_TRADE_REJECT       0x61 // Player rejects the trade (Gen I: PKMN_TRADE_REJECT_GEN_I)
#define PKMN_TABLE_LEAVE        0x6F // Player leaves the trade table (Gen I: PKMN_TABLE_LEAVE_GEN_I)
#define PKMN_SELECT_MON_MASK    0x60 // Mask for selecting a Pokemon (Gen I: PKMN_SEL_NUM_MASK_GEN_I)
#define PKMN_SELECT_MON_ONE     0x60 // Value for selecting the first Pokemon (Gen I: PKMN_SEL_NUM_ONE_GEN_I)

// Link cable trading protocol constants
#define TRADE_SYNC_BYTE 0x55
#define TRADE_ACK_BYTE 0x99
#define TRADE_CONFIRM_BYTE 0x66
#define TRADE_CANCEL_BYTE 0x77

// Pokemon Types (Gen 1)
// Based on common assignments, e.g., https://bulbapedia.bulbagarden.net/wiki/Type#List_of_types
// and Flipper Zero source
#define POKEMON_TYPE_NORMAL   0
#define POKEMON_TYPE_FIGHTING 1
#define POKEMON_TYPE_FLYING   2
#define POKEMON_TYPE_POISON   3
#define POKEMON_TYPE_GROUND   4
#define POKEMON_TYPE_ROCK     5
// #define POKEMON_TYPE_BIRD  6 // Glitch type
#define POKEMON_TYPE_BUG      7
#define POKEMON_TYPE_GHOST    8
// Types 0x09 to 0x13 are unused or glitch types in Gen 1
#define POKEMON_TYPE_FIRE     20 // 0x14
#define POKEMON_TYPE_WATER    21 // 0x15
#define POKEMON_TYPE_GRASS    22 // 0x16
#define POKEMON_TYPE_ELECTRIC 23 // 0x17
#define POKEMON_TYPE_PSYCHIC  24 // 0x18
#define POKEMON_TYPE_ICE      25 // 0x19
#define POKEMON_TYPE_DRAGON   26 // 0x1A

// Trading states
typedef enum {
    TRADE_STATE_IDLE,
    TRADE_STATE_WAITING_FOR_PARTNER,
    TRADE_STATE_CONNECTED,
    TRADE_STATE_RECEIVING_POKEMON,
    TRADE_STATE_SENDING_POKEMON,
    TRADE_STATE_EXCHANGING_BLOCKS,
    TRADE_STATE_PATCH_PREAMBLE,
    TRADE_STATE_PATCH_DATA_EXCHANGE,
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

// Helper for endian swapping
static inline uint16_t bswap16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

// Structure for the full Gen 1 trade block (415 bytes)
// This matches the structure used in many Game Boy trading implementations.
typedef struct __attribute__((packed)) {
    char player_trainer_name[POKEMON_NAME_LENGTH]; // Player's trainer name (11 bytes)
    uint8_t party_count;                           // Number of Pokémon in party (usually 1 for trade) (1 byte)
    uint8_t party_species[7];                      // Species IDs of party Pokémon (6 bytes) + 0xFF terminator (1 byte)
                                                   // Typically, only the first is the traded Pokémon, others 0xFF.
    pokemon_core_data_t pokemon_data[6];           // Array of 6 Pokémon core data structures (6 * 44 = 264 bytes)

    char original_trainer_names[6][POKEMON_NAME_LENGTH]; // OT names for all 6 party Pokémon (6 * 11 = 66 bytes)
    char pokemon_nicknames[6][POKEMON_NAME_LENGTH];      // Nicknames for all 6 party Pokémon (6 * 11 = 66 bytes)
} trade_block_t; // Total expected: 11+1+7+264+66+66 = 415 bytes.

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
    bool our_block_sent_this_exchange;             // True if we've sent our trade_block in the current exchange

    // Internal sub-state for complex sequences like block exchange
    uint8_t trade_exchange_sub_state; 
    size_t exchange_counter; // Counter for bytes in sub-sequences

    // Buffer for receiving the partner's trade block
    trade_block_t incoming_trade_block_buffer; // Buffer to store the raw incoming trade block
    size_t incoming_pokemon_bytes_count;     // Counter for bytes received for the trade block
} trade_session_t;

// Define for trade_exchange_sub_state values
#define TRADE_SUBSTATE_NONE                  0
#define TRADE_SUBSTATE_INITIAL_PREAMBLE      1 // Expecting first set of 0xFDs
#define TRADE_SUBSTATE_RANDOM_NUMBERS        2 // Expecting random numbers (just echoing)
#define TRADE_SUBSTATE_FINAL_PREAMBLE        3 // Expecting second set of 0xFDs
#define TRADE_SUBSTATE_EXCHANGING_BLOCKS     4 // Main data block byte-for-byte exchange
// Further substates for patch list if implemented later

// Function declarations
bool pokemon_validate_data(const pokemon_data_t* pokemon);
uint8_t pokemon_calculate_checksum(const pokemon_data_t* pokemon);
const char* pokemon_get_species_name(uint8_t species_id);
const char* pokemon_get_type_name(uint8_t type_id);
const char* pokemon_get_move_name(uint8_t move_id);
const char* trade_state_to_string(trade_state_t state);

#endif // POKEMON_DATA_H 