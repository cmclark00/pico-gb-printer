#include "pokemon_trading.h"
#include "pokemon_data.h"
#include "linkcable.h"
#include "websocket_server.h"
#include "pico/time.h"
#include "hardware/gpio.h"
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

// Error tracking
static char last_error[128];

// Function to create a test Pokemon for trading
static bool pokemon_create_test_pokemon(pokemon_data_t* pokemon, uint8_t species, uint8_t level, const char* nickname, const char* trainer_name) {
    if (!pokemon) return false;
    
    memset(pokemon, 0, sizeof(pokemon_data_t));
    
    pokemon->species = species;
    pokemon->level = level;
    pokemon->current_hp = level * 2 + 50; // Simple HP calculation
    pokemon->status = 0; // No status conditions
    
    // Set types based on species (simplified)
    switch (species) {
        case 0x01: // Bulbasaur
            pokemon->type1 = 12; // Grass
            pokemon->type2 = 3;  // Poison
            break;
        case 0x04: // Charmander
            pokemon->type1 = 20; // Fire
            pokemon->type2 = 20; // Fire
            break;
        case 0x07: // Squirtle
            pokemon->type1 = 21; // Water
            pokemon->type2 = 21; // Water
            break;
        case 0x19: // Pikachu
            pokemon->type1 = 13; // Electric
            pokemon->type2 = 13; // Electric
            break;
        default:
            pokemon->type1 = 0; // Normal
            pokemon->type2 = 0; // Normal
            break;
    }
    
    pokemon->catch_rate = 45;
    pokemon->moves[0] = 1; // Pound
    pokemon->moves[1] = 0; // No move
    pokemon->moves[2] = 0; // No move
    pokemon->moves[3] = 0; // No move
    
    pokemon->original_trainer_id = 12345;
    
    // Set experience as 3-byte array (little endian)
    uint32_t exp = (level * level * level); // Simple exp calculation
    pokemon->experience[0] = exp & 0xFF;
    pokemon->experience[1] = (exp >> 8) & 0xFF;
    pokemon->experience[2] = (exp >> 16) & 0xFF;
    
    // Set stat experience (EVs)
    pokemon->hp_exp = 1000;
    pokemon->attack_exp = 1000;
    pokemon->defense_exp = 1000;
    pokemon->speed_exp = 1000;
    pokemon->special_exp = 1000;
    
    // Set IVs (simplified)
    pokemon->iv_data[0] = 0xAA;
    pokemon->iv_data[1] = 0xAA;
    
    // Set PP
    pokemon->move_pp[0] = 35;
    pokemon->move_pp[1] = 0;
    pokemon->move_pp[2] = 0;
    pokemon->move_pp[3] = 0;
    
    // Set duplicate level (required by Gen 1 format)
    pokemon->level_copy = level;
    
    // Calculate and set stats (simple formulas)
    pokemon->max_hp = pokemon->current_hp;
    pokemon->attack = level + 20;
    pokemon->defense = level + 15;
    pokemon->speed = level + 10;
    pokemon->special = level + 25;
    
    // Set nickname and trainer name
    strncpy(pokemon->nickname, nickname, POKEMON_NAME_LENGTH - 1);
    pokemon->nickname[POKEMON_NAME_LENGTH - 1] = '\0';
    strncpy(pokemon->ot_name, trainer_name, POKEMON_OT_NAME_LENGTH - 1);
    pokemon->ot_name[POKEMON_OT_NAME_LENGTH - 1] = '\0';
    
    return true;
}

