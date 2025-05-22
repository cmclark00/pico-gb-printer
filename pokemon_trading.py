import sqlite3

# --- Database Setup (Minimal for testing) ---
def setup_database(db_path):
    """
    Sets up the Pokemon database. Drops and recreates the Pokemon table.
    This function is designed to be safe to run multiple times for a clean setup.
    The actual sample data is inserted by the __main__ block of this script.
    """
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute("""
    DROP TABLE IF EXISTS Pokemon;
    """)
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS Pokemon (
        pokemon_id INTEGER PRIMARY KEY AUTOINCREMENT,
        trainer_id INTEGER,
        species TEXT NOT NULL,
        nickname TEXT,
        level INTEGER DEFAULT 1,
        moves_json TEXT DEFAULT '[]',
        ivs_json TEXT DEFAULT '{}',
        evs_json TEXT DEFAULT '{}',
        ability TEXT,
        held_item TEXT,
        ot TEXT, -- Original Trainer Name
        tid TEXT, -- Original Trainer ID
        sid TEXT, -- Original Trainer Secret ID
        friendship INTEGER DEFAULT 70,
        pokeball TEXT,
        nature TEXT,
        gender TEXT,
        is_shiny BOOLEAN DEFAULT FALSE,
        date_met TEXT,
        location_met TEXT,
        language_of_origin TEXT
    );
    """)
    conn.commit()
    conn.close()

# --- Core Trading Logic ---

BASE_FRIENDSHIP = 70 # Default friendship value for traded Pokemon (Gen 2+ standard)

# Defines trade evolutions that occur without any special items (typically Gen 1).
GEN_1_EVOLUTIONS = {
    "Kadabra": "Alakazam",
    "Machoke": "Machamp",
    "Graveler": "Golem",
    "Haunter": "Gengar",
}

# Defines trade evolutions that require a specific held item (typically Gen 2).
# Structure: "PokemonSpecies": {"item": "RequiredItem", "evolves_to": "EvolvedSpecies"}
GEN_2_EVOLUTIONS = {
    "Poliwhirl": {"item": "Kings Rock", "evolves_to": "Politoed"}, # Poliwhirl holding King's Rock
    "Slowpoke": {"item": "Kings Rock", "evolves_to": "Slowking"},  # Slowpoke holding King's Rock
    "Onix": {"item": "Metal Coat", "evolves_to": "Steelix"},
    "Scyther": {"item": "Metal Coat", "evolves_to": "Scizor"},
    "Seadra": {"item": "Dragon Scale", "evolves_to": "Kingdra"},
    "Porygon": {"item": "Up-Grade", "evolves_to": "Porygon2"},    # Porygon holding Up-Grade
}

# Defines trade evolutions for Gen 3, specifically Clamperl which has item-dependent branched evolution.
GEN_3_EVOLUTIONS = {
    "Clamperl": [ 
        {"item": "DeepSeaTooth", "evolves_to": "Huntail"}, # Clamperl holding DeepSeaTooth
        {"item": "DeepSeaScale", "evolves_to": "Gorebyss"},# Clamperl holding DeepSeaScale
    ]
}

def check_and_perform_trade_evolution(db_path, pokemon_id, new_trainer_id):
    """
    Checks if a traded Pokemon meets evolution criteria and updates its species and item in the database if it evolves.
    Handles Gen 1 (no item), Gen 2 (specific item), and Gen 3 (Clamperl-specific items) trade evolutions.
    The Everstone item will prevent any evolution. Consumes the item if used for evolution.
    This function operates as its own transaction for the evolution update.
    """
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    evolved = False
    item_consumed = False
    original_item = None

    try:
        cursor.execute("SELECT species, held_item FROM Pokemon WHERE pokemon_id = ?", (pokemon_id,))
        result = cursor.fetchone()
        if not result:
            print(f"Error: Pokemon with ID {pokemon_id} not found.")
            return False

        current_species, held_item = result
        original_item = held_item # Keep track for potential restoration if Everstone is held

        # Gen 2: Everstone prevents evolution
        if held_item == "Everstone":
            return False

        new_species = None

        # Check Gen 1 evolutions
        if current_species in GEN_1_EVOLUTIONS:
            new_species = GEN_1_EVOLUTIONS[current_species]

        # Check Gen 2 evolutions (item-dependent)
        elif current_species in GEN_2_EVOLUTIONS:
            evo_info = GEN_2_EVOLUTIONS[current_species]
            if held_item == evo_info["item"]:
                new_species = evo_info["evolves_to"]
                item_consumed = True
        
        # Check Gen 3 evolutions (Clamperl-specific item check)
        elif current_species == "Clamperl" and current_species in GEN_3_EVOLUTIONS:
            for evo_case in GEN_3_EVOLUTIONS[current_species]:
                if held_item == evo_case["item"]:
                    new_species = evo_case["evolves_to"]
                    item_consumed = True
                    break # Found the correct evolution path

        if new_species:
            print(f"Pokemon ID {pokemon_id} ({current_species}) is evolving to {new_species} for trainer {new_trainer_id}!")
            update_query = "UPDATE Pokemon SET species = ?"
            params = [new_species]

            if item_consumed:
                print(f"Item {held_item} was consumed.")
                update_query += ", held_item = NULL"
            
            update_query += " WHERE pokemon_id = ?"
            params.append(pokemon_id)
            
            cursor.execute(update_query, tuple(params))
            conn.commit()
            evolved = True
        
    except sqlite3.Error as e:
        print(f"Database error during evolution check for pokemon {pokemon_id}: {e}")
        conn.rollback() # Rollback any partial changes if error occurs
        # If an item was conceptually consumed before error, but not committed,
        # it's fine as the transaction will roll back.
        # If Everstone was involved, original_item logic isn't strictly needed due to rollback,
        # but good for clarity if we had more complex pre-commit logic.
        return False
    finally:
        conn.close()
        
    return evolved

def execute_trade(db_path, trainer_a_id, pokemon_a_id, trainer_b_id, pokemon_b_id):
    """
    Executes a Pokemon trade between two trainers.
    Performs operations as a single database transaction.
    Resets friendship to base value (70) for both Pokemon (Gen 2 behavior).
    Calls check_and_perform_trade_evolution for both Pokemon.
    """
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    try:
        # Start transaction
        cursor.execute("BEGIN TRANSACTION;")

        # Verify Pokemon exist and get their original trainers for logging/safety (optional)
        cursor.execute("SELECT trainer_id FROM Pokemon WHERE pokemon_id = ?", (pokemon_a_id,))
        pk_a_owner = cursor.fetchone()
        cursor.execute("SELECT trainer_id FROM Pokemon WHERE pokemon_id = ?", (pokemon_b_id,))
        pk_b_owner = cursor.fetchone()

        if not pk_a_owner or not pk_b_owner:
            print("Error: One or both Pokemon not found.")
            conn.rollback()
            return False
        
        # Optional: Check if trainers actually own these Pokemon
        # For now, we assume the input is correct as per problem statement

        # Update owner for Pokemon A
        cursor.execute("UPDATE Pokemon SET trainer_id = ?, friendship = ? WHERE pokemon_id = ?",
                       (trainer_b_id, BASE_FRIENDSHIP, pokemon_a_id))
        print(f"Pokemon {pokemon_a_id} ownership changed to trainer {trainer_b_id}, friendship reset.")

        # Update owner for Pokemon B
        cursor.execute("UPDATE Pokemon SET trainer_id = ?, friendship = ? WHERE pokemon_id = ?",
                       (trainer_a_id, BASE_FRIENDSHIP, pokemon_b_id))
        print(f"Pokemon {pokemon_b_id} ownership changed to trainer {trainer_a_id}, friendship reset.")

        conn.commit() # Commit ownership changes before evolution checks

    except sqlite3.Error as e:
        print(f"Database error during trade execution: {e}")
        conn.rollback()
        return False
    finally:
        # Evolution checks are outside the main ownership transaction block
        # as they are consequential actions. If they fail, the trade itself is still valid.
        # However, if evolution updates fail, those specific updates are rolled back
        # within check_and_perform_trade_evolution.
        pass 

    # Perform evolution checks separately after the main trade transaction is committed.
    # This ensures that if an evolution fails, the trade itself is not rolled back.
    # Each evolution check is its own mini-transaction internally managed by check_and_perform_trade_evolution.
    
    evolved_a = False # Flag for Pokemon A's evolution
    evolved_b = False # Flag for Pokemon B's evolution
    
    # The design decision here is that evolution checks are separate from the main trade transaction.
    # The trade (ownership swap) is committed first. Then, evolution checks are performed.
    # If an evolution check fails or the script crashes, the ownership swap remains.
    # This aligns with game behavior where the trade itself finalizes before evolution sequences typically play.
    # For true atomicity of trade + evolution as a single unit, the evolution logic would need to be
    # part of the same database transaction as the ownership swap.
    
    print(f"\nChecking evolution for Pokemon {pokemon_a_id} (now with new trainer {trainer_b_id})...")
    evolved_a = check_and_perform_trade_evolution(db_path, pokemon_a_id, trainer_b_id)
    
    print(f"\nChecking evolution for Pokemon {pokemon_b_id} (now with new trainer {trainer_a_id})...")
    evolved_b = check_and_perform_trade_evolution(db_path, pokemon_b_id, trainer_a_id)

    print(f"\nTrade completed. Pokemon A (ID: {pokemon_a_id}) evolved: {evolved_a}. Pokemon B (ID: {pokemon_b_id}) evolved: {evolved_b}.")
    
    # The function returns True if the ownership swap part of the trade was successful.
    # Evolution success/failure does not change the success status of the trade itself.
    conn.close() # Close the connection used for the main trade transaction
    return True


if __name__ == '__main__':
    # This block is executed when pokemon_trading.py is run directly.
    # It sets up the database and populates it with sample trainers and Pokemon
    # to facilitate testing of the trading and evolution logic, and to provide
    # initial data for the web application.
    DB_PATH = "pokemon_trade.db" # Defines the database file name
    setup_database(DB_PATH)      # Creates the Pokemon table schema (drops if exists)

    conn = sqlite3.connect(DB_PATH) # Connect to the database to insert sample data
    cursor = conn.cursor()

    # Sample Trainers
    TRAINER_ASH = 101
    TRAINER_MISTY = 102
    TRAINER_MAY = 103 # New trainer for Gen 3 tests

    # Add some Pokemon
    # Gen 1 Trade Evos
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Kadabra", "AshKadabra", None))
    ash_kadabra_id = cursor.lastrowid
    
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Machoke", "MistyMachoke", None))
    misty_machoke_id = cursor.lastrowid

    # Gen 2 Trade Evos
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Onix", "AshOnix", "Metal Coat"))
    ash_onix_id = cursor.lastrowid

    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Scyther", "MistyScyther", "Metal Coat"))
    misty_scyther_id = cursor.lastrowid
    
    # Pokemon for Everstone test
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Haunter", "AshHaunterStone", "Everstone"))
    ash_haunter_stone_id = cursor.lastrowid
    
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Poliwhirl", "MistyPoliStone", "Everstone"))
    misty_poliwhirl_stone_id = cursor.lastrowid

    # Pokemon that won't evolve
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Pikachu", "AshPika", None))
    ash_pikachu_id = cursor.lastrowid
    
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Staryu", "MistyStaryu", None))
    misty_staryu_id = cursor.lastrowid
    
    # Gen 2 item trade without correct item
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Porygon", "AshPorygonWrong", "Potion")) # Wrong item
    ash_porygon_wrong_item_id = cursor.lastrowid

    # Gen 3 Trade Evos for Clamperl
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_ASH, "Clamperl", "AshClamperlTooth", "DeepSeaTooth"))
    ash_clamperl_tooth_id = cursor.lastrowid
    
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Clamperl", "MistyClamperlScale", "DeepSeaScale"))
    misty_clamperl_scale_id = cursor.lastrowid

    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MAY, "Clamperl", "MayClamperlWrong", "Metal Coat")) # Wrong item for Clamperl
    may_clamperl_wrong_item_id = cursor.lastrowid
    
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MAY, "Clamperl", "MayClamperlStone", "Everstone")) # Everstone
    may_clamperl_everstone_id = cursor.lastrowid
    
    # A non-evolving Pokemon for May to trade with
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MAY, "Zigzagoon", "MayZigzagoon", None))
    may_zigzagoon_id = cursor.lastrowid


    conn.commit()
    conn.close()

    print("--- Initial Pokemon State ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon ORDER BY pokemon_id"):
        print(row)
    conn.close()
    print("-" * 30)

    print("\n--- Test 1: Gen 1 Trade Evolution (Kadabra for Machoke) ---")
    execute_trade(DB_PATH, TRAINER_ASH, ash_kadabra_id, TRAINER_MISTY, misty_machoke_id)
    print("\n--- Pokemon State After Test 1 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_kadabra_id, misty_machoke_id)):
        print(row)
    conn.close()
    print("-" * 30)

    print("\n--- Test 2: Gen 2 Trade Evolution (Onix w/ Metal Coat for Scyther w/ Metal Coat) ---")
    execute_trade(DB_PATH, TRAINER_ASH, ash_onix_id, TRAINER_MISTY, misty_scyther_id)
    print("\n--- Pokemon State After Test 2 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_onix_id, misty_scyther_id)):
        print(row)
    conn.close()
    print("-" * 30)
    
    print("\n--- Test 3: Trade with Everstone (Haunter w/ Everstone for Poliwhirl w/ Everstone) ---")
    execute_trade(DB_PATH, TRAINER_ASH, ash_haunter_stone_id, TRAINER_MISTY, misty_poliwhirl_stone_id)
    print("\n--- Pokemon State After Test 3 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_haunter_stone_id, misty_poliwhirl_stone_id)):
        print(row)
    conn.close()
    print("-" * 30)

    print("\n--- Test 4: Trade no evolution (Pikachu for Staryu) ---")
    execute_trade(DB_PATH, TRAINER_ASH, ash_pikachu_id, TRAINER_MISTY, misty_staryu_id)
    print("\n--- Pokemon State After Test 4 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_pikachu_id, misty_staryu_id)):
        print(row)
    conn.close()
    print("-" * 30)
    
    print("\n--- Test 5: Gen 2 item trade with wrong item (Porygon w/ Potion for Staryu) ---")
    # Re-use misty_staryu_id, it's now with TRAINER_ASH from Test 4
    # We need a Pokemon from another trainer for Porygon to be traded with. Let's use May's Zigzagoon.
    # First, ensure May's Zigzagoon is with May (103)
    cursor.execute("UPDATE Pokemon SET trainer_id = ? WHERE pokemon_id = ?", (TRAINER_MAY, may_zigzagoon_id))
    conn.commit()

    execute_trade(DB_PATH, TRAINER_ASH, ash_porygon_wrong_item_id, TRAINER_MAY, may_zigzagoon_id) 
    print("\n--- Pokemon State After Test 5 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_porygon_wrong_item_id, may_zigzagoon_id)):
        print(row)
    conn.close()
    print("-" * 30)

    print("\n--- Test 6: Gen 3 Clamperl Evolution (Ash's Clamperl w/ DeepSeaTooth for Misty's Clamperl w/ DeepSeaScale) ---")
    execute_trade(DB_PATH, TRAINER_ASH, ash_clamperl_tooth_id, TRAINER_MISTY, misty_clamperl_scale_id)
    print("\n--- Pokemon State After Test 6 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (ash_clamperl_tooth_id, misty_clamperl_scale_id)):
        print(row)
    conn.close()
    print("-" * 30)

    print("\n--- Test 7: Gen 3 Clamperl with wrong item (May's Clamperl w/ Metal Coat for Ash's Pikachu) ---")
    # Ash's Pikachu is now with Misty (102) from Test 4.
    # Let's ensure Ash's Pikachu (now Misty's) is reset for this trade.
    # Or, more simply, use a different Pokemon. Let's use Ash's Kadabra (now Alakazam, with Misty 102 from Test 1)
    # Actually, let's make sure may_zigzagoon_id is back with May before this trade. It was traded to Ash in Test 5.
    cursor.execute("UPDATE Pokemon SET trainer_id = ? WHERE pokemon_id = ?", (TRAINER_MAY, may_zigzagoon_id))
    conn.commit()

    execute_trade(DB_PATH, TRAINER_MAY, may_clamperl_wrong_item_id, TRAINER_ASH, ash_pikachu_id) # ash_pikachu_id is now with Misty (102)
    print("\n--- Pokemon State After Test 7 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    # Querying for may_clamperl_wrong_item_id and the current owner of ash_pikachu_id (which is Misty)
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (may_clamperl_wrong_item_id, ash_pikachu_id)):
        print(row)
    conn.close()
    print("-" * 30)
    
    print("\n--- Test 8: Gen 3 Clamperl with Everstone (May's Clamperl w/ Everstone for Misty's Staryu) ---")
    # Misty's Staryu is now with Ash (101) from Test 4.
    # Let's ensure Misty's Staryu is back with Misty for this trade.
    # Or, use another Pokemon. Let's use May's other Zigzagoon, which we'll add.
    cursor.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
                   (TRAINER_MISTY, "Marill", "MistyMarill", None))
    misty_marill_id = cursor.lastrowid
    conn.commit()

    execute_trade(DB_PATH, TRAINER_MAY, may_clamperl_everstone_id, TRAINER_MISTY, misty_marill_id)
    print("\n--- Pokemon State After Test 8 ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon WHERE pokemon_id IN (?,?) ORDER BY pokemon_id", (may_clamperl_everstone_id, misty_marill_id)):
        print(row)
    conn.close()
    print("-" * 30)

    # Verify one of the older tests still works (e.g. Test 1 output will be visible in the full run)
    # The script already prints "--- Pokemon State After Test 1 ---" which shows Kadabra->Alakazam and Machoke->Machamp.

    print("\n--- Final Pokemon State ---")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    for row in cursor.execute("SELECT pokemon_id, trainer_id, species, held_item, friendship FROM Pokemon ORDER BY pokemon_id"):
        print(row)
    conn.close()
    print("-" * 30)

    # Example of how to call check_and_perform_trade_evolution directly (e.g., if a different event triggered it)
    # print("\n--- Direct Evolution Check Example (Not part of a trade) ---")
    # conn_direct = sqlite3.connect(DB_PATH)
    # cursor_direct = conn_direct.cursor()
    # cursor_direct.execute("INSERT INTO Pokemon (trainer_id, species, nickname, held_item) VALUES (?, ?, ?, ?)",
    #                (TRAINER_ASH, "Clamperl", "TestClamperlDirect", "DeepSeaTooth"))
    # test_clamperl_id = cursor_direct.lastrowid
    # conn_direct.commit()
    # check_and_perform_trade_evolution(DB_PATH, test_clamperl_id, TRAINER_ASH)
    # for row in cursor_direct.execute("SELECT pokemon_id, trainer_id, species, held_item FROM Pokemon WHERE pokemon_id = ?", (test_clamperl_id,)):
    #     print(row)
    # conn_direct.close()
    
    # Clean up the dummy database file
    # import os
    # os.remove(DB_PATH)
    # print(f"\nCleaned up {DB_PATH}")