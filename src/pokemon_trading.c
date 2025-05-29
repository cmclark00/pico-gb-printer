#include "pokemon_trading.h"
#include "pokemon_data.h"
#include "linkcable.h"
#include "websocket_server.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "char_encode.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Global storage for Pokemon
static pokemon_slot_t pokemon_storage[MAX_STORED_POKEMON];
static size_t stored_pokemon_count = 0;

// Current trading session
static trade_session_t current_session;

// Trade log buffer
static char trade_log[2048];
static size_t log_position = 0;

// Global trade block for the Pokemon this device will offer
static trade_block_t g_trade_block_to_send;

// Error tracking
static char last_error[128];

// Helper function to convert 0x50-terminated names to null-terminated C strings
static void convert_pokemon_name_from_block(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        if (dest && dest_size > 0) dest[0] = '\0'; // Ensure null termination on error or empty input
        return;
    }
    // Assuming src is a uint8_t* effectively, as it contains encoded chars.
    pokemon_encoded_array_to_str_until_terminator(dest, (const uint8_t*)src, dest_size);
    // Explicitly ensure null-termination as a safeguard, in case the above function doesn't guarantee it
    // if the source string fills dest_size without its own terminator.
    if (dest_size > 0) {
        dest[dest_size - 1] = '\0';
    }
}

// Function to create a test Pokemon trade block for trading
static bool pokemon_create_test_trade_block(trade_block_t* trade_data, uint8_t species_id, uint8_t level, const char* pkmn_nickname, const char* pkmn_ot_name, const char* player_trainer_name) {
    if (!trade_data) return false;
    
    // Clear the entire trade block initially
    memset(trade_data, 0, sizeof(trade_block_t));

    // 1. Player's Trainer Name (11 bytes)
    // Ensure source string is not too long for POKEMON_NAME_LENGTH including terminator space
    pokemon_str_to_encoded_array((uint8_t*)trade_data->player_trainer_name, player_trainer_name, POKEMON_NAME_LENGTH, true);

    // 2. Party Count (1 byte)
    trade_data->party_count = 1;

    // 3. Party Species (7 bytes: 6 species + 0xFF terminator)
    memset(trade_data->party_species, 0xFF, sizeof(trade_data->party_species));
    trade_data->party_species[0] = species_id;

    // 4. Pokemon Core Data for the first Pokemon (pokemon_data[0]) (44 bytes)
    // The rest (pokemon_data[1] to pokemon_data[5]) will remain zeroed from the initial memset.
    pokemon_core_data_t* pkmn_core = &trade_data->pokemon_data[0];
    
    // Zero out the specific Pokemon core data struct first
    memset(pkmn_core, 0, sizeof(pokemon_core_data_t));

    // Essential fields for a recognizable Pokemon
    pkmn_core->species = species_id;    // e.g., 0x19 for Pikachu
    pkmn_core->level = level;          // e.g., 25
    pkmn_core->level_copy = level;     // Copy of level
    pkmn_core->catch_rate = 190;       // Pikachu's catch rate (important for validity)

    // Basic stats (minimal values, linkcable_send_trade_block will bswap16 these)
    pkmn_core->current_hp = 20;        // Example: 20 HP
    pkmn_core->max_hp = 20;            // Example: 20 Max HP
    pkmn_core->attack = 5;
    pkmn_core->defense = 5;
    pkmn_core->speed = 5;
    pkmn_core->special = 5;

    // Trainer ID (linkcable_send_trade_block will bswap16 this)
    pkmn_core->original_trainer_id = 0x1234; // Example OT ID

    // Types (important for display and validity)
    if (species_id == 0x19) { // Pikachu
        pkmn_core->type1 = POKEMON_TYPE_ELECTRIC;
        pkmn_core->type2 = POKEMON_TYPE_ELECTRIC;
    } else {
        pkmn_core->type1 = POKEMON_TYPE_NORMAL; // Default for others
        pkmn_core->type2 = POKEMON_TYPE_NORMAL;
    }

    // Minimal moves (e.g., first move Pound, rest empty)
    pkmn_core->moves[0] = 1; // Pound
    pkmn_core->moves[1] = 0;
    pkmn_core->moves[2] = 0;
    pkmn_core->moves[3] = 0;
    pkmn_core->move_pp[0] = 35; // PP for Pound
    pkmn_core->move_pp[1] = 0;
    pkmn_core->move_pp[2] = 0;
    pkmn_core->move_pp[3] = 0;

    // Experience points (3 bytes, little-endian for a small amount)
    //uint32_t exp_val = 100; // level * level * level; // Minimal exp
    //pkmn_core->experience[0] = exp_val & 0xFF;
    //pkmn_core->experience[1] = (exp_val >> 8) & 0xFF;
    //pkmn_core->experience[2] = (exp_val >> 16) & 0xFF;
    // EVs and IVs will be zero from memset, which is fine for a basic Pokemon.

    // 5. Original Trainer Names (6 * 11 bytes)
    // Fill the first OT name, rest with terminators.
    pokemon_str_to_encoded_array((uint8_t*)trade_data->original_trainer_names[0], pkmn_ot_name, POKEMON_OT_NAME_LENGTH, true);

    for (int i = 1; i < 6; ++i) {
        // memset with TERM_ directly as these are unused and should be filled with terminator
        memset(trade_data->original_trainer_names[i], TERM_, POKEMON_OT_NAME_LENGTH);
    }

    // 6. Pokemon Nicknames (6 * 11 bytes)
    // Fill the first nickname, rest with terminators.
    pokemon_str_to_encoded_array((uint8_t*)trade_data->pokemon_nicknames[0], pkmn_nickname, POKEMON_NAME_LENGTH, true);

    for (int i = 1; i < 6; ++i) {
        // memset with TERM_ directly
        memset(trade_data->pokemon_nicknames[i], TERM_, POKEMON_NAME_LENGTH);
    }

    // TODO: mail_count and mail_data if ever needed, but Flipper's TradeBlockGenI doesn't use them for basic trades.

    return true;
}

