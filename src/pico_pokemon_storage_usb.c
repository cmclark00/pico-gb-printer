#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "globals.h"
#include "pokemon_trading.h"
#include "pokemon_data.h"
#include "linkcable.h"

bool debug_enable = ENABLE_DEBUG;
bool speed_240_MHz = false;

// Pokemon trading status
uint32_t last_trade_time = 0;
uint32_t total_trades = 0;

// Link cable interrupt handler for Pokemon trading
bool link_cable_data_received = false;
void link_cable_ISR(void) {
    pokemon_trading_update();
    link_cable_data_received = true;
}

int64_t link_cable_watchdog(alarm_id_t id, void *user_data) {
    if (!link_cable_data_received) {
        linkcable_reset();
        pokemon_trading_reset();
    } else {
        link_cable_data_received = false;
    }
    return MS(300);
}

// USB Serial command handling
void handle_usb_commands(void) {
    static char command_buffer[256];
    static int buffer_pos = 0;
    
    // Check for incoming USB serial data
    int c = getchar_timeout_us(0);
    if (c != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            // End of command
            command_buffer[buffer_pos] = '\0';
            
            if (strcmp(command_buffer, "status") == 0) {
                printf("Pokemon Storage Status:\n");
                printf("- Stored Pokemon: %zu/%d\n", pokemon_get_stored_count(), MAX_STORED_POKEMON);
                printf("- Trade State: %s\n", trade_state_to_string(pokemon_get_trade_state()));
                printf("- Total Trades: %lu\n", total_trades);
                
            } else if (strcmp(command_buffer, "list") == 0) {
                printf("Stored Pokemon:\n");
                pokemon_slot_t* pokemon_list = pokemon_get_stored_list();
                size_t count = pokemon_get_stored_count();
                
                for (size_t i = 0; i < MAX_STORED_POKEMON; i++) {
                    if (pokemon_list[i].occupied) {
                        const pokemon_data_t* pokemon = &pokemon_list[i].pokemon;
                        printf("Slot %zu: %s (Lv.%d) - %s/%s - Trainer: %s\n",
                               i,
                               pokemon_get_species_name(pokemon->species),
                               pokemon->level,
                               pokemon_get_type_name(pokemon->type1),
                               pokemon_get_type_name(pokemon->type2),
                               pokemon->ot_name);
                    }
                }
                
            } else if (strncmp(command_buffer, "delete ", 7) == 0) {
                int slot = atoi(command_buffer + 7);
                if (pokemon_delete_stored(slot)) {
                    printf("Deleted Pokemon from slot %d\n", slot);
                } else {
                    printf("Error: Invalid slot %d\n", slot);
                }
                
            } else if (strcmp(command_buffer, "logs") == 0) {
                printf("Trading Logs:\n");
                const char* logs = pokemon_get_trade_log();
                if (logs && strlen(logs) > 0) {
                    printf("%s", logs);
                } else {
                    printf("No logs available\n");
                }
                
            } else if (strcmp(command_buffer, "reset") == 0) {
                pokemon_trading_reset();
                printf("Trading system reset\n");
                
            } else if (strcmp(command_buffer, "help") == 0) {
                printf("Pokemon Storage Commands:\n");
                printf("- status    : Show system status\n");
                printf("- list      : List all stored Pokemon\n");
                printf("- delete X  : Delete Pokemon from slot X\n");
                printf("- logs      : Show trading logs\n");
                printf("- reset     : Reset trading system\n");
                printf("- help      : Show this help\n");
                
            } else if (strlen(command_buffer) > 0) {
                printf("Unknown command: %s\n", command_buffer);
                printf("Type 'help' for available commands\n");
            }
            
            buffer_pos = 0;
            printf("\n> ");
            
        } else if (c == 8 || c == 127) { // Backspace or Delete
            if (buffer_pos > 0) {
                buffer_pos--;
                printf("\b \b");
            }
        } else if (buffer_pos < sizeof(command_buffer) - 1) {
            command_buffer[buffer_pos++] = c;
            putchar(c);
        }
    }
}

// Key button for reset
#ifdef PIN_KEY
static void key_callback(uint gpio, uint32_t events) {
    linkcable_reset();
    pokemon_trading_reset();
    LED_OFF;
}
#endif

int main(void) {
    speed_240_MHz = set_sys_clock_khz(240000, false);

    // Initialize stdio over USB
    stdio_init_all();

    // Initialize LED
#ifdef LED_PIN
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif
    LED_ON;

    // Initialize Pokemon trading system
    pokemon_trading_init();

#ifdef PIN_KEY
    // Set up reset key
    gpio_init(PIN_KEY);
    gpio_set_dir(PIN_KEY, GPIO_IN);
    gpio_set_irq_enabled_with_callback(PIN_KEY, GPIO_IRQ_EDGE_RISE, true, &key_callback);
#endif

    // Initialize link cable with Pokemon trading handler
    linkcable_init(link_cable_ISR);

    // Set up watchdog timer
    add_alarm_in_us(MS(300), link_cable_watchdog, NULL, true);

    LED_OFF;

    printf("\n=== Pokemon Storage System (USB Mode) ===\n");
    printf("Connect Game Boy Color with link cable to start trading\n");
    printf("Type 'help' for available commands\n");
    printf("> ");

    while (true) {
        // Handle USB serial commands
        handle_usb_commands();
        
        // Update Pokemon trading state machine
        pokemon_trading_update();
        
        // Small delay to prevent busy waiting
        sleep_ms(1);
    }

    return 0;
} 