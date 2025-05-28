# Pokemon Storage System for RP2040-Zero

This project transforms the Game Boy Printer emulator into a **Pokemon Storage System** that connects to a Game Boy Color via link cable and provides a modern web interface for managing your Pokemon collection.

## 🎮 Features

- **Pokemon Trading**: Connect your Game Boy Color and trade Pokemon from Red/Blue/Yellow
- **Web Interface**: Access via USB ethernet at `192.168.7.1` 
- **Storage**: Store up to 256 Pokemon with full metadata
- **Real-time Status**: Live trading status and comprehensive logging
- **Data Export**: View Pokemon details, stats, and trading history

## 🔧 Hardware Requirements

### RP2040-Zero Device
- **RP2040-Zero** microcontroller board (NOT Pico W - we use USB ethernet instead)
- Game Boy Color Link Cable (or compatible)
- Micro USB cable for power and data

### Wiring Diagram
```
Game Boy Link Cable → RP2040-Zero
Pin 2 (Serial Out)  → GPIO 14 (Configurable)
Pin 3 (Serial In)   → GPIO 15 (Configurable) 
Pin 5 (Clock)       → GPIO 16 (Configurable)
Pin 6 (Ground)      → GND
```

## 🚀 Setup & Installation

### 1. Flash the Firmware
1. Download `pico_gb_printer.uf2` from the build
2. Hold BOOTSEL button on RP2040-Zero while connecting USB
3. Copy the `.uf2` file to the mounted drive
4. Device will reboot automatically

### 2. Network Configuration
The RP2040-Zero appears as a **USB Ethernet adapter** when connected:

1. **Connect USB**: Plug RP2040-Zero into your computer via USB
2. **Network Detection**: Your OS should detect a new network interface
3. **IP Assignment**: The device creates a network at `192.168.7.0/24`
   - Device IP: `192.168.7.1` 
   - Your computer gets: `192.168.7.2` (via DHCP)

### 3. Access Web Interface
1. Open web browser
2. Navigate to: **`http://192.168.7.1`**
3. You should see the Pokemon Storage System interface

## 🎯 Usage

### Trading Pokemon
1. **Connect Game Boy**: Wire Game Boy Color link cable to RP2040-Zero
2. **Start Game**: Load Pokemon Red/Blue/Yellow on Game Boy
3. **Initiate Trade**: Go to Pokemon Center → Trade Center
4. **Monitor Web**: Watch real-time status at `192.168.7.1`
5. **Complete Trade**: Pokemon automatically stored when trade completes

### Web Interface Features
- **📊 Status Dashboard**: Current trade state, stored count, total trades
- **🎮 Pokemon Collection**: View all stored Pokemon with details
- **📝 Trading Logs**: Real-time log of all trading activity  
- **🔄 Auto-refresh**: Interface updates every 5 seconds

### JSON API Endpoints
- `GET /status.json` - System status and statistics
- `GET /pokemon.json` - Complete Pokemon collection data
- `GET /logs.json` - Trading logs and events
- `GET /trade.json` - Current trading session status

## 🔧 Technical Details

### USB Ethernet Implementation
This system uses **TinyUSB + LWIP** to create a USB ethernet gadget:
- **USB CDC-ECM/RNDIS**: Standard USB networking protocols
- **DHCP Server**: Automatically assigns IP addresses
- **HTTP Server**: Serves web interface and JSON APIs
- **No WiFi Required**: Works on basic RP2040-Zero hardware

### Pokemon Data Format
- **44-byte structure**: Compatible with original Game Boy format
- **Species Database**: All 151 Generation 1 Pokemon supported
- **Validation**: Checksum verification and data integrity checks
- **Metadata**: Timestamps, game version, trainer info

### Link Cable Protocol
- **PIO State Machine**: Hardware-accelerated serial communication
- **Game Boy Timing**: Compatible with original link cable timing
- **Trade Detection**: Automatic recognition of trading protocols
- **Error Handling**: Robust error recovery and logging

## 🛠️ Troubleshooting

### USB Ethernet Not Detected
- **Windows**: Install RNDIS drivers if needed
- **macOS/Linux**: Should work automatically with CDC-ECM
- **Driver Issues**: Try different USB ports or cables

### Cannot Access 192.168.7.1
1. Check network adapter is active
2. Verify IP assignment (should be 192.168.7.2)
3. Try `ping 192.168.7.1`
4. Disable other network interfaces temporarily

### Game Boy Connection Issues  
1. **Check Wiring**: Verify link cable connections
2. **Power Supply**: Ensure RP2040-Zero has stable power
3. **Game Compatibility**: Tested with Red/Blue/Yellow
4. **Cable Quality**: Try different link cable if available

### No Pokemon Detected
1. **Trading Protocol**: Ensure you're in Pokemon Center trade mode
2. **Game State**: Save game before trading attempts
3. **Logs**: Check `/logs.json` for error messages
4. **Reset**: Try resetting the device and game

## 📋 Game Compatibility

### ✅ Fully Supported
- Pokemon Red (EN)
- Pokemon Blue (EN) 
- Pokemon Yellow (EN)

### ⚠️ Limited Support
- Other language versions (may need adjustments)
- Pokemon Gold/Silver (different protocol)

### ❌ Not Supported
- Game Boy Advance Pokemon games
- Newer generation games

## 🔍 Development & Building

### Prerequisites
- Pico SDK 2.1.1+
- CMake 3.13+
- GCC ARM cross-compiler
- Git

### Build Instructions
```bash
git clone <repository>
cd pico-gb-printer
mkdir build && cd build
cmake ..
make
```

### Customization
- **GPIO Pins**: Modify `include/globals.h`
- **Storage Size**: Adjust `MAX_STORED_POKEMON` 
- **Web Interface**: Edit inline HTML in `pico_pokemon_storage.c`
- **Trading Protocol**: Modify `pokemon_trading.c`

## 📝 License

This project is based on the original pico-gb-printer and maintains compatibility with its licensing terms.

## 🤝 Contributing

Contributions welcome! Areas for improvement:
- Additional Pokemon game support
- Enhanced web interface features  
- Mobile app companion
- Cloud backup integration
- Trading statistics and analytics

---

**Happy Pokemon collecting! 🎮⚡** 