void pokemon_trading_init(void) {
    // Clear all storage slots
    memset(pokemon_storage, 0, sizeof(pokemon_storage));
    stored_pokemon_count = 0;
    
    // Initialize trading session
    memset(&current_session, 0, sizeof(current_session));
    current_session.state = TRADE_STATE_IDLE;
    
    // Initialize local trainer information
    current_session.local_trainer_id = 0x1234; // Default trainer ID for our system
    strncpy(current_session.local_trainer_name, "PICO", POKEMON_OT_NAME_LENGTH - 1);
    current_session.local_trainer_name[POKEMON_OT_NAME_LENGTH - 1] = '\0';
    current_session.local_party_count = 0;
    current_session.bidirectional_mode = false;
    current_session.our_block_sent_this_exchange = false;
    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
    current_session.exchange_counter = 0;
    
    // Clear logs and errors
    memset(trade_log, 0, sizeof(trade_log));
    memset(last_error, 0, sizeof(last_error));
    log_position = 0;
    
    pokemon_log_trade_event("SYSTEM", "Pokemon trading system initialized");
    
    // Add some test Pokemon for trading with proper trainer info
    trade_block_t test_trade_block; // Temporary block for creation
    
    // Add a Pikachu
    if (pokemon_create_test_trade_block(&test_trade_block, 0x19, 25, "PIKACHU", "ASH", current_session.local_trainer_name)) {
        // Copy the created test block to the global block to be sent
        memcpy(&g_trade_block_to_send, &test_trade_block, sizeof(trade_block_t));
        
        // Log message for the new test block creation:
        char init_msg[128];
        snprintf(init_msg, sizeof(init_msg), "Prepared test trade block. Player: %.7s, Pokemon: %.7s (Species: %d, Lvl: %d)", 
                 g_trade_block_to_send.player_trainer_name, 
                 g_trade_block_to_send.pokemon_nicknames[0], 
                 g_trade_block_to_send.pokemon_data[0].species, 
                 g_trade_block_to_send.pokemon_data[0].level);
        pokemon_log_trade_event("SYSTEM", init_msg);
    } else {
        pokemon_log_trade_event("ERROR", "Failed to create test trade block for Pikachu");
    }
    
    // Comment out other test Pokemon creations for now to simplify testing
    // if (pokemon_create_test_trade_block(&test_trade_block, 0x04, 15, "CHARMANDER", "RED", current_session.local_trainer_name)) { 
    //     // memcpy(&g_trade_block_to_send, &test_trade_block, sizeof(trade_block_t)); 
    // }
    
    // if (pokemon_create_test_trade_block(&test_trade_block, 0x07, 20, "SQUIRTLE", "BLUE", current_session.local_trainer_name)) { 
    //     // memcpy(&g_trade_block_to_send, &test_trade_block, sizeof(trade_block_t)); 
    // }
}

