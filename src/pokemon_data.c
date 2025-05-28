#include "pokemon_data.h"
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Pokemon species names (Gen 1)
static const char* pokemon_species_names[152] = {
    "",           // 0x00 - No Pokemon
    "BULBASAUR",  // 0x01
    "IVYSAUR",    // 0x02
    "VENUSAUR",   // 0x03
    "CHARMANDER", // 0x04
    "CHARMELEON", // 0x05
    "CHARIZARD",  // 0x06
    "SQUIRTLE",   // 0x07
    "WARTORTLE",  // 0x08
    "BLASTOISE",  // 0x09
    "CATERPIE",   // 0x0A
    "METAPOD",    // 0x0B
    "BUTTERFREE", // 0x0C
    "WEEDLE",     // 0x0D
    "KAKUNA",     // 0x0E
    "BEEDRILL",   // 0x0F
    "PIDGEY",     // 0x10
    "PIDGEOTTO",  // 0x11
    "PIDGEOT",    // 0x12
    "RATTATA",    // 0x13
    "RATICATE",   // 0x14
    "SPEAROW",    // 0x15
    "FEAROW",     // 0x16
    "EKANS",      // 0x17
    "ARBOK",      // 0x18
    "PIKACHU",    // 0x19
    "RAICHU",     // 0x1A
    "SANDSHREW",  // 0x1B
    "SANDSLASH",  // 0x1C
    "NIDORAN♀",   // 0x1D
    "NIDORINA",   // 0x1E
    "NIDOQUEEN",  // 0x1F
    "NIDORAN♂",   // 0x20
    "NIDORINO",   // 0x21
    "NIDOKING",   // 0x22
    "CLEFAIRY",   // 0x23
    "CLEFABLE",   // 0x24
    "VULPIX",     // 0x25
    "NINETALES",  // 0x26
    "JIGGLYPUFF", // 0x27
    "WIGGLYTUFF", // 0x28
    "ZUBAT",      // 0x29
    "GOLBAT",     // 0x2A
    "ODDISH",     // 0x2B
    "GLOOM",      // 0x2C
    "VILEPLUME",  // 0x2D
    "PARAS",      // 0x2E
    "PARASECT",   // 0x2F
    "VENONAT",    // 0x30
    "VENOMOTH",   // 0x31
    "DIGLETT",    // 0x32
    "DUGTRIO",    // 0x33
    "MEOWTH",     // 0x34
    "PERSIAN",    // 0x35
    "PSYDUCK",    // 0x36
    "GOLDUCK",    // 0x37
    "MANKEY",     // 0x38
    "PRIMEAPE",   // 0x39
    "GROWLITHE",  // 0x3A
    "ARCANINE",   // 0x3B
    "POLIWAG",    // 0x3C
    "POLIWHIRL",  // 0x3D
    "POLIWRATH",  // 0x3E
    "ABRA",       // 0x3F
    "KADABRA",    // 0x40
    "ALAKAZAM",   // 0x41
    "MACHOP",     // 0x42
    "MACHOKE",    // 0x43
    "MACHAMP",    // 0x44
    "BELLSPROUT", // 0x45
    "WEEPINBELL", // 0x46
    "VICTREEBEL", // 0x47
    "TENTACOOL",  // 0x48
    "TENTACRUEL", // 0x49
    "GEODUDE",    // 0x4A
    "GRAVELER",   // 0x4B
    "GOLEM",      // 0x4C
    "PONYTA",     // 0x4D
    "RAPIDASH",   // 0x4E
    "SLOWPOKE",   // 0x4F
    "SLOWBRO",    // 0x50
    "MAGNEMITE",  // 0x51
    "MAGNETON",   // 0x52
    "FARFETCH'D", // 0x53
    "DODUO",      // 0x54
    "DODRIO",     // 0x55
    "SEEL",       // 0x56
    "DEWGONG",    // 0x57
    "GRIMER",     // 0x58
    "MUK",        // 0x59
    "SHELLDER",   // 0x5A
    "CLOYSTER",   // 0x5B
    "GASTLY",     // 0x5C
    "HAUNTER",    // 0x5D
    "GENGAR",     // 0x5E
    "ONIX",       // 0x5F
    "DROWZEE",    // 0x60
    "HYPNO",      // 0x61
    "KRABBY",     // 0x62
    "KINGLER",    // 0x63
    "VOLTORB",    // 0x64
    "ELECTRODE",  // 0x65
    "EXEGGCUTE",  // 0x66
    "EXEGGUTOR",  // 0x67
    "CUBONE",     // 0x68
    "MAROWAK",    // 0x69
    "HITMONLEE",  // 0x6A
    "HITMONCHAN", // 0x6B
    "LICKITUNG",  // 0x6C
    "KOFFING",    // 0x6D
    "WEEZING",    // 0x6E
    "RHYHORN",    // 0x6F
    "RHYDON",     // 0x70
    "CHANSEY",    // 0x71
    "TANGELA",    // 0x72
    "KANGASKHAN", // 0x73
    "HORSEA",     // 0x74
    "SEADRA",     // 0x75
    "GOLDEEN",    // 0x76
    "SEAKING",    // 0x77
    "STARYU",     // 0x78
    "STARMIE",    // 0x79
    "MR. MIME",   // 0x7A
    "SCYTHER",    // 0x7B
    "JYNX",       // 0x7C
    "ELECTABUZZ", // 0x7D
    "MAGMAR",     // 0x7E
    "PINSIR",     // 0x7F
    "TAUROS",     // 0x80
    "MAGIKARP",   // 0x81
    "GYARADOS",   // 0x82
    "LAPRAS",     // 0x83
    "DITTO",      // 0x84
    "EEVEE",      // 0x85
    "VAPOREON",   // 0x86
    "JOLTEON",    // 0x87
    "FLAREON",    // 0x88
    "PORYGON",    // 0x89
    "OMANYTE",    // 0x8A
    "OMASTAR",    // 0x8B
    "KABUTO",     // 0x8C
    "KABUTOPS",   // 0x8D
    "AERODACTYL", // 0x8E
    "SNORLAX",    // 0x8F
    "ARTICUNO",   // 0x90
    "ZAPDOS",     // 0x91
    "MOLTRES",    // 0x92
    "DRATINI",    // 0x93
    "DRAGONAIR",  // 0x94
    "DRAGONITE",  // 0x95
    "MEWTWO",     // 0x96
    "MEW",        // 0x97
};

