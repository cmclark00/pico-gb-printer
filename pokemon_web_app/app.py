import sqlite3
import json
import uuid
import os
import sys # For modifying path
from flask import Flask, render_template, g, redirect, url_for, flash

# Adjust path to import from parent directory
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
sys.path.append(parent_dir)

from pokemon_trading import execute_trade as execute_pokemon_trade # Import and alias
# from pokemon_trading import check_and_perform_trade_evolution # Not strictly needed if execute_trade handles evolution details adequately via get_pokemon_details

app = Flask(__name__)
app.secret_key = 'your_very_secret_key' # Needed for flash messages; consider environment variable for production
DATABASE = os.path.join(parent_dir, 'pokemon_trade.db') # Defines the path to the SQLite database, relative to the project root

# In-memory store for active trades.
# Note: For a production application, this should be replaced with a more persistent and scalable solution
# like Redis, a database table, or a distributed cache. In-memory storage will be lost on app restart
# and won't work if multiple instances of the app are run (e.g., with Gunicorn workers).
active_trades = {} # Stores trade_id: {trade_session_data}

def get_db():
    db = getattr(g, '_database', None)
    if db is None:
        db = g._database = sqlite3.connect(DATABASE)
        db.row_factory = sqlite3.Row # Access columns by name
    return db

@app.teardown_appcontext
def close_connection(exception):
    db = getattr(g, '_database', None)
    if db is not None:
        db.close()

def query_db(query, args=(), one=False):
    cur = get_db().execute(query, args)
    rv = cur.fetchall()
    cur.close()
    return (rv[0] if rv else None) if one else rv

@app.route('/trainer/<int:trainer_id>/pokemon_list')
def pokemon_list(trainer_id):
    # Fetch trainer's name/details if available (optional, for display)
    # For now, just using trainer_id
    
    pokemon_data = query_db("""
        SELECT pokemon_id, species, nickname, level, held_item, moves_json 
        FROM Pokemon 
        WHERE trainer_id = ?
    """, (trainer_id,))
    
    processed_pokemon_data = []
    for pkm_row in pokemon_data:
        pkm_dict = dict(pkm_row) 
        try:
            pkm_dict['moves'] = json.loads(pkm_dict['moves_json']) if pkm_dict['moves_json'] else []
        except (TypeError, json.JSONDecodeError):
            pkm_dict['moves'] = []
        processed_pokemon_data.append(pkm_dict)

    return render_template('pokemon_list.html', 
                           trainer_id=trainer_id, 
                           pokemon_list=processed_pokemon_data)

def get_pokemon_details(pokemon_id):
    # This function can be used to get details before and after trade to check evolution.
    # Fetches the current state of a Pokemon from the database.
    pkm = query_db("SELECT pokemon_id, species, nickname, level, held_item, moves_json, trainer_id FROM Pokemon WHERE pokemon_id = ?", (pokemon_id,), one=True)
    if pkm:
        pkm_dict = dict(pkm) # Convert sqlite3.Row to a dictionary for easier key-based access and modification
        try:
            pkm_dict['moves'] = json.loads(pkm_dict['moves_json']) if pkm_dict['moves_json'] else []
        except (TypeError, json.JSONDecodeError):
            pkm_dict['moves'] = []
        return pkm_dict
    return None

@app.route('/initiate_trade/<int:trainer_id>/<int:pokemon_id>')
def initiate_trade(trainer_id, pokemon_id):
    trade_id = uuid.uuid4().hex
    pokemon_details = get_pokemon_details(pokemon_id)

    if not pokemon_details or pokemon_details['trainer_id'] != trainer_id:
        flash("Error: Pokemon not found or does not belong to this trainer.", "error")
        return redirect(url_for('pokemon_list', trainer_id=trainer_id))

    active_trades[trade_id] = {
        "trainer_a_id": trainer_id,
        "trainer_b_id": None,
        "pokemon_a_id": pokemon_id,
        "pokemon_a_details": pokemon_details,
        "pokemon_b_id": None,
        "pokemon_b_details": None,
        "trainer_a_confirmed": False,
        "trainer_b_confirmed": False,
        "status": "pending_b" # Initial status: waiting for Trainer B to join and make an offer
    }
    # Refined flash message as per subtask instructions
    flash(f"Trade initiated! You will find a shareable link on the trade page. Or, if your partner knows the trade ID ({trade_id}), they can construct the URL.", "info")
    return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=trainer_id))