void pokemon_trading_update(void) {
    // Check for incoming link cable data
    uint8_t received_byte;
    bool data_available = false;
    
    // Try to receive data from link cable
    received_byte = linkcable_receive();
    if (received_byte != 0xFF) { // 0xFF typically means no data
        data_available = true;
        
        // Log all received bytes for debugging
        extern bool debug_enable;
        if (debug_enable) {
            char debug_msg[64];
            snprintf(debug_msg, sizeof(debug_msg), "RX: 0x%02X (%d)", received_byte, received_byte);
            pokemon_log_trade_event("RAW", debug_msg);
        }
    }
    
    // Debug GPIO states periodically
    extern bool debug_enable;
    if (debug_enable) {
        static uint32_t last_gpio_check = 0;
        uint32_t current_time = to_us_since_boot(get_absolute_time()) / 1000;
        
        // Check GPIO every 5 seconds
        if (current_time - last_gpio_check > 5000) {
            last_gpio_check = current_time;
            
            // Read raw GPIO states
            bool sck_state = gpio_get(2);   // Clock pin
            bool sin_state = gpio_get(0);   // Serial In pin (from Game Boy)
            bool sout_state = gpio_get(3);  // Serial Out pin (to Game Boy)
            
            char gpio_msg[128];
            snprintf(gpio_msg, sizeof(gpio_msg), "GPIO States - SCK:%d SIN:%d SOUT:%d", 
                    sck_state, sin_state, sout_state);
            pokemon_log_trade_event("DEBUG", gpio_msg);
        }
    }
    
    switch (current_session.state) {
        case TRADE_STATE_IDLE:
            // Listen for Pokemon trading communication
            if (data_available) {
                uint8_t response = PKMN_BLANK; // Default response
                static int save_sequence_count = 0; // Should be reset if not in save sequence

                switch (received_byte) {
                    case PKMN_MASTER: // 0x01: Game Boy initiates as master
                        response = PKMN_SLAVE; // We respond as slave
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        pokemon_log_trade_event("STATE", "IDLE -> WAITING_PARTNER (Master/Slave Sync)");
                        save_sequence_count = 0;
                        break;
                    
                    case PKMN_CONNECTED: // 0x60: Often sent by GB when entering trade room / after selections
                        response = PKMN_CONNECTED; // Echo
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER; // Move to a state where we expect menu selections or preamble
                        pokemon_log_trade_event("STATE", "IDLE -> WAITING_PARTNER (Connected 0x60)");
                        save_sequence_count = 0;
                        break;

                    // If we receive a menu highlight byte in IDLE,
                    // it implies the GB is already in the menu. Echo and transition.
                    case PKMN_MENU_TRADE_CENTRE_HIGHLIGHTED: // 0xD0
                    case PKMN_MENU_COLOSSEUM_HIGHLIGHTED:    // 0xD1
                    case PKMN_MENU_CANCEL_HIGHLIGHTED:       // 0xD2
                        response = received_byte; // Echo the highlight byte
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        pokemon_log_trade_event("STATE", "IDLE -> WAITING_PARTNER (Menu Highlight RX in IDLE)");
                        char menu_idle_msg[64];
                        snprintf(menu_idle_msg, sizeof(menu_idle_msg), "IDLE: RX:0x%02X -> TX:0x%02X (Menu Highlight)", received_byte, response);
                        pokemon_log_trade_event("PROTOCOL", menu_idle_msg);
                        save_sequence_count = 0; // Reset save sequence
                        break;

                    case PKMN_MENU_TRADE_CENTRE_SELECTED: // 0xD4: Trade Center selected while in IDLE
                        response = PKMN_BLANK; // Respond with BLANK (consistent with WAITING_FOR_PARTNER state's response)
                        current_session.state = TRADE_STATE_CONNECTED; // Transition to expect preamble
                        pokemon_log_trade_event("STATE", "IDLE -> CONNECTED (Trade Center Selected in IDLE)");
                        char tc_selected_idle_msg[64];
                        snprintf(tc_selected_idle_msg, sizeof(tc_selected_idle_msg), "IDLE: RX:0x%02X -> TX:0x%02X (Trade Center Selected)", received_byte, response);
                        pokemon_log_trade_event("PROTOCOL", tc_selected_idle_msg);
                        save_sequence_count = 0; // Reset save sequence
                        break;

                    case SERIAL_PREAMBLE_BYTE: // 0xFD, if received in IDLE, assume trade is starting
                        response = SERIAL_PREAMBLE_BYTE; // Echo 0xFD
                        current_session.state = TRADE_STATE_CONNECTED;
                        // The TRADE_STATE_CONNECTED handler will initialize its sub-state and counter.
                        pokemon_log_trade_event("STATE", "IDLE -> CONNECTED (Preamble 0xFD RX in IDLE)");
                        char preamble_idle_msg[64];
                        snprintf(preamble_idle_msg, sizeof(preamble_idle_msg), "IDLE: RX:0x%02X -> TX:0x%02X (Preamble Start)", received_byte, response);
                        pokemon_log_trade_event("PROTOCOL", preamble_idle_msg);
                        save_sequence_count = 0; // Reset save sequence
                        break;

                    // Handling for save sequence (entering cable club)
                    case 0x03: // Save game / status check
                        response = PKMN_BLANK; // Acknowledge save
                        save_sequence_count++;
                         if (save_sequence_count >= 2) { // Flipper seems to need a few of these
                            current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                            pokemon_log_trade_event("STATE", "IDLE -> WAITING_FOR_PARTNER (Save Ack -> Cable Club Entry)");
                        }
                        break;
                    case PKMN_BLANK: // 0x00: Continue/padding
                        response = PKMN_BLANK;
                        if (save_sequence_count > 0) {
                            save_sequence_count++;
                            if (save_sequence_count >= 3) { // Arbitrary number, check Flipper if specific
                                pokemon_log_trade_event("SAVE", "Save sequence likely complete.");
                                // current_session.state might already be WAITING_FOR_PARTNER
                                save_sequence_count = 0; 
                            }
                        }
                        break;
                    
                    // Catch-all for other unexpected bytes in IDLE, maybe reset or log
                    default:
                        response = PKMN_BLANK; // Safe default, Flipper might send PKMN_BREAK_LINK
                        char unknown_idle_msg[64];
                        snprintf(unknown_idle_msg, sizeof(unknown_idle_msg), "IDLE: RX:0x%02X -> TX:0x%02X (Unexpected)", received_byte, response);
                        pokemon_log_trade_event("DEBUG", unknown_idle_msg);
                        // Consider resetting to CONN_FALSE like Flipper if connection seems broken
                        // For now, just echo blank and stay IDLE or let save_sequence_count manage transition.
                        break;
                }
                
                current_session.session_start_time = to_us_since_boot(get_absolute_time()) / 1000;
                pokemon_send_trade_response(response);
                
                char msg[64];
                snprintf(msg, sizeof(msg), "IDLE RX: 0x%02X -> TX: 0x%02X (save_seq: %d)", 
                        received_byte, response, save_sequence_count);
                pokemon_log_trade_event("PROTOCOL", msg);
                websocket_broadcast_protocol_data(received_byte, response, "IDLE");
            }
            break;
            
        case TRADE_STATE_WAITING_FOR_PARTNER: // Equivalent to Flipper's GAMEBOY_CONN_TRUE (menu navigation)
            if (data_available) {
                uint8_t response = received_byte; // Default: echo most bytes in this state

                switch (received_byte) {
                    case PKMN_MASTER: // 0x01: Unexpected master signal if already connected
                        response = PKMN_SLAVE; // Re-assert slave role
                        current_session.state = TRADE_STATE_IDLE; // Reset to initial connection state
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER -> IDLE (Unexpected Master Signal)");
                        break;

                    case PKMN_BLANK: // 0x00
                        response = PKMN_BLANK;
                        break;

                    case PKMN_CONNECTED: // 0x60, can be part of menu loop
                        response = PKMN_CONNECTED; // Echo
                        break;
                    
                    // Menu Highlighted (echo)
                    case PKMN_MENU_TRADE_CENTRE_HIGHLIGHTED: // 0xD0
                    case PKMN_MENU_COLOSSEUM_HIGHLIGHTED:    // 0xD1
                    case PKMN_MENU_CANCEL_HIGHLIGHTED:       // 0xD2
                        response = received_byte; // Echo highlight
                        pokemon_log_trade_event("MENU", "Menu item highlighted");
                        break;

                    // Menu Selected
                    case PKMN_MENU_TRADE_CENTRE_SELECTED: // 0xD4: Trade Center selected
                        response = PKMN_BLANK; // Flipper responds with BLANK here and changes state
                        current_session.state = TRADE_STATE_CONNECTED; // Our "Ready for preamble" state
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER -> CONNECTED (Trade Center Selected)");
                        pokemon_log_trade_event("MENU", "Trade Center selected, ready for trade data preamble.");
                        break;
                    case PKMN_MENU_COLOSSEUM_SELECTED: // 0xD5: Colosseum selected
                        response = PKMN_BLANK; // Flipper responds with BLANK
                        // We don't implement Colosseum, so perhaps go back to IDLE or log an error.
                        // For now, mimic Flipper's state change to a "Colosseum" mode if we had one, or just log.
                        pokemon_log_trade_event("MENU", "Colosseum selected (not implemented), echoing blank.");
                        // current_session.state = TRADE_STATE_IDLE; // Or a specific Colosseum state
                        break;
                    case PKMN_MENU_CANCEL_SELECTED: // 0xD6: Cancel selected from Cable Club menu
                        response = received_byte; // Echo, Flipper uses PKMN_BREAK_LINK
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER -> IDLE (Cancel Selected)");
                        pokemon_log_trade_event("MENU", "Cancel selected from Cable Club menu.");
                        break;
                    
                    case 0x6F: // PKMN_TABLE_LEAVE_GEN_I (Flipper constant) - User backs out of trade screen
                        response = received_byte; // Echo
                        current_session.state = TRADE_STATE_IDLE; // Or WAITING_FOR_PARTNER if still in club
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER -> IDLE (Partner left table 0x6F)");
                        break;

                    // Bytes that might indicate start of actual trade data sequence (preamble)
                    // Flipper's getTradeCentreResponse handles this after GAMEBOY_READY state.
                    // If we receive 0xFD here, it means Trade Center was selected implicitly or via a different byte.
                    case SERIAL_PREAMBLE_BYTE: // 0xFD - Flipper constant for this
                        response = SERIAL_PREAMBLE_BYTE; // Echo preamble
                        current_session.state = TRADE_STATE_CONNECTED; // Ensure we are in the state to receive the block
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER -> CONNECTED (Preamble 0xFD received)");
                        // The TRADE_STATE_CONNECTED should then handle the preamble sequence.
                        return; 
                        
                    default:
                        // For other bytes not explicitly handled, echo them.
                        // This covers other menu navigation or status bytes.
                        // pokemon_log_trade_event("DEBUG", "WAITING_PARTNER: Default echo.");
                        break;
                }
                
                pokemon_send_trade_response(response);
                
                char partner_msg[64];
                snprintf(partner_msg, sizeof(partner_msg), "WAIT_PARTNER RX: 0x%02X -> TX: 0x%02X", 
                        received_byte, response);
                pokemon_log_trade_event("PROTOCOL", partner_msg);
                websocket_broadcast_protocol_data(received_byte, response, "WAITING_FOR_PARTNER");
            }
            break;
            
        case TRADE_STATE_CONNECTED: // Was: Equivalent to Flipper's GAMEBOY_READY (expecting trade data preamble 0xFD)
                                    // Now: Manages the full preamble, random numbers, and final preamble sequence.
            if (current_session.trade_exchange_sub_state == TRADE_SUBSTATE_NONE) {
                // First time entering this state after Trade Center selection or receiving 0xFD in WAITING_FOR_PARTNER.
                // The response to the byte that caused transition (e.g. PKMN_BLANK for 0xD4, or 0xFD for 0xFD) 
                // was already sent by the previous state (IDLE or WAITING_FOR_PARTNER).
                // We are now setting up to expect the first 0xFD of the preamble sequence if not already received,
                // or to continue the preamble if 0xFD was the trigger.
                
                pokemon_log_trade_event("DEBUG", "TRADE_STATE_CONNECTED: Initializing sub-state.");

                // If we entered because of 0xFD from WAITING_FOR_PARTNER, that 0xFD is the first preamble byte.
                // The `data_available` and `received_byte` in the outer scope are from the current processing cycle.
                // If pokemon_trading_update was called and linkcable_receive() got 0xFD, and that led to this state transition
                // and this block of code, then `received_byte` *is* 0xFD.
                if (data_available && received_byte == SERIAL_PREAMBLE_BYTE) {
                    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_INITIAL_PREAMBLE;
                    current_session.exchange_counter = 1; // Count this 0xFD
                    pokemon_log_trade_event("SUBSTATE", "CONNECTED -> INITIAL_PREAMBLE (0xFD from WAITING_FOR_PARTNER is 1st byte)");
                    pokemon_send_trade_response(SERIAL_PREAMBLE_BYTE); // Echo the 0xFD
                    // We have processed the 0xFD that triggered the state change. We should wait for the next byte.
                    return; 
                } else {
                    // If we entered from 0xD4 (Trade Center select) or other means, 
                    // we are now expecting 0xFD as the *next* byte.
                    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_INITIAL_PREAMBLE;
                    current_session.exchange_counter = 0;
                    pokemon_log_trade_event("SUBSTATE", "CONNECTED -> INITIAL_PREAMBLE (Awaiting first 0xFD)");
                    // No response sent here; the response to 0xD4 (PKMN_BLANK) was handled by IDLE state.
                    // We must wait for a new byte.
                    return; 
                }
            }
            
            if (data_available) { // This will now process a *new* byte after the returns above
                uint8_t response = received_byte; // Default: echo byte

                switch (current_session.trade_exchange_sub_state) {
                    case TRADE_SUBSTATE_INITIAL_PREAMBLE:
                        if (received_byte == SERIAL_PREAMBLE_BYTE) {
                            current_session.exchange_counter++;
                            char log_msg[64];
                            snprintf(log_msg, sizeof(log_msg), "Initial Preamble RX: 0x%02X (%zu/%d)", received_byte, current_session.exchange_counter, SERIAL_RNS_LENGTH);
                            pokemon_log_trade_event("PROTOCOL_DETAIL", log_msg);

                            if (current_session.exchange_counter >= SERIAL_RNS_LENGTH) {
                                current_session.trade_exchange_sub_state = TRADE_SUBSTATE_RANDOM_NUMBERS;
                                current_session.exchange_counter = 0;
                                pokemon_log_trade_event("SUBSTATE", "INITIAL_PREAMBLE -> RANDOM_NUMBERS");
                            }
                        } else {
                            // Unexpected byte during initial preamble
                            pokemon_log_trade_event("ERROR", "Unexpected byte during initial preamble, resetting to IDLE");
                            current_session.state = TRADE_STATE_IDLE;
                            current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
                            response = PKMN_BLANK; // Or a cancel byte
                        }
                        break;

                    case TRADE_SUBSTATE_RANDOM_NUMBERS:
                        // GameBoy sends random numbers, Flipper echoes them.
                        // These are followed by more preamble bytes.
                        current_session.exchange_counter++;
                        char log_msg_rn[64];
                        snprintf(log_msg_rn, sizeof(log_msg_rn), "Random/Preamble2 RX: 0x%02X (%zu/%d)", received_byte, current_session.exchange_counter, SERIAL_RNS_LENGTH + SERIAL_TRADE_BLOCK_PREAMBLE_LENGTH);
                        pokemon_log_trade_event("PROTOCOL_DETAIL", log_msg_rn);

                        if (current_session.exchange_counter >= (SERIAL_RNS_LENGTH + SERIAL_TRADE_BLOCK_PREAMBLE_LENGTH)) {
                            current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE; // Reset sub-state for next main state
                            current_session.exchange_counter = 0;
                            current_session.state = TRADE_STATE_EXCHANGING_BLOCKS; // Transition to actual block exchange
                            // Initialize for block exchange
                            current_session.incoming_pokemon_bytes_count = 0; // Need to add this to trade_session_t
                            memset(&current_session.incoming_trade_block_buffer, 0, sizeof(trade_block_t)); // Need to add this
                            pokemon_log_trade_event("STATE", "CONNECTED -> EXCHANGING_BLOCKS (Preamble/Randoms complete)");
                        }
                        break;

                    // TRADE_SUBSTATE_FINAL_PREAMBLE is merged into RANDOM_NUMBERS count by Flipper logic
                    
                    default:
                        pokemon_log_trade_event("ERROR", "Unknown trade_exchange_sub_state in CONNECTED state");
                        current_session.state = TRADE_STATE_IDLE;
                        current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
                        response = PKMN_BLANK;
                        break;
                }

                // Handle cancellation specifically if it occurs during these sub-states
                if (received_byte == PKMN_MENU_CANCEL_SELECTED) { 
                    response = PKMN_MENU_CANCEL_SELECTED; // Echo
                    current_session.state = TRADE_STATE_IDLE;
                    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
                    pokemon_log_trade_event("STATE", "CONNECTED -> IDLE (Cancel 0xD6 during preamble/random)");
                }
                
                pokemon_send_trade_response(response);
                char connected_log_msg[128]; // Increased buffer size for more detailed logging
                snprintf(connected_log_msg, sizeof(connected_log_msg), "CONNECTED RX:0x%02X->TX:0x%02X (SubState:%d, Cnt:%zu)", 
                        received_byte, response, current_session.trade_exchange_sub_state, current_session.exchange_counter);
                pokemon_log_trade_event("PROTOCOL", connected_log_msg);
                websocket_broadcast_protocol_data(received_byte, response, "CONNECTED");
            }
            break;
            
        case TRADE_STATE_EXCHANGING_BLOCKS: // New state to replace RECEIVING_POKEMON and SENDING_POKEMON byte-by-byte
            if (data_available) {
                uint8_t byte_to_send = PKMN_BLANK;
                
                // Store incoming byte
                if (current_session.incoming_pokemon_bytes_count < sizeof(trade_block_t)) {
                    ((uint8_t*)&current_session.incoming_trade_block_buffer)[current_session.incoming_pokemon_bytes_count] = received_byte;
                }
                
                // Get byte to send from our pre-prepared g_trade_block_to_send
                if (current_session.incoming_pokemon_bytes_count < sizeof(trade_block_t)) {
                    byte_to_send = ((uint8_t*)&g_trade_block_to_send)[current_session.incoming_pokemon_bytes_count];
                } else {
                    // Should not happen if counts are managed correctly
                    byte_to_send = PKMN_BLANK; 
                }

                pokemon_send_trade_response(byte_to_send);
                
                char exchange_log[128];
                snprintf(exchange_log, sizeof(exchange_log), "EXCHANGE RX:0x%02X->TX:0x%02X (Byte %zu/%zu)", 
                        received_byte, byte_to_send, current_session.incoming_pokemon_bytes_count + 1, sizeof(trade_block_t));
                pokemon_log_trade_event("PROTOCOL_DETAIL", exchange_log);
                websocket_broadcast_protocol_data(received_byte, byte_to_send, "EXCHANGING_BLOCKS");

                current_session.incoming_pokemon_bytes_count++;

                if (current_session.incoming_pokemon_bytes_count >= sizeof(trade_block_t)) {
                    pokemon_log_trade_event("INFO", "Full trade block exchanged.");
                    
                    // Parse the received block
                    memcpy(&current_session.incoming_pokemon.core, &current_session.incoming_trade_block_buffer.pokemon_data[0], sizeof(pokemon_core_data_t));
                    
                    // Byte swap received uint16_t fields from Big Endian (network) to Little Endian (RP2040)
                    pokemon_core_data_t* core = &current_session.incoming_pokemon.core;
                    core->current_hp = bswap16(core->current_hp);
                    core->original_trainer_id = bswap16(core->original_trainer_id);
                    core->hp_exp = bswap16(core->hp_exp); // Corrected field name
                    core->attack_exp = bswap16(core->attack_exp); // Corrected field name
                    core->defense_exp = bswap16(core->defense_exp); // Corrected field name
                    core->speed_exp = bswap16(core->speed_exp); // Corrected field name
                    core->special_exp = bswap16(core->special_exp); // Corrected field name
                    core->max_hp = bswap16(core->max_hp);
                    core->attack = bswap16(core->attack); // Corrected field name
                    core->defense = bswap16(core->defense); // Corrected field name
                    core->speed = bswap16(core->speed); // Corrected field name
                    core->special = bswap16(core->special); // Corrected field name
                    // IVs (iv_data[2]) are uint8_t arrays, already in correct byte order if sent correctly by GB.
                    // Experience (experience[3]) is a uint8_t array, also fine.

                    convert_pokemon_name_from_block(current_session.incoming_pokemon.nickname, current_session.incoming_trade_block_buffer.pokemon_nicknames[0], POKEMON_NAME_LENGTH);
                    convert_pokemon_name_from_block(current_session.incoming_pokemon.ot_name, current_session.incoming_trade_block_buffer.original_trainer_names[0], POKEMON_OT_NAME_LENGTH);
                    current_session.has_incoming_data = true;

                    char parsed_msg[128];
                    snprintf(parsed_msg, sizeof(parsed_msg), "Parsed incoming: %s (L%d) from %s", 
                        current_session.incoming_pokemon.nickname, 
                        current_session.incoming_pokemon.core.level, 
                        current_session.incoming_pokemon.ot_name);
                    pokemon_log_trade_event("TRADE", parsed_msg);

                    if (pokemon_validate_data(&current_session.incoming_pokemon)) { // Basic validation
                        pokemon_log_trade_event("VALIDATION", "Incoming Pokemon data appears valid (structurally).");
                        current_session.state = TRADE_STATE_CONFIRMING; 
                        pokemon_log_trade_event("STATE", "EXCHANGING_BLOCKS -> CONFIRMING");
                        // What byte to send here? The Flipper logic moves to patch list or select phase.
                        // After block exchange, the protocol usually moves to selecting which Pokemon (if party > 1)
                        // or directly to confirmation. For a single Pokemon trade, it might be confirmation bytes.
                        // For now, we will assume confirmation follows. The CONFIRMING state handles actual confirm bytes.
                    } else {
                        pokemon_log_trade_event("ERROR", "Incoming Pokemon data failed validation after exchange.");
                        current_session.state = TRADE_STATE_ERROR;
                        strcpy(last_error, "Invalid data in exchanged block");
                    }
                    // Reset for next potential full exchange or different process
                    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
                    current_session.exchange_counter = 0;
                    current_session.incoming_pokemon_bytes_count = 0;
                }
            }
            break;
            
        case TRADE_STATE_CONFIRMING:
            // Enhanced final trade confirmation and completion
            if (data_available) {
                switch (received_byte) {
                    case TRADE_CONFIRM_BYTE: // 0x66 - Confirm trade
                        // Store the received Pokemon
                        if (pokemon_store_received(&current_session.incoming_pokemon, "GAME_BOY")) {
                            current_session.state = TRADE_STATE_COMPLETE;
                            pokemon_log_trade_event("STATE", "CONFIRMING → COMPLETE (store OK)");
                            pokemon_send_trade_response(TRADE_RESPONSE_SUCCESS);
                            
                            char completion_msg[128];
                            snprintf(completion_msg, sizeof(completion_msg), 
                                    "Trade completed! Received %s (Lv.%d) from %s", 
                                    pokemon_get_species_name(current_session.incoming_pokemon.core.species),
                                    current_session.incoming_pokemon.core.level,
                                    current_session.incoming_pokemon.ot_name);
                            pokemon_log_trade_event("TRADE", completion_msg);
                            
                            // Mark the sent Pokemon as traded
                            if (stored_pokemon_count > 1 && pokemon_storage[1].occupied) {
                                char sent_msg[128];
                                snprintf(sent_msg, sizeof(sent_msg), 
                                        "Sent %s (Lv.%d) to partner", 
                                        pokemon_get_species_name(pokemon_storage[1].pokemon.core.species),
                                        pokemon_storage[1].pokemon.core.level);
                                pokemon_log_trade_event("TRADE", sent_msg);
                                
                                // Remove Pokemon #2 from storage
                                pokemon_delete_stored(1);
                            }
                        } else {
                            current_session.state = TRADE_STATE_ERROR;
                            pokemon_log_trade_event("STATE", "CONFIRMING → ERROR (storage full)");
                            strcpy(last_error, "Storage full - cannot complete trade");
                            pokemon_send_trade_response(TRADE_RESPONSE_STORAGE_FULL);
                        }
                        break;
                        
                    case TRADE_CANCEL_BYTE: // 0x77 - Cancel trade
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "CONFIRMING → IDLE (cancel 0x77)");
                        pokemon_send_trade_response(TRADE_CANCEL_BYTE);
                        pokemon_log_trade_event("TRADE", "Trade cancelled by partner during confirmation");
                        break;
                        
                    case 0x00: // Continue/waiting for confirmation
                        pokemon_send_trade_response(0x00);
                        pokemon_log_trade_event("TRADE", "Waiting for final trade confirmation");
                        break;
                        
                    case 0x7C: // Additional confirmation signal
                        pokemon_send_trade_response(0x7C);
                        pokemon_log_trade_event("TRADE", "Trade confirmation acknowledged");
                        break;
                        
                    default:
                        // Echo unknown bytes during confirmation
                        pokemon_send_trade_response(received_byte);
                        char confirm_msg[64];
                        snprintf(confirm_msg, sizeof(confirm_msg), "Confirmation phase: 0x%02X", received_byte);
                        pokemon_log_trade_event("DEBUG", confirm_msg);
                        break;
                }
            }
            break;
            
        case TRADE_STATE_COMPLETE:
            // Trade completed, reset to idle
            current_session.state = TRADE_STATE_IDLE;
            pokemon_log_trade_event("STATE", "COMPLETE → IDLE (trade done)");
            // Reset more fields of current_session
            memset(&current_session.incoming_pokemon, 0, sizeof(pokemon_data_t));
            memset(&current_session.outgoing_pokemon, 0, sizeof(pokemon_data_t));
            current_session.has_incoming_data = false;
            current_session.trade_confirmed = false;
            current_session.partner_name[0] = '\0';
            current_session.needs_internal_reset = false;
            current_session.our_block_sent_this_exchange = false;
            current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
            current_session.exchange_counter = 0;
            // Keep local trainer info and error_count
            pokemon_log_trade_event("TRADE", "Trade completed successfully");
            break;
            
        case TRADE_STATE_ERROR:
            // Error state, reset after logging
            pokemon_log_trade_event("ERROR", last_error);
            current_session.state = TRADE_STATE_IDLE;
            pokemon_log_trade_event("STATE", "ERROR → IDLE (error handled)");
            // Reset relevant fields for a new attempt, keep error_count and local trainer info
            memset(&current_session.incoming_pokemon, 0, sizeof(pokemon_data_t));
            memset(&current_session.outgoing_pokemon, 0, sizeof(pokemon_data_t));
            current_session.has_incoming_data = false;
            current_session.trade_confirmed = false;
            current_session.partner_name[0] = '\0';
            current_session.needs_internal_reset = false;
            current_session.our_block_sent_this_exchange = false;
            current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
            current_session.exchange_counter = 0;
            current_session.error_count++;
            break;
    }
}