// Pokemon type names
static const char* pokemon_type_names[] = {
    "NORMAL",    // 0x00
    "FIGHTING",  // 0x01
    "FLYING",    // 0x02
    "POISON",    // 0x03
    "GROUND",    // 0x04
    "ROCK",      // 0x05
    "BIRD",      // 0x06 (unused in final game)
    "BUG",       // 0x07
    "GHOST",     // 0x08
    "STEEL",     // 0x09 (Gen 2)
    "???",       // 0x0A
    "???",       // 0x0B
    "???",       // 0x0C
    "???",       // 0x0D
    "???",       // 0x0E
    "???",       // 0x0F
    "???",       // 0x10
    "???",       // 0x11
    "???",       // 0x12
    "???",       // 0x13
    "FIRE",      // 0x14
    "WATER",     // 0x15
    "GRASS",     // 0x16
    "ELECTRIC",  // 0x17
    "PSYCHIC",   // 0x18
    "ICE",       // 0x19
    "DRAGON",    // 0x1A
    "DARK",      // 0x1B (Gen 2)
};

bool pokemon_validate_data(const pokemon_data_t* pokemon) {
    if (!pokemon) {
        return false;
    }
    
    // Check for valid species (1-151 for Gen 1)
    if (pokemon->core.species == 0 || pokemon->core.species > 151) {
        return false;
    }
    
    // Check for reasonable level (1-100)
    if (pokemon->core.level == 0 || pokemon->core.level > 100) {
        return false;
    }
    
    // Check level consistency (level_copy should match level)
    if (pokemon->core.level != pokemon->core.level_copy) {
        return false;
    }
    
    // Check for valid moves (0 = no move, 1-165 for Gen 1)
    for (int i = 0; i < 4; i++) {
        if (pokemon->core.moves[i] > 165) {
            return false;
        }
    }
    
    // Check that current HP doesn't exceed max HP
    if (pokemon->core.current_hp > pokemon->core.max_hp) {
        return false;
    }
    
    // Check nickname and OT name are null-terminated
    bool nickname_terminated = false;
    bool ot_terminated = false;
    
    for (int i = 0; i < POKEMON_NAME_LENGTH; i++) {
        if (pokemon->nickname[i] == '\0') {
            nickname_terminated = true;
            break;
        }
    }
    
    for (int i = 0; i < POKEMON_OT_NAME_LENGTH; i++) {
        if (pokemon->ot_name[i] == '\0') {
            ot_terminated = true;
            break;
        }
    }
    
    return nickname_terminated && ot_terminated;
}