@app.route('/trade/<trade_id>/trainer/<int:current_trainer_id>')
def trade_interface(trade_id, current_trainer_id):
    trade_session = active_trades.get(trade_id)

    if not trade_session:
        flash("Trade session not found or has expired.", "error")
        # Potentially redirect to a generic error page or trainer's Pokemon list
        return redirect(url_for('pokemon_list', trainer_id=current_trainer_id)) # Fallback

    # If trainer_b_id is not set (i.e., Trainer B hasn't joined yet via URL)
    # and the current_trainer_id visiting this page is not Trainer A (the initiator),
    # then this current_trainer_id is dynamically assigned as Trainer B for this trade session.
    if trade_session["trainer_b_id"] is None and current_trainer_id != trade_session["trainer_a_id"]:
        trade_session["trainer_b_id"] = current_trainer_id
        if trade_session["status"] == "pending_b": # Check if status is still 'pending_b' (waiting for B to join)
             trade_session["status"] = "pending_b_offer" # Update status: B has now joined and needs to make an offer.

    # Determine roles for the template to customize display and available actions
    is_trainer_a = (current_trainer_id == trade_session["trainer_a_id"])
    is_trainer_b = (current_trainer_id == trade_session["trainer_b_id"])
    
    other_trainer_id = None
    if is_trainer_a and trade_session["trainer_b_id"]:
        other_trainer_id = trade_session["trainer_b_id"]
    elif is_trainer_b: # trainer_b_id must be set if is_trainer_b is true
        other_trainer_id = trade_session["trainer_a_id"]


    trainer_pokemon_list = []
    # If current user is B and hasn't offered, or if A wants to change (not implemented yet for A to change)
    # For now, only B can make an initial offer via this page.
    if is_trainer_b and not trade_session["pokemon_b_id"] and trade_session["status"] == "pending_b_offer":
        pokemon_data = query_db("SELECT pokemon_id, species, nickname FROM Pokemon WHERE trainer_id = ?", (current_trainer_id,))
        for pkm_row in pokemon_data:
            # Exclude Pokemon already offered by trainer A if B is also A (trading with self - not typical but for testing)
            if trade_session["pokemon_a_id"] == pkm_row["pokemon_id"] and trade_session["trainer_a_id"] == current_trainer_id:
                continue
            trainer_pokemon_list.append(dict(pkm_row))
            
    elif is_trainer_a and not trade_session["pokemon_a_id"]: # Should not happen with current initiate_trade
        # This case is if A cancels their offer and wants to re-offer. For now, initiate_trade always sets pokemon_a.
        # For simplicity, we won't implement A changing offer in this step.
        pass


    return render_template('trade_interface.html',
                           trade_id=trade_id,
                           trade_session=trade_session,
                           current_trainer_id=current_trainer_id,
                           is_trainer_a=is_trainer_a,
                           is_trainer_b=is_trainer_b,
                           other_trainer_id=other_trainer_id,
                           trainer_pokemon_list=trainer_pokemon_list)

@app.route('/offer_pokemon/<trade_id>/<int:offering_trainer_id>/<int:pokemon_id>')
def offer_pokemon(trade_id, offering_trainer_id, pokemon_id):
    trade_session = active_trades.get(trade_id)

    if not trade_session:
        flash("Trade session not found.", "error")
        return redirect(url_for('pokemon_list', trainer_id=offering_trainer_id)) # Fallback

    pokemon_details = get_pokemon_details(pokemon_id)
    if not pokemon_details or pokemon_details['trainer_id'] != offering_trainer_id:
        flash("Error: Pokemon not found or does not belong to this trainer.", "error")
        return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=offering_trainer_id))

    # Check if this Pokemon is already offered by the other trainer
    if (trade_session["pokemon_a_id"] == pokemon_id and trade_session["trainer_a_id"] != offering_trainer_id) or \
       (trade_session["pokemon_b_id"] == pokemon_id and trade_session["trainer_b_id"] != offering_trainer_id):
        flash("This Pokemon is already part of the trade by the other trainer.", "error")
        return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=offering_trainer_id))
    
    # Logic for who is offering
    # This primarily handles trainer B's first offer.
    # It also allows Trainer A to change their offer, which would reset confirmations for both trainers.
    if trade_session["trainer_a_id"] == offering_trainer_id:
        # Logic for Trainer A changing their offered Pokemon.
        trade_session["pokemon_a_id"] = pokemon_id
        trade_session["pokemon_a_details"] = pokemon_details
        trade_session["trainer_a_confirmed"] = False # Reset A's confirmation.
        trade_session["trainer_b_confirmed"] = False # Reset B's confirmation as well, as the offer changed.
        # Update status: if B had already offered, move to confirm; otherwise, B still needs to offer.
        trade_session["status"] = "pending_confirm" if trade_session["pokemon_b_id"] else "pending_b_offer"

    elif trade_session["trainer_b_id"] == offering_trainer_id or trade_session["trainer_b_id"] is None:
        # Logic for Trainer B making their offer.
        if trade_session["trainer_b_id"] is None: # This should ideally be set when B first visits the trade_interface.
            trade_session["trainer_b_id"] = offering_trainer_id # Assign if somehow missed.
            
        # Prevent offering the exact same Pokemon instance if Trainer A and B are the same person (self-trade scenario).
        if trade_session["pokemon_a_id"] == pokemon_id and trade_session["trainer_a_id"] == offering_trainer_id:
             flash("Cannot offer the same Pokemon that is already part of the trade by Trainer A.", "error")
        else:
            trade_session["pokemon_b_id"] = pokemon_id
            trade_session["pokemon_b_details"] = pokemon_details
            trade_session["trainer_a_confirmed"] = False # Reset confirmations for both trainers since B's offer is new/changed.
            trade_session["trainer_b_confirmed"] = False
            if trade_session["pokemon_a_id"]: # If Trainer A's offer is still valid (which it should be).
                trade_session["status"] = "pending_confirm" # Both have now offered, waiting for confirmations.
            else: 
                # This case (A's offer missing) should ideally not be reached if A always initiates and doesn't clear their offer.
                trade_session["status"] = "pending_a_offer" # Indicates something is amiss with Trainer A's offer.
    else:
        # The user trying to make an offer is not recognized as either Trainer A or the assigned Trainer B.
        flash("You are not an active participant in this trade.", "error")
        return redirect(url_for('pokemon_list', trainer_id=offering_trainer_id))


    return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=offering_trainer_id))