void pokemon_trading_reset(void) {
    // Clear all storage slots
    // memset(pokemon_storage, 0, sizeof(pokemon_storage)); // Keep stored pokemon for now
    // stored_pokemon_count = 0;
    
    current_session.state = TRADE_STATE_IDLE;
    pokemon_log_trade_event("STATE", "ANY → IDLE (system reset)");
    // Preserve local_trainer_id, local_trainer_name. Reset other fields.
    uint16_t temp_trainer_id = current_session.local_trainer_id;
    char temp_trainer_name[POKEMON_OT_NAME_LENGTH];
    strncpy(temp_trainer_name, current_session.local_trainer_name, POKEMON_OT_NAME_LENGTH);
    uint8_t temp_error_count = current_session.error_count; // Preserve error count across manual resets

    memset(&current_session, 0, sizeof(current_session));

    current_session.local_trainer_id = temp_trainer_id;
    strncpy(current_session.local_trainer_name, temp_trainer_name, POKEMON_OT_NAME_LENGTH);
    current_session.error_count = temp_error_count;
    current_session.state = TRADE_STATE_IDLE; // Ensure state is IDLE after reset
    current_session.our_block_sent_this_exchange = false;
    current_session.trade_exchange_sub_state = TRADE_SUBSTATE_NONE;
    current_session.exchange_counter = 0;

    pokemon_log_trade_event("SYSTEM", "Pokemon trading system reset");
}

