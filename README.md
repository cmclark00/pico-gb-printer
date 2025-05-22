# Pokemon Trading Simulator (Generations 1-3)

## Project Overview
This project is a multi-generational Pokemon trading simulator that supports mechanics from Pokemon Generations 1, 2, and 3. It includes logic for trade evolutions, item-based evolutions, and the Everstone item. The simulation is accessed and controlled via a web-based user interface. Pokemon data and trainer information are stored in an SQLite database.

## Features
-   **Multi-Generational Trading:** Supports trade evolution rules from Gen 1, Gen 2, and Gen 3.
-   **Evolution Logic:**
    -   Standard trade evolutions (e.g., Kadabra to Alakazam).
    -   Item-dependent trade evolutions (e.g., Onix + Metal Coat to Steelix, Scyther + Metal Coat to Scizor).
    -   Specific item evolutions for Gen 2 (King's Rock, Up-Grade, Dragon Scale).
    -   Specific item evolutions for Gen 3 (Clamperl with DeepSeaTooth or DeepSeaScale).
-   **Item Handling:**
    -   Consumes items like Metal Coat, King's Rock, DeepSeaTooth, etc., upon successful evolution.
    -   Everstone functionality to prevent evolution.
-   **Web User Interface:** Built with Flask, allowing users to:
    -   View a trainer's list of Pokemon.
    -   Initiate a trade by selecting a Pokemon.
    -   Join an existing trade using a shared link/trade ID.
    -   Offer Pokemon for trade.
    -   Confirm and execute trades.
    -   View trade outcomes, including evolutions.
-   **SQLite Database Storage:** Stores Pokemon details (species, nickname, level, held item, trainer ID, etc.).
-   **Friendship Reset:** Simulates Gen 2+ behavior where traded Pokemon have their friendship reset to a base value (70).

## Directory Structure
```
pokemon_project_root/
|-- pokemon_trade.db        # SQLite database
|-- pokemon_trading.py      # Core trading logic, DB setup, evolution rules
|-- pokemon_web_app/
|   |-- app.py              # Flask application, UI routes, web backend
|   |-- templates/          # HTML templates for the web UI
|   |   |-- pokemon_list.html
|   |   |-- trade_interface.html
|   |-- static/             # (Optional, if you added any CSS/JS files)
|-- README.md               # This file
```

## Setup and Installation

**Prerequisites:**
-   Python 3.x
-   Flask

**Installation:**
```bash
pip install Flask
```

**Database Initialization:**
The database `pokemon_trade.db` is created and populated with initial sample data if it doesn't exist when `pokemon_web_app/app.py` is first run. The `setup_database` function from `pokemon_trading.py` is called, which is designed to be safe to run multiple times.
*(Self-correction: The original `pokemon_trading.py`'s `if __name__ == '__main__':` block is what populates the diverse sample data. `app.py`'s call to `setup_database` only ensures the schema. The README should be clear on this.)*

To ensure the database is created with the schema AND populated with diverse sample data (Ash, Misty, various Pokemon for testing evolution, etc.), you **must** run `pokemon_trading.py` directly from the project root directory at least once:
```bash
python pokemon_trading.py
```
Subsequently, running `pokemon_web_app/app.py` will use this populated database. If `pokemon_trade.db` is missing when `app.py` runs, it will create the schema but the database will be empty of Pokemon data.

## Running the Application
1.  Ensure the database `pokemon_trade.db` has been created and populated by running `python pokemon_trading.py` as described above.
2.  Navigate to the `pokemon_web_app` directory (if you are in the project root):
    ```bash
    cd pokemon_web_app
    ```
3.  Run the Flask application:
    ```bash
    python app.py
    ```
    (Alternatively, from the project root: `python pokemon_web_app/app.py`)

4.  **Access:** Open your browser and go to `http://127.0.0.1:5000/trainer/101/pokemon_list` (for Ash), `http://127.0.0.1:5000/trainer/102/pokemon_list` (for Misty), or `http://127.0.0.1:5000/trainer/103/pokemon_list` (for May) to view their Pokemon. Other trainer IDs might exist if you added more in `setup_database`.

## How to Use (Trading Flow)
1.  Trainer A navigates to their Pokemon list and clicks "Select for Trade" on a Pokemon.
2.  Trainer A is taken to the trade interface page. A flash message will appear with trade initiation confirmation. The trade interface page itself will show a shareable link like: `http://127.0.0.1:5000/trade/<trade_id>/trainer/PUT_PARTNERS_TRAINER_ID_HERE`.
3.  Trainer A copies this link and sends it to Trainer B. Trainer B replaces `PUT_PARTNERS_TRAINER_ID_HERE` with their own trainer ID (e.g., 102) and navigates to the URL.
4.  Trainer B sees Trainer A's offer and can select one of their own Pokemon to offer.
5.  Once both trainers have offered Pokemon, they will see "Confirm Trade" buttons.
6.  Both trainers must click "Confirm Trade".
7.  The trade is processed, and the page updates with the results (success, failure, evolutions).

## Code Overview
-   **`pokemon_trading.py`**: Contains the core data-agnostic trading logic (`execute_trade`), evolution rules (`check_and_perform_trade_evolution`), and initial database schema setup (`setup_database`). The `if __name__ == '__main__':` block in this script is responsible for populating the database with rich sample data for testing.
-   **`pokemon_web_app/app.py`**: Implements the Flask web application, defining routes for viewing Pokemon, initiating trades, handling offers, and confirming trades. It uses an in-memory dictionary (`active_trades`) to manage ongoing trade sessions.

This project demonstrates a simulation of core Pokemon trading features with a focus on accurate evolution mechanics for the specified generations.