@app.route('/cancel_trade/<trade_id>/<int:cancelling_trainer_id>')
def cancel_trade(trade_id, cancelling_trainer_id):
    trade_session = active_trades.get(trade_id)
    if not trade_session:
        flash("Trade not found.", "error")
        return redirect(url_for('pokemon_list', trainer_id=cancelling_trainer_id))

    if trade_session["trainer_a_id"] == cancelling_trainer_id or \
       trade_session["trainer_b_id"] == cancelling_trainer_id:
        # active_trades.pop(trade_id, None) # Option 1: Delete trade immediately
        trade_session["status"] = "cancelled" # Option 2: Mark as cancelled
        flash("Trade cancelled.", "info")
        # Potentially notify the other trainer if this were a real-time app
        # For now, the other trainer will see the "cancelled" status when they load the page.
        
        # Redirect to their own Pokemon list
        return redirect(url_for('pokemon_list', trainer_id=cancelling_trainer_id))
    else:
        flash("You cannot cancel this trade as you are not part of it.", "error")
        return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=cancelling_trainer_id))

@app.route('/confirm_trade_offer/<trade_id>/<int:confirming_trainer_id>')
def confirm_trade_offer(trade_id, confirming_trainer_id):
    trade_session = active_trades.get(trade_id)

    if not trade_session:
        flash("Trade session not found.", "error")
        return redirect(url_for('pokemon_list', trainer_id=confirming_trainer_id))

    # Check if the trade is in a state where confirmation is expected (i.e., both have offered).
    if trade_session["status"] not in ["pending_confirm", "awaiting_other_confirmation"]:
        flash("Trade is not currently awaiting confirmation (e.g., waiting for both offers).", "warning")
        return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))

    # Set confirmation flag for the current trainer if they are part of the trade.
    user_is_trainer_a = (trade_session["trainer_a_id"] == confirming_trainer_id)
    user_is_trainer_b = (trade_session["trainer_b_id"] == confirming_trainer_id)

    if user_is_trainer_a:
        if trade_session["trainer_a_confirmed"]: # Check if already confirmed
            flash("You (Trainer A) have already confirmed this trade.", "info")
            return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))
        trade_session["trainer_a_confirmed"] = True
    elif user_is_trainer_b:
        if trade_session["trainer_b_confirmed"]: # Check if already confirmed
            flash("You (Trainer B) have already confirmed this trade.", "info")
            return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))
        trade_session["trainer_b_confirmed"] = True
    else:
        # This should not happen if users follow UI links, but protects against manual URL manipulation.
        flash("You are not a participant authorized to confirm this trade.", "error")
        return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))

    # Check if both trainers have now confirmed the trade.
    if trade_session["trainer_a_confirmed"] and trade_session["trainer_b_confirmed"]:
        trade_session["status"] = "processing" # Mark as 'processing' while backend logic runs.
        flash("Both trainers confirmed. Processing trade...", "info") # Inform users.
        
        # Store pre-trade species names for simple evolution check by comparing species names later.
        # This is a UI-level check; the core evolution logic is in pokemon_trading.py.
        pkm_a_species_before = trade_session["pokemon_a_details"]["species"]
        pkm_b_species_before = trade_session["pokemon_b_details"]["species"]

        # Ensure all necessary components for the trade (trainer IDs, Pokemon IDs) are present.
        # This is a safeguard before calling the core trade execution function.
        if not all([trade_session["trainer_a_id"], trade_session["pokemon_a_id"], 
                    trade_session["trainer_b_id"], trade_session["pokemon_b_id"]]):
            trade_session["status"] = "failed" # Mark as 'failed' if critical info is missing.
            flash("Trade Failed! Critical information missing (e.g., one trainer or Pokemon ID not recorded in session).", "error")
            # Reset confirmations to allow potential correction, though typically a new trade would be needed.
            trade_session["trainer_a_confirmed"] = False
            trade_session["trainer_b_confirmed"] = False
            return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))

        # Execute the trade using the backend logic from pokemon_trading.py.
        trade_successful = execute_pokemon_trade(
            db_path=DATABASE, # DATABASE path is correctly resolved to the project root.
            trainer_a_id=trade_session["trainer_a_id"],
            pokemon_a_id=trade_session["pokemon_a_id"],
            trainer_b_id=trade_session["trainer_b_id"],
            pokemon_b_id=trade_session["pokemon_b_id"]
        )

        if trade_successful:
            trade_session["status"] = "completed"
            
            updated_pokemon_a_details = get_pokemon_details(trade_session["pokemon_a_id"])
            updated_pokemon_b_details = get_pokemon_details(trade_session["pokemon_b_id"])

            trade_session["pokemon_a_details_after_trade"] = updated_pokemon_a_details
            trade_session["pokemon_b_details_after_trade"] = updated_pokemon_b_details
            
            evo_msg_parts = []
            if updated_pokemon_a_details and updated_pokemon_a_details["species"] != pkm_a_species_before:
                evo_msg_parts.append(f"{pkm_a_species_before} (now Trainer {updated_pokemon_a_details['trainer_id']}'s) evolved to {updated_pokemon_a_details['species']}")
            if updated_pokemon_b_details and updated_pokemon_b_details["species"] != pkm_b_species_before:
                evo_msg_parts.append(f"{pkm_b_species_before} (now Trainer {updated_pokemon_b_details['trainer_id']}'s) evolved to {updated_pokemon_b_details['species']}")
            
            evo_msg = ". ".join(evo_msg_parts)
            flash(f"Trade Successful! {evo_msg if evo_msg else 'No evolutions occurred.'}", "success")

        else:
            trade_session["status"] = "failed"
            flash("Trade Failed! The Pokemon were not exchanged due to an issue with the trade execution logic.", "error")
        
        # Clear confirmations for this session as it's resolved (completed or failed)
        trade_session["trainer_a_confirmed"] = False
        trade_session["trainer_b_confirmed"] = False
    else:
        trade_session["status"] = "awaiting_other_confirmation"
        flash("Confirmation received. Waiting for the other trainer to confirm.", "info")

    return redirect(url_for('trade_interface', trade_id=trade_id, current_trainer_id=confirming_trainer_id))