bool pokemon_store_received(const pokemon_data_t* pokemon, const char* source_game) {
    if (stored_pokemon_count >= MAX_STORED_POKEMON) {
        return false;
    }
    
    // Find first empty slot
    for (size_t i = 0; i < MAX_STORED_POKEMON; i++) {
        if (!pokemon_storage[i].occupied) {
            pokemon_storage[i].occupied = true;
            pokemon_storage[i].timestamp = to_us_since_boot(get_absolute_time()) / 1000;
            memcpy(&pokemon_storage[i].pokemon, pokemon, sizeof(pokemon_data_t));
            strncpy(pokemon_storage[i].game_version, source_game, 15);
            pokemon_storage[i].game_version[15] = '\0';
            pokemon_storage[i].checksum = pokemon_calculate_checksum(pokemon);
            
            stored_pokemon_count++;
            
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Stored %s (Lv.%d) in slot %zu", 
                    pokemon_get_species_name(pokemon->core.species), 
                    pokemon->core.level, i);
            pokemon_log_trade_event("STORAGE", log_msg);
            
            return true;
        }
    }
    
    return false;
}

pokemon_slot_t* pokemon_get_stored_list(void) {
    return pokemon_storage;
}

size_t pokemon_get_stored_count(void) {
    return stored_pokemon_count;
}