uint8_t pokemon_calculate_checksum(const pokemon_data_t* pokemon) {
    uint8_t checksum = 0;
    
    // Calculate checksum for core data (44 bytes)
    const uint8_t* core_data = (const uint8_t*)&pokemon->core;
    for (size_t i = 0; i < sizeof(pokemon_core_data_t); i++) {
        checksum ^= core_data[i];
    }
    
    // Include nickname in checksum
    for (size_t i = 0; i < POKEMON_NAME_LENGTH && pokemon->nickname[i] != '\0'; i++) {
        checksum ^= (uint8_t)pokemon->nickname[i];
    }
    
    // Include OT name in checksum
    for (size_t i = 0; i < POKEMON_OT_NAME_LENGTH && pokemon->ot_name[i] != '\0'; i++) {
        checksum ^= (uint8_t)pokemon->ot_name[i];
    }
    
    return checksum;
}

const char* pokemon_get_species_name(uint8_t species_id) {
    if (species_id < sizeof(pokemon_species_names) / sizeof(pokemon_species_names[0])) {
        return pokemon_species_names[species_id];
    }
    return "UNKNOWN";
}

const char* pokemon_get_type_name(uint8_t type_id) {
    if (type_id < sizeof(pokemon_type_names) / sizeof(pokemon_type_names[0])) {
        return pokemon_type_names[type_id];
    }
    return "UNKNOWN";
}

const char* pokemon_get_move_name(uint8_t move_id) {
    // Simplified move names - in a full implementation you'd have all 165 moves
    static const char* basic_moves[] = {
        "NONE", "POUND", "KARATE CHOP", "DOUBLE SLAP", "COMET PUNCH",
        "MEGA PUNCH", "PAY DAY", "FIRE PUNCH", "ICE PUNCH", "THUNDER PUNCH"
    };
    
    if (move_id < sizeof(basic_moves) / sizeof(basic_moves[0])) {
        return basic_moves[move_id];
    }
    return "UNKNOWN MOVE";
}

const char* trade_state_to_string(trade_state_t state) {
    switch (state) {
        case TRADE_STATE_IDLE: return "IDLE";
        case TRADE_STATE_WAITING_FOR_PARTNER: return "WAITING_FOR_PARTNER";
        case TRADE_STATE_CONNECTED: return "CONNECTED";
        case TRADE_STATE_RECEIVING_POKEMON: return "RECEIVING_POKEMON";
        case TRADE_STATE_SENDING_POKEMON: return "SENDING_POKEMON";
        case TRADE_STATE_CONFIRMING: return "CONFIRMING";
        case TRADE_STATE_COMPLETE: return "COMPLETE";
        case TRADE_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
} 