if __name__ == '__main__':
    # Ensure the pokemon_trade.db exists and has data.
    if not os.path.exists(DATABASE):
        print(f"Database file not found at {DATABASE} (resolved to: {os.path.abspath(DATABASE)}).")
        print(f"Attempting to create database schema using 'setup_database' from 'pokemon_trading.py'...")
        try:
            from pokemon_trading import setup_database
            setup_database(DATABASE) # Create schema
            print(f"Database schema created at {DATABASE}. You now need to populate it with initial data.")
            print(f"To do this, go to the parent directory ('{parent_dir}') and run 'python pokemon_trading.py' script fully.")
        except ImportError:
            print(f"ERROR: Could not import 'setup_database' from 'pokemon_trading.py'. Ensure '{parent_dir}/pokemon_trading.py' exists.")
            print(f"Current sys.path: {sys.path}")
        except Exception as e:
            print(f"ERROR: An error occurred during initial DB setup: {e}")
            
    print(f"Flask app starting. Ensure '{DATABASE}' exists and is populated.")
    print(f"To populate with example data, navigate to '{parent_dir}' and run: python pokemon_trading.py")
    print(f"Access the app at http://127.0.0.1:5000/trainer/<trainer_id>/pokemon_list (e.g., /trainer/101/pokemon_list)")
    app.run(debug=True, port=5000)