bool pokemon_delete_stored(size_t index) {
    if (index >= MAX_STORED_POKEMON || !pokemon_storage[index].occupied) {
        return false;
    }
    
    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "Deleted %s from slot %zu", 
            pokemon_get_species_name(pokemon_storage[index].pokemon.core.species), 
            index);
    
    memset(&pokemon_storage[index], 0, sizeof(pokemon_slot_t));
    stored_pokemon_count--;
    
    pokemon_log_trade_event("STORAGE", log_msg);
    return true;
}

bool pokemon_send_stored(size_t index) {
    // This function will now use the globally prepared g_trade_block_to_send
    // The 'index' parameter is currently unused as we always send the Pokemon in g_trade_block_to_send.
    // We might want to adapt this if we allow selecting from multiple prepared blocks.

    if (g_trade_block_to_send.pokemon_data[0].species == 0) { // Check if a Pokemon is actually prepared
        snprintf(last_error, sizeof(last_error), "No Pokemon prepared in g_trade_block_to_send.");
        pokemon_log_trade_event("ERROR", last_error);
        return false;
    }

    // Populate current_session.outgoing_pokemon from g_trade_block_to_send for local tracking/logging
    // This is distinct from the full trade_block_t that is sent over the wire.
    // The outgoing_pokemon struct is a pokemon_data_t, which holds core data + C-string names.
    
    // Copy core data directly
    memcpy(&current_session.outgoing_pokemon.core, &g_trade_block_to_send.pokemon_data[0], sizeof(pokemon_core_data_t));

    // Convert and copy nickname (from trade_block_t.pokemon_nicknames[0] to pokemon_data_t.nickname)
    convert_pokemon_name_from_block(current_session.outgoing_pokemon.nickname, g_trade_block_to_send.pokemon_nicknames[0], POKEMON_NAME_LENGTH);

    // Convert and copy OT name (from trade_block_t.original_trainer_names[0] to pokemon_data_t.ot_name)
    // Note: The trade block uses player_trainer_name for the sender's overall name,
    // and original_trainer_names[0] for the OT of the specific Pokemon being traded.
    // We should use original_trainer_names[0] for the Pokemon's OT name.
    convert_pokemon_name_from_block(current_session.outgoing_pokemon.ot_name, g_trade_block_to_send.original_trainer_names[0], POKEMON_OT_NAME_LENGTH);
    
    // Ensure our trainer ID is set in the outgoing pokemon's core data
    // (g_trade_block_to_send should already have this from its creation)
    current_session.outgoing_pokemon.core.original_trainer_id = g_trade_block_to_send.pokemon_data[0].original_trainer_id;


    char send_log[128];
    snprintf(send_log, sizeof(send_log), "Preparing to send Pokemon: %s (Species: %d) from OT: %s",
             current_session.outgoing_pokemon.nickname,
             current_session.outgoing_pokemon.core.species,
             current_session.outgoing_pokemon.ot_name);
    pokemon_log_trade_event("TRADE_PREP", send_log);

    // Send the entire trade block. The linkcable_send_trade_block handles byte swapping.
    linkcable_send_trade_block(&g_trade_block_to_send);

    current_session.our_block_sent_this_exchange = true;
    current_session.state = TRADE_STATE_CONFIRMING; 
    // current_session.needs_internal_reset = true; // Not needed for confirming state typically

    char state_log[128];
    snprintf(state_log, sizeof(state_log), "Sent our trade block. Transitioning to CONFIRMING. Nick: %.7s, Species: %d",
        g_trade_block_to_send.pokemon_nicknames[0], g_trade_block_to_send.pokemon_data[0].species);
    pokemon_log_trade_event("STATE_TRANSITION", state_log);
    
    // Log the species ID that we are offering for simpler debugging
    // current_session.offered_pokemon_species = g_trade_block_to_send.pokemon_data[0].species;
    // strncpy(current_session.offered_pokemon_name, g_trade_block_to_send.pokemon_nicknames[0], POKEMON_NAME_LENGTH -1);
    // Ensure null termination for offered_pokemon_name as it's used in logs
    // char temp_offered_name[POKEMON_NAME_LENGTH];
    // convert_pokemon_name_from_block(temp_offered_name, g_trade_block_to_send.pokemon_nicknames[0], POKEMON_NAME_LENGTH);
    // strncpy(current_session.offered_pokemon_name, temp_offered_name, sizeof(current_session.offered_pokemon_name) -1);
    // current_session.offered_pokemon_name[sizeof(current_session.offered_pokemon_name) -1] = '\0';


    return true;
}

void pokemon_send_trade_response(uint8_t response_code) {
    linkcable_send(response_code);
}

trade_state_t pokemon_get_trade_state(void) {
    return current_session.state;
}

const char* pokemon_get_last_error(void) {
    return last_error;
}

trade_session_t* pokemon_get_current_session(void) {
    return &current_session;
}

void pokemon_log_trade_event(const char* event, const char* details) {
    char timestamp[32];
    uint32_t time_ms = to_us_since_boot(get_absolute_time()) / 1000;
    snprintf(timestamp, sizeof(timestamp), "[%lu.%03lu]", 
             time_ms / 1000, time_ms % 1000);
    
    char log_entry[256];
    int len = snprintf(log_entry, sizeof(log_entry), "%s %s: %s\n", 
                      timestamp, event, details);
    
    // Append to trade log buffer
    if (log_position + len >= sizeof(trade_log)) {
        log_position = 0;
    }
    
    memcpy(trade_log + log_position, log_entry, len);
    log_position += len;
    trade_log[log_position] = '\0';
}

const char* pokemon_get_trade_log(void) {
    return trade_log;
} 