void pokemon_trading_init(void) {
    // Clear all storage slots
    memset(pokemon_storage, 0, sizeof(pokemon_storage));
    stored_pokemon_count = 0;
    
    // Initialize trading session
    memset(&current_session, 0, sizeof(current_session));
    current_session.state = TRADE_STATE_IDLE;
    
    // Clear logs and errors
    memset(trade_log, 0, sizeof(trade_log));
    memset(last_error, 0, sizeof(last_error));
    log_position = 0;
    
    pokemon_log_trade_event("SYSTEM", "Pokemon trading system initialized");
    
    // Add some test Pokemon for trading
    pokemon_data_t test_pokemon;
    
    // Add a Pikachu
    if (pokemon_create_test_pokemon(&test_pokemon, 0x19, 25, "PIKACHU", "ASH")) {
        pokemon_store_received(&test_pokemon, "TEST_DATA");
    }
    
    // Add a Charmander  
    if (pokemon_create_test_pokemon(&test_pokemon, 0x04, 15, "CHARMANDER", "RED")) {
        pokemon_store_received(&test_pokemon, "TEST_DATA");
    }
    
    // Add a Squirtle
    if (pokemon_create_test_pokemon(&test_pokemon, 0x07, 20, "SQUIRTLE", "BLUE")) {
        pokemon_store_received(&test_pokemon, "TEST_DATA");
    }
    
    pokemon_log_trade_event("SYSTEM", "Added test Pokemon for trading");
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
                uint8_t response = 0x00; // Default response
                static int save_sequence_count = 0;
                
                // Pokemon trading protocol responses
                switch (received_byte) {
                    case 0x01: // Pokemon sync/handshake
                        response = 0x01; // Sync back
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        pokemon_log_trade_event("STATE", "IDLE → WAITING_PARTNER (sync/handshake)");
                        save_sequence_count = 0;
                        break;
                    case 0x02: // Pokemon trade request
                        response = 0x02; // Accept trade
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "IDLE → CONNECTED (trade request)");
                        save_sequence_count = 0;
                        break;
                    case 0x03: // Save game / status check (Cable Club entry)
                        response = 0x00; // Acknowledge save
                        save_sequence_count++;
                        if (save_sequence_count >= 2) {
                            current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                            pokemon_log_trade_event("STATE", "IDLE → WAITING_PARTNER (save ack)");
                            pokemon_log_trade_event("SAVE", "Game save acknowledged, entering Cable Club");
                        }
                        break;
                    case 0x06: // Menu confirmation / Enter room confirmation
                        response = 0x06; // Echo confirmation
                        pokemon_log_trade_event("MENU", "Room entry confirmed");
                        save_sequence_count = 0;
                        break;
                    case 0x00: // Continue/padding after save
                        response = 0x00; // Continue
                        if (save_sequence_count > 0) { // Only process as part of save sequence if count > 0
                            save_sequence_count++;
                            if (save_sequence_count >= 3) {
                                pokemon_log_trade_event("SAVE", "Save sequence completed");
                                save_sequence_count = 0; // Reset counter
                            }
                            // No "Continuing after room entry" log here, as it's part of the problematic loop for initial 0x00s.
                        }
                        // If save_sequence_count was 0, it remains 0. RX 0x00 results in TX 0x00 without side effects on save_sequence_count.
                        break;
                    case 0x1C: // Menu navigation
                        response = 0x1C; // Echo navigation
                        pokemon_log_trade_event("MENU", "Menu navigation in Cable Club");
                        save_sequence_count = 0;
                        break;
                    case 0x60: // Trade Center selection
                        response = 0x60; // Echo selection
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        pokemon_log_trade_event("STATE", "IDLE → WAITING_PARTNER (Trade Center)");
                        pokemon_log_trade_event("MENU", "Trade Center selected");
                        save_sequence_count = 0;
                        break;
                    case 0x61: // Colosseum selection
                        response = 0x61; // Echo selection
                        pokemon_log_trade_event("MENU", "Colosseum selected");
                        save_sequence_count = 0;
                        break;
                    case 0x62: // Trade request
                        response = 0x62;
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "IDLE → CONNECTED (trade request 0x62)");
                        pokemon_log_trade_event("TRADE", "Trade request accepted");
                        break;
                    case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69:
                    case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E:
                        // Menu selection options (0x63-0x6E from possible_indexes range)
                        response = received_byte; // Echo selection
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        char sel_msg_buf[128]; // Buffer for state log
                        snprintf(sel_msg_buf, sizeof(sel_msg_buf), "IDLE → WAITING_PARTNER (menu sel 0x%02X)", received_byte);
                        pokemon_log_trade_event("STATE", sel_msg_buf);
                        snprintf(sel_msg_buf, sizeof(sel_msg_buf), "Partner selected Pokemon #%d, offering our Pokemon #2 - transitioning to connected", received_byte);
                        pokemon_log_trade_event("SELECTION", sel_msg_buf);
                        char menu_msg_buf[128]; // Buffer for state log
                        snprintf(menu_msg_buf, sizeof(menu_msg_buf), "Menu selection 0x%02X", received_byte);
                        pokemon_log_trade_event("MENU", menu_msg_buf);
                        save_sequence_count = 0;
                        break;
                    case 0x6F: // Stop/cancel trade
                        response = 0x6F; // Echo stop
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "ANY → IDLE (stop/cancel 0x6F)");
                        pokemon_log_trade_event("TRADE", "Trade stopped/cancelled by partner");
                        save_sequence_count = 0;
                        break;
                    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4:
                        // Room states from enter_room_states specification
                        response = received_byte; // Echo room state
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        char room_msg_buf[128]; // Buffer for state log
                        snprintf(room_msg_buf, sizeof(room_msg_buf), "IDLE → WAITING_PARTNER (room state 0x%02X)", received_byte);
                        pokemon_log_trade_event("STATE", room_msg_buf);
                        snprintf(room_msg_buf, sizeof(room_msg_buf), "Room state 0x%02X", received_byte);
                        pokemon_log_trade_event("ROOM", room_msg_buf);
                        save_sequence_count = 0;
                        break;
                    case 0xFE: // No input signal
                        response = 0x00; // Continue/ready
                        pokemon_log_trade_event("PROTOCOL", "No input signal (0xFE) - sending continue");
                        save_sequence_count = 0;
                        break;
                    case 0x70: // Trading protocol
                        response = 0x70; // Echo
                        pokemon_log_trade_event("TRADE", "Trading protocol start");
                        save_sequence_count = 0;
                        break;
                    case 0x71: // Trading handshake
                        response = 0x71; // Echo
                        pokemon_log_trade_event("TRADE", "Trading handshake");
                        save_sequence_count = 0;
                        break;
                    case 0x72: // Trading ready
                        response = 0x72; // Echo
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("TRADE", "Ready to trade");
                        save_sequence_count = 0;
                        break;
                    case 0xC0: // Enter trading room / Both players ready
                        response = 0xC0; // Echo confirmation
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("TRADE", "Entering trading room - both players ready!");
                        save_sequence_count = 0;
                        break;
                    case 0xFD: // Preamble byte
                        response = 0xFD; // Echo preamble
                        current_session.state = TRADE_STATE_RECEIVING_POKEMON;
                        pokemon_log_trade_event("STATE", "IDLE → RX_POKEMON (preamble 0xFD)");
                        save_sequence_count = 0;
                        break;
                    case 0x04: // Pokemon selection (slot 4) - transition to connected
                        response = 0x02; // We offer Pokemon #2 in response
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "IDLE → CONNECTED (partner sel 0x04)");
                        pokemon_log_trade_event("SELECTION", "Partner selected Pokemon #4, offering our Pokemon #2 - transitioning to connected state");
                        save_sequence_count = 0;
                        break;
                    case 0x05: // Pokemon selection (slot 5) - transition to connected  
                        response = 0x02; // We offer Pokemon #2 in response
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "IDLE → CONNECTED (partner sel 0x05)");
                        pokemon_log_trade_event("SELECTION", "Partner selected Pokemon #5, offering our Pokemon #2 - transitioning to connected state");
                        save_sequence_count = 0;
                        break;
                    case 0x07: // Pokemon selection continuation or confirm
                        response = 0x00; // Continue/ready
                        pokemon_log_trade_event("SELECTION", "Selection continuation - sending continue");
                        save_sequence_count = 0;
                        break;
                    case 0x08: // Cancel/back in Pokemon selection - but continue offering trade
                        response = 0x08; // Echo back the cancel
                        // Stay in TRADE_STATE_IDLE to continue the selection process
                        pokemon_log_trade_event("SELECTION", "Selection cancel/back - echoing back");
                        save_sequence_count = 0;
                        break;
                    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
                        // Other selection/menu bytes - keep offering our Pokemon
                        response = 0x00; // Continue/ready
                        char selection_msg[64];
                        snprintf(selection_msg, sizeof(selection_msg), "Menu byte 0x%02X - sending continue", received_byte);
                        pokemon_log_trade_event("SELECTION", selection_msg);
                        save_sequence_count = 0;
                        break;
                    default:
                        response = 0x00; // Safe default
                        // Log unknown bytes for debugging - but handle some common ones
                        if (received_byte >= 0x80 && received_byte <= 0xFF) {
                            // High bytes often indicate advanced trading protocol
                            response = received_byte; // Echo back
                            char advanced_msg[64];
                            snprintf(advanced_msg, sizeof(advanced_msg), "Advanced protocol: 0x%02X", received_byte);
                            pokemon_log_trade_event("PROTOCOL", advanced_msg);
                        } else if (received_byte >= 0x01 && received_byte <= 0x06) {
                            // Pokemon selection commands - always trade our first Pokemon
                            response = 0x02; // We always offer Pokemon #2
                            current_session.state = TRADE_STATE_CONNECTED;
                            char sel_msg_buf[128]; // Buffer for state log
                            snprintf(sel_msg_buf, sizeof(sel_msg_buf), "IDLE → CONNECTED (partner sel 0x%02X default)", received_byte);
                            pokemon_log_trade_event("STATE", sel_msg_buf);
                            snprintf(sel_msg_buf, sizeof(sel_msg_buf), "Partner selected Pokemon #%d, offering our Pokemon #2 - transitioning to connected", received_byte);
                            pokemon_log_trade_event("SELECTION", sel_msg_buf);
                            save_sequence_count = 0;
                        } else {
                            char unknown_msg[64];
                            snprintf(unknown_msg, sizeof(unknown_msg), "Unknown byte in IDLE: 0x%02X", received_byte);
                            pokemon_log_trade_event("DEBUG", unknown_msg);
                        }
                        break;
                }
                
                current_session.session_start_time = to_us_since_boot(get_absolute_time()) / 1000;
                pokemon_send_trade_response(response);
                
                char msg[64];
                snprintf(msg, sizeof(msg), "RX: 0x%02X → TX: 0x%02X (save_seq: %d)", 
                        received_byte, response, save_sequence_count);
                pokemon_log_trade_event("PROTOCOL", msg);
                
                // Broadcast real-time WebSocket update
                websocket_broadcast_protocol_data(received_byte, response, "IDLE");
            }
            break;
            
        case TRADE_STATE_WAITING_FOR_PARTNER:
            // Continue Pokemon trading handshake after save
            if (data_available) {
                uint8_t response = 0x00; // Default response
                static int partner_wait_count = 0;
                
                // Handle continued trading protocol
                switch (received_byte) {
                    case 0x01: // Continue sync
                        response = 0x01; 
                        partner_wait_count++;
                        if (partner_wait_count > 3) {
                            current_session.state = TRADE_STATE_CONNECTED;
                            pokemon_log_trade_event("STATE", "WAITING_PARTNER → CONNECTED (sync >3)");
                            pokemon_log_trade_event("CABLE", "Partner detected, ready to trade");
                        }
                        break;
                    case 0x06: // Room confirmation
                        response = 0x06; // Echo confirmation
                        pokemon_log_trade_event("MENU", "Room entry confirmed (waiting for partner)");
                        break;
                    case 0x02: // Trade request
                        response = 0x02;
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → CONNECTED (trade req)");
                        pokemon_log_trade_event("TRADE", "Trade request accepted");
                        break;
                    case 0x03: // Status check (partner detection)
                        response = 0x00; // Ready
                        partner_wait_count++;
                        break;
                    case 0x00: // Continue/padding
                        response = 0x00;
                        partner_wait_count++;
                        // Simulate finding a trading partner after enough exchanges
                        if (partner_wait_count > 5) { 
                            // Send a different response to indicate partner found
                            response = 0x01;
                            current_session.state = TRADE_STATE_CONNECTED; // Transition to connected
                            pokemon_log_trade_event("STATE", "WAITING_PARTNER → CONNECTED (sim partner)");
                            pokemon_log_trade_event("CABLE", "Simulating partner detection, transitioning to connected");
                        }
                        break;
                    case 0x1C: // Menu navigation/selection 
                        response = 0x1C; // Echo selection
                        pokemon_log_trade_event("MENU", "Menu navigation");
                        break;
                    case 0x60: // Cable Club menu selection (Trade Center)
                        response = 0x60; // Echo menu selection
                        pokemon_log_trade_event("MENU", "Trade Center selected");
                        break;
                    case 0x61: // Cable Club menu selection (Colosseum)
                        response = 0x61; // Echo menu selection
                        pokemon_log_trade_event("MENU", "Colosseum selected");
                        break;
                    case 0x62: // Additional menu option or Pokemon selection continuation
                        response = 0x62; // Echo selection
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → WAITING_PARTNER (menu 0x62)");
                        pokemon_log_trade_event("MENU", "Menu option 0x62 selected - entering partner wait");
                        break;
                    case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69:
                    case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E:
                        // Menu selection options (0x63-0x6E from possible_indexes range)
                        response = received_byte; // Echo selection
                        current_session.state = TRADE_STATE_WAITING_FOR_PARTNER;
                        char partner_menu_msg_buf[128];
                        snprintf(partner_menu_msg_buf, sizeof(partner_menu_msg_buf), "WAITING_PARTNER → WAITING_PARTNER (partner menu 0x%02X)", received_byte);
                        pokemon_log_trade_event("STATE", partner_menu_msg_buf);
                        snprintf(partner_menu_msg_buf, sizeof(partner_menu_msg_buf), "Partner menu selection 0x%02X", received_byte);
                        pokemon_log_trade_event("MENU", partner_menu_msg_buf);
                        break;
                    case 0x6F: // Stop/cancel trade
                        response = 0x6F; // Echo stop
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → IDLE (stop/cancel 0x6F)");
                        pokemon_log_trade_event("TRADE", "Trade stopped/cancelled by partner (0x6F)");
                        break;
                    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4:
                        // Room states from enter_room_states specification
                        response = received_byte; // Echo room state
                        char partner_room_msg[64];
                        snprintf(partner_room_msg, sizeof(partner_room_msg), "Partner room state 0x%02X", received_byte);
                        pokemon_log_trade_event("ROOM", partner_room_msg);
                        break;
                    case 0xFE: // No input signal
                        response = 0x00; // Continue/ready
                        pokemon_log_trade_event("PROTOCOL", "No input signal (0xFE) in partner wait - sending continue");
                        break;
                    case 0x70: // Trading protocol start
                        response = 0x70; // Acknowledge
                        pokemon_log_trade_event("TRADE", "Trading protocol initiated");
                        break;
                    case 0x71: // Trading handshake
                        response = 0x71; // Echo handshake
                        pokemon_log_trade_event("TRADE", "Trading handshake");
                        break;
                    case 0x72: // Trading ready
                        response = 0x72; // Ready
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → CONNECTED (ready 0x72)");
                        pokemon_log_trade_event("TRADE", "Both sides ready to trade");
                        break;
                    case 0x7F: // Trading cancel/abort
                        response = 0x7F; // Acknowledge cancel
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → IDLE (cancel 0x7F)");
                        pokemon_log_trade_event("TRADE", "Trading cancelled");
                        break;
                    case 0xC0: // Enter trading room / Both players ready
                        response = 0xC0; // Echo confirmation
                        current_session.state = TRADE_STATE_CONNECTED;
                        pokemon_log_trade_event("STATE", "WAITING_PARTNER → CONNECTED (room 0xC0)");
                        pokemon_log_trade_event("TRADE", "Trading room entered - both players ready!");
                        break;
                    case 0xFD: // Start data transmission
                        response = 0xFD;
                        current_session.state = TRADE_STATE_RECEIVING_POKEMON;
                        break;
                    default:
                        response = 0x00;
                        // Log unknown bytes for debugging
                        char unknown_msg[64];
                        snprintf(unknown_msg, sizeof(unknown_msg), "Unknown byte: 0x%02X", received_byte);
                        pokemon_log_trade_event("DEBUG", unknown_msg);
                        break;
                }
                
                pokemon_send_trade_response(response);
                
                char msg[64];
                snprintf(msg, sizeof(msg), "RX: 0x%02X → TX: 0x%02X (wait: %d)", 
                        received_byte, response, partner_wait_count);
                pokemon_log_trade_event("PARTNER", msg);
            }
            break;
            
        case TRADE_STATE_CONNECTED:
            // Enhanced Pokemon selection and trading preparation
            if (data_available) {
                uint8_t response = 0x00;
                
                switch (received_byte) {
                    case 0x00: // Continue/ready
                        response = 0x00;
                        pokemon_log_trade_event("TRADE", "Both sides ready for Pokemon selection");
                        break;
                    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
                        // Pokemon party selection (1-6)
                        response = 0x02; // We'll always offer Pokemon #2 from our storage
                        char selection_msg[64];
                        snprintf(selection_msg, sizeof(selection_msg), "Partner selected Pokemon #%d, offering our Pokemon #2", received_byte);
                        pokemon_log_trade_event("SELECTION", selection_msg);
                        break;
                    case 0x62: // Accept trade command - proceed to data exchange
                        response = 0x62; // Echo acceptance
                        current_session.state = TRADE_STATE_RECEIVING_POKEMON;
                        pokemon_log_trade_event("STATE", "CONNECTED → RX_POKEMON (accept 0x62)");
                        pokemon_log_trade_event("TRADE", "Trade accepted (0x62) in connected state - starting data exchange");
                        websocket_broadcast_trade_event("TRADE", "Trade accepted - starting data exchange");
                        break;
                    case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69:
                    case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E:
                        // Menu selection options during connected state
                        response = received_byte; // Echo selection
                        char connected_menu_msg[64];
                        snprintf(connected_menu_msg, sizeof(connected_menu_msg), "Connected menu selection 0x%02X", received_byte);
                        pokemon_log_trade_event("SELECTION", connected_menu_msg);
                        break;
                    case 0x6F: // Stop/cancel trade
                        response = 0x6F; // Echo stop
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "CONNECTED → IDLE (stop/cancel 0x6F)");
                        pokemon_log_trade_event("TRADE", "Trade stopped/cancelled by partner (0x6F) in connected state");
                        websocket_broadcast_trade_event("TRADE", "Trade cancelled by partner");
                        break;
                    case 0xFE: // No input signal
                        response = 0x00; // Continue/ready
                        pokemon_log_trade_event("PROTOCOL", "No input signal (0xFE) in connected state - sending continue");
                        break;
                    case 0x7C: // Trade ready confirmation
                        response = 0x7C;
                        current_session.state = TRADE_STATE_RECEIVING_POKEMON;
                        pokemon_log_trade_event("STATE", "CONNECTED → RX_POKEMON (confirm 0x7C)");
                        pokemon_log_trade_event("TRADE", "Trade confirmation exchanged, starting data transfer");
                        websocket_broadcast_trade_event("TRADE", "Trade confirmation exchanged, starting data transfer");
                        break;
                    case 0x7F: // Cancel trade
                        response = 0x7F;
                        current_session.state = TRADE_STATE_IDLE;
                        pokemon_log_trade_event("STATE", "CONNECTED → IDLE (cancel 0x7F)");
                        pokemon_log_trade_event("TRADE", "Trade cancelled by partner");
                        websocket_broadcast_trade_event("TRADE", "Trade cancelled by partner");
                        break;
                    case 0xFD: // Start Pokemon data transmission
                        response = 0xFD;
                        current_session.state = TRADE_STATE_RECEIVING_POKEMON;
                        pokemon_log_trade_event("STATE", "CONNECTED → RX_POKEMON (preamble 0xFD)");
                        pokemon_log_trade_event("TRADE", "Pokemon data transmission starting");
                        websocket_broadcast_trade_event("TRADE", "Pokemon data transmission starting");
                        break;
                    default:
                        char debug_msg[128]; // Increased buffer size
                        if (received_byte == 0x91) { // Seen from P2 (GameBoy)
                            response = 0x93; // Respond as P1
                            snprintf(debug_msg, sizeof(debug_msg), "Connected state RX: 0x%02X → TX: 0x%02X (Map Header Swap)", received_byte, response);
                        } else if (received_byte == 0x93) { // Seen from P1 (GameBoy)
                            response = 0x91; // Respond as P2
                            snprintf(debug_msg, sizeof(debug_msg), "Connected state RX: 0x%02X → TX: 0x%02X (Map Header Swap)", received_byte, response);
                        } else if (received_byte >= 0x80 && received_byte < 0x90) { // Likely map data bytes
                            response = 0x00; // Send ACK/continue instead of echo
                            snprintf(debug_msg, sizeof(debug_msg), "Connected state RX: 0x%02X → TX: 0x%02X (Map Data Ack)", received_byte, response);
                        } else {
                            response = received_byte; // Echo other unknown bytes
                            snprintf(debug_msg, sizeof(debug_msg), "Connected state RX: 0x%02X → TX: 0x%02X (Default Echo)", received_byte, response);
                        }
                        pokemon_log_trade_event("DEBUG", debug_msg);
                        break;
                }
                
                pokemon_send_trade_response(response);
            }
            break;
            
        case TRADE_STATE_RECEIVING_POKEMON:
            // Static variables for the receiving process
            static uint8_t imidazole_receive_buffer[POKEMON_DATA_SIZE + POKEMON_NAME_LENGTH + POKEMON_OT_NAME_LENGTH];
            static size_t imidazole_received_bytes = 0;
            static bool imidazole_receiving_pokemon_data = false;
            static bool imidazole_receiving_nickname = false;
            static bool imidazole_receiving_ot_name = false;
            
            if (data_available) {
                char xfer_log_buffer[128];
                uint8_t byte_to_transmit = received_byte; // Default: echo received byte as ack

                // Initialize receiving sequence if not already started
                if (!imidazole_receiving_pokemon_data && !imidazole_receiving_nickname && !imidazole_receiving_ot_name) {
                    if (received_byte == 0xFD) { // Preamble for Pokemon data
                        pokemon_log_trade_event("RECV_INIT", "Preamble 0xFD received, starting Pokemon reception.");
                        imidazole_receiving_pokemon_data = true;
                        imidazole_received_bytes = 0;
                        // byte_to_transmit = 0xFD; // Echo preamble - already default
                    } else {
                        // If not a preamble, it might be an error or unexpected byte.
                        // For now, we just echo and log.
                        snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "RECV: RX:0x%02X -> TX:0x%02X (Unexpected initial byte, echoing)", received_byte, byte_to_transmit);
                        pokemon_log_trade_event("DEBUG", xfer_log_buffer);
                        pokemon_send_trade_response(byte_to_transmit);
                        return; // Or handle error state
                    }
                }

                if (imidazole_receiving_pokemon_data) {
                    imidazole_receive_buffer[imidazole_received_bytes++] = received_byte;
                    snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "RECV: RX:0x%02X -> TX:0x%02X (Core byte %zu/%d)", received_byte, byte_to_transmit, imidazole_received_bytes, POKEMON_DATA_SIZE);
                    
                    if (imidazole_received_bytes >= POKEMON_DATA_SIZE) {
                        pokemon_log_trade_event("RECV_CORE_DONE", "Incoming core data received.");
                        imidazole_receiving_pokemon_data = false;
                        imidazole_receiving_nickname = true;
                        imidazole_received_bytes = 0;
                    }
                } else if (imidazole_receiving_nickname) {
                    imidazole_receive_buffer[POKEMON_DATA_SIZE + imidazole_received_bytes++] = received_byte;
                    snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "RECV: RX:0x%02X -> TX:0x%02X (Nickname byte %zu/%d)", received_byte, byte_to_transmit, imidazole_received_bytes, POKEMON_NAME_LENGTH);

                    if (imidazole_received_bytes >= POKEMON_NAME_LENGTH) {
                        pokemon_log_trade_event("RECV_NICK_DONE", "Incoming nickname received.");
                        imidazole_receiving_nickname = false;
                        imidazole_receiving_ot_name = true;
                        imidazole_received_bytes = 0;
                    }
                } else if (imidazole_receiving_ot_name) {
                    imidazole_receive_buffer[POKEMON_DATA_SIZE + POKEMON_NAME_LENGTH + imidazole_received_bytes++] = received_byte;
                    snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "RECV: RX:0x%02X -> TX:0x%02X (OT Name byte %zu/%d)", received_byte, byte_to_transmit, imidazole_received_bytes, POKEMON_OT_NAME_LENGTH);

                    if (imidazole_received_bytes >= POKEMON_OT_NAME_LENGTH) {
                        pokemon_log_trade_event("RECV_OT_DONE", "Incoming OT name received.");
                        memcpy(&current_session.incoming_pokemon, imidazole_receive_buffer, POKEMON_DATA_SIZE);
                        memcpy(current_session.incoming_pokemon.nickname, imidazole_receive_buffer + POKEMON_DATA_SIZE, POKEMON_NAME_LENGTH);
                        current_session.incoming_pokemon.nickname[POKEMON_NAME_LENGTH-1] = '\0'; // Ensure null termination
                        memcpy(current_session.incoming_pokemon.ot_name, imidazole_receive_buffer + POKEMON_DATA_SIZE + POKEMON_NAME_LENGTH, POKEMON_OT_NAME_LENGTH);
                        current_session.incoming_pokemon.ot_name[POKEMON_OT_NAME_LENGTH-1] = '\0'; // Ensure null termination
                        
                        current_session.has_incoming_data = true;
                        imidazole_receiving_ot_name = false;
                        imidazole_received_bytes = 0; // Reset for next potential reception
                        
                        char trade_msg[128];
                        snprintf(trade_msg, sizeof(trade_msg), "Complete Pokemon received: %s (Lv.%d) from %s", 
                                pokemon_get_species_name(current_session.incoming_pokemon.species),
                                current_session.incoming_pokemon.level,
                                current_session.incoming_pokemon.ot_name);
                        pokemon_log_trade_event("TRADE", trade_msg);

                        if (pokemon_validate_data(&current_session.incoming_pokemon)) {
                            pokemon_log_trade_event("VALIDATION", "Incoming Pokemon data is valid.");
                            // Successfully received a Pokemon. Now it might be our turn to send,
                            // or go to confirmation if we've already sent ours.
                            // For a simple sequential trade: GB sends -> Pico sends -> Confirm
                            // We'll transition to SENDING_POKEMON if we have one prepared by pokemon_send_stored.
                            if (current_session.outgoing_pokemon.species != 0) { // Check if there's an outgoing Pokemon
                                current_session.state = TRADE_STATE_SENDING_POKEMON;
                                current_session.needs_internal_reset = true; // Initialize sender
                                pokemon_log_trade_event("STATE", "RX_POKEMON -> SENDING_POKEMON (Got theirs, now send ours)");
                                // The byte_to_transmit here is the ack for the last byte of THEIR Pokemon.
                                // The SENDING_POKEMON state will handle sending the first byte of OUR Pokemon.
                            } else {
                                // No Pokemon queued to send from our side, this might be an issue
                                // or we are in a different trade mode (e.g. receive only).
                                // For now, go to confirm as if we didn't need to send.
                                pokemon_log_trade_event("WARNING", "No outgoing Pokemon, proceeding to confirm.");
                                current_session.state = TRADE_STATE_CONFIRMING;
                                pokemon_log_trade_event("STATE", "RX_POKEMON -> CONFIRMING (No outgoing Pokemon)");
                            }
                        } else {
                            pokemon_log_trade_event("VALIDATION", "Incoming Pokemon data INVALID.");
                            current_session.state = TRADE_STATE_ERROR;
                            strcpy(last_error, "Invalid Pokemon data received from partner");
                            pokemon_log_trade_event("STATE", "RX_POKEMON -> ERROR (Invalid data)");
                            byte_to_transmit = TRADE_CANCEL_BYTE; // Signal error to partner
                        }
                    }
                }
                pokemon_send_trade_response(byte_to_transmit);
                pokemon_log_trade_event("DETAIL", xfer_log_buffer);
            }
            break;
            
        case TRADE_STATE_SENDING_POKEMON:
            // Static variables for the sending process
            static size_t outgoing_data_sent_bytes = 0;
            static bool sending_outgoing_pokemon_core_data = false;
            static bool sending_outgoing_pokemon_nickname = false;
            static bool sending_outgoing_pokemon_ot_name = false;

            if (current_session.needs_internal_reset) {
                pokemon_log_trade_event("SEND_INIT", "Initializing sending process.");
                sending_outgoing_pokemon_core_data = true;
                sending_outgoing_pokemon_nickname = false;
                sending_outgoing_pokemon_ot_name = false;
                outgoing_data_sent_bytes = 0;
                current_session.needs_internal_reset = false;
                // We might need to send an initial byte to kick things off or wait for GB
                // For now, assume GB sends a byte we reply to.
            }

            if (data_available) {
                uint8_t byte_to_transmit = 0x00; // Default byte to send (e.g., padding or error)
                char xfer_log_buffer[128];

                if (sending_outgoing_pokemon_core_data) {
                    if (outgoing_data_sent_bytes < POKEMON_DATA_SIZE) {
                        byte_to_transmit = ((uint8_t*)&current_session.outgoing_pokemon)[outgoing_data_sent_bytes];
                        snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (Core byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_DATA_SIZE, received_byte);
                        outgoing_data_sent_bytes++;
                    } else {
                        pokemon_log_trade_event("SEND_CORE_DONE", "Core data sent.");
                        sending_outgoing_pokemon_core_data = false;
                        sending_outgoing_pokemon_nickname = true;
                        outgoing_data_sent_bytes = 0;
                        // Send first nickname byte immediately
                        if (outgoing_data_sent_bytes < POKEMON_NAME_LENGTH) {
                             byte_to_transmit = (uint8_t)current_session.outgoing_pokemon.nickname[outgoing_data_sent_bytes];
                             snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (Nickname byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_NAME_LENGTH, received_byte);
                             outgoing_data_sent_bytes++;
                        } else { // Empty nickname, should not happen if POKEMON_NAME_LENGTH > 0 but handle just in case
                            pokemon_log_trade_event("SEND_NICK_DONE", "Nickname (empty) sent.");
                            sending_outgoing_pokemon_nickname = false;
                            sending_outgoing_pokemon_ot_name = true;
                            outgoing_data_sent_bytes = 0;
                            // Send first OT byte
                            if (outgoing_data_sent_bytes < POKEMON_OT_NAME_LENGTH) {
                                byte_to_transmit = (uint8_t)current_session.outgoing_pokemon.ot_name[outgoing_data_sent_bytes];
                                snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (OT Name byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_OT_NAME_LENGTH, received_byte);
                                outgoing_data_sent_bytes++;
                            } else {
                                // This means all parts are zero length, highly unlikely
                                pokemon_log_trade_event("SEND_ALL_DONE", "All parts sent (zero length).");
                                current_session.state = TRADE_STATE_CONFIRMING;
                                pokemon_log_trade_event("STATE", "SENDING_POKEMON -> CONFIRMING");
                                byte_to_transmit = TRADE_ACK_BYTE; // Or some other completion signal
                            }
                        }
                    }
                } else if (sending_outgoing_pokemon_nickname) {
                    if (outgoing_data_sent_bytes < POKEMON_NAME_LENGTH) {
                        byte_to_transmit = (uint8_t)current_session.outgoing_pokemon.nickname[outgoing_data_sent_bytes];
                        snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (Nickname byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_NAME_LENGTH, received_byte);
                        outgoing_data_sent_bytes++;
                    } else {
                        pokemon_log_trade_event("SEND_NICK_DONE", "Nickname sent.");
                        sending_outgoing_pokemon_nickname = false;
                        sending_outgoing_pokemon_ot_name = true;
                        outgoing_data_sent_bytes = 0;
                        // Send first OT byte immediately
                        if (outgoing_data_sent_bytes < POKEMON_OT_NAME_LENGTH) {
                            byte_to_transmit = (uint8_t)current_session.outgoing_pokemon.ot_name[outgoing_data_sent_bytes];
                            snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (OT Name byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_OT_NAME_LENGTH, received_byte);
                            outgoing_data_sent_bytes++;
                        } else {
                             pokemon_log_trade_event("SEND_OT_DONE", "OT Name (empty) sent.");
                             sending_outgoing_pokemon_ot_name = false;
                             pokemon_log_trade_event("SEND_ALL_DONE", "All Pokemon data sent.");
                             current_session.state = TRADE_STATE_CONFIRMING;
                             pokemon_log_trade_event("STATE", "SENDING_POKEMON -> CONFIRMING");
                             byte_to_transmit = TRADE_ACK_BYTE; // Signal completion of sending our Pokemon
                        }
                    }
                } else if (sending_outgoing_pokemon_ot_name) {
                    if (outgoing_data_sent_bytes < POKEMON_OT_NAME_LENGTH) {
                        byte_to_transmit = (uint8_t)current_session.outgoing_pokemon.ot_name[outgoing_data_sent_bytes];
                        snprintf(xfer_log_buffer, sizeof(xfer_log_buffer), "SEND: TX:0x%02X (OT Name byte %zu/%d) / RX:0x%02X", byte_to_transmit, outgoing_data_sent_bytes + 1, POKEMON_OT_NAME_LENGTH, received_byte);
                        outgoing_data_sent_bytes++;
                    } else {
                        pokemon_log_trade_event("SEND_OT_DONE", "OT Name sent.");
                        sending_outgoing_pokemon_ot_name = false;
                        pokemon_log_trade_event("SEND_ALL_DONE", "All Pokemon data sent.");
                        current_session.state = TRADE_STATE_CONFIRMING;
                        pokemon_log_trade_event("STATE", "SENDING_POKEMON -> CONFIRMING");
                        byte_to_transmit = TRADE_ACK_BYTE; // Signal completion of sending our Pokemon
                    }
                } else {
                    // Should not be in this state if not sending any part, but log and recover.
                    pokemon_log_trade_event("ERROR", "SENDING_POKEMON state without active send part. Resetting.");
                    current_session.state = TRADE_STATE_IDLE; // Reset to a safe state
                    byte_to_transmit = TRADE_CANCEL_BYTE; // Signal an issue
                }
                pokemon_send_trade_response(byte_to_transmit);
                pokemon_log_trade_event("DETAIL", xfer_log_buffer);
            } else {
                 // If no data from Game Boy, what should we do? 
                 // The link cable protocol is typically master/slave or relies on specific timing.
                 // For now, we assume the Game Boy will send bytes to clock out our data.
                 // If we initiated the send, we might need to send a Preamble (e.g. 0xFD) 
                 // and then the data if the GB doesn't send anything after a timeout.
                 // This part of the protocol needs to be robust.
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
                                    pokemon_get_species_name(current_session.incoming_pokemon.species),
                                    current_session.incoming_pokemon.level,
                                    current_session.incoming_pokemon.ot_name);
                            pokemon_log_trade_event("TRADE", completion_msg);
                            
                            // Mark the sent Pokemon as traded
                            if (stored_pokemon_count > 1 && pokemon_storage[1].occupied) {
                                char sent_msg[128];
                                snprintf(sent_msg, sizeof(sent_msg), 
                                        "Sent %s (Lv.%d) to partner", 
                                        pokemon_get_species_name(pokemon_storage[1].pokemon.species),
                                        pokemon_storage[1].pokemon.level);
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
            memset(&current_session, 0, sizeof(current_session));
            pokemon_log_trade_event("TRADE", "Trade completed successfully");
            break;
            
        case TRADE_STATE_ERROR:
            // Error state, reset after logging
            pokemon_log_trade_event("ERROR", last_error);
            current_session.state = TRADE_STATE_IDLE;
            pokemon_log_trade_event("STATE", "ERROR → IDLE (error handled)");
            current_session.error_count++;
            break;
    }
}

void pokemon_trading_reset(void) {
    current_session.state = TRADE_STATE_IDLE;
    pokemon_log_trade_event("STATE", "ANY → IDLE (system reset)");
    memset(&current_session, 0, sizeof(current_session));
    pokemon_log_trade_event("SYSTEM", "Trading session reset");
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
                    pokemon_get_species_name(pokemon->species), 
                    pokemon->level, i);
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
            pokemon_get_species_name(pokemon_storage[index].pokemon.species), 
            index);
    
    memset(&pokemon_storage[index], 0, sizeof(pokemon_slot_t));
    stored_pokemon_count--;
    
    pokemon_log_trade_event("STORAGE", log_msg);
    return true;
}

bool pokemon_send_stored(size_t index) {
    if (index >= MAX_STORED_POKEMON || !pokemon_storage[index].occupied) {
        pokemon_log_trade_event("ERROR", "Attempted to send invalid/empty slot");
        return false;
    }

    // Prepare the selected Pokemon for sending
    memcpy(&current_session.outgoing_pokemon, &pokemon_storage[index].pokemon, sizeof(pokemon_data_t));
    current_session.needs_internal_reset = true; // Signal that the trade state machine needs to pick this up.
                                                 // This might need a more specific flag or state transition.
    current_session.state = TRADE_STATE_SENDING_POKEMON; // Transition to sending state

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "Prepared %s (Lv.%d) from slot %zu for sending",
            pokemon_get_species_name(pokemon_storage[index].pokemon.species),
            pokemon_storage[index].pokemon.level, index);
    pokemon_log_trade_event("STORAGE", log_msg);
    pokemon_log_trade_event("STATE", "User Action -> SENDING_POKEMON");


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