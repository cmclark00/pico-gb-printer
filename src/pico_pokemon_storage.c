#include "pico/stdlib.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/pio.h"

#include "globals.h"

#include "lwip/apps/fs.h"
#include "tusb_lwip_glue.h"
#include "pokemon_trading.h"
#include "pokemon_data.h"
#include "linkcable.h"
#include "websocket_server.h"

bool debug_enable = ENABLE_DEBUG;
bool speed_240_MHz = false;

uint8_t file_buffer[FILE_BUFFER_SIZE];              // buffer for rendering JSON responses

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

// Key button for reset
#ifdef PIN_KEY
static void key_callback(uint gpio, uint32_t events) {
    linkcable_reset();
    pokemon_trading_reset();
    LED_OFF;
}
#endif

// Web server endpoints
#define ROOT_PAGE     "/index.html"
#define STATUS_FILE   "/status.json"
#define POKEMON_FILE  "/pokemon.json"
#define LOGS_FILE     "/logs.json"
#define TRADE_FILE    "/trade.json"

// Simple HTML for Pokemon interface
static const char pokemon_html[] = 
"<!DOCTYPE html>"
"<html><head>"
"<title>Pokemon Storage System</title>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body { font-family: Arial, sans-serif; margin: 20px; background: #f0f8ff; }"
"h1 { color: #1e40af; text-align: center; }"
".status { background: #e0f2fe; padding: 15px; border-radius: 8px; margin: 10px 0; }"
".pokemon-card { background: #fff; border: 2px solid #fbbf24; border-radius: 10px; padding: 15px; margin: 10px; display: inline-block; min-width: 200px; }"
".pokemon-name { font-weight: bold; color: #dc2626; font-size: 18px; }"
".pokemon-info { margin: 5px 0; color: #374151; }"
".logs { background: #f3f4f6; padding: 10px; border-radius: 5px; max-height: 200px; overflow-y: auto; font-family: monospace; font-size: 12px; }"
"button { background: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 5px; cursor: pointer; margin: 5px; }"
"button:hover { background: #2563eb; }"
".refresh { text-align: center; margin: 20px; }"
"</style>"
"</head><body>"
"<h1>🎮 Pokemon Storage System 🎮</h1>"
"<div class='status' id='status'>Loading status...</div>"
"<div class='refresh'><button onclick='loadData()'>Refresh Data</button></div>"
"<h2>Stored Pokemon</h2>"
"<div id='pokemon-list'>Loading Pokemon...</div>"
"<h2>Trading Logs</h2>"
"<div class='logs' id='logs'>Loading logs...</div>"
"<script>"
"function loadData() {"
"  fetch('/status.json').then(r=>r.json()).then(data => {"
"    document.getElementById('status').innerHTML = "
"      `<strong>Status:</strong> ${data.status.trade_state}<br>`+"
"      `<strong>Stored Pokemon:</strong> ${data.status.stored_pokemon}/256<br>`+"
"      `<strong>Total Trades:</strong> ${data.status.total_trades}`;"
"  });"
"  fetch('/pokemon.json').then(r=>r.json()).then(data => {"
"    let html = '';"
"    data.pokemon.forEach(p => {"
"      html += `<div class='pokemon-card'>`+"
"        `<div class='pokemon-name'>${p.species}</div>`+"
"        `<div class='pokemon-info'>Level: ${p.level}</div>`+"
"        `<div class='pokemon-info'>Type: ${p.type1}/${p.type2}</div>`+"
"        `<div class='pokemon-info'>Trainer: ${p.trainer}</div>`+"
"        `<div class='pokemon-info'>Game: ${p.game}</div>`+"
"        `</div>`;"
"    });"
"    document.getElementById('pokemon-list').innerHTML = html || 'No Pokemon stored yet.';"
"  });"
"  fetch('/logs.json').then(r=>r.json()).then(data => {"
"    document.getElementById('logs').innerHTML = data.logs || 'No logs available.';"
"  });"
"}"
"loadData();"
"setInterval(loadData, 5000);"
"</script>"
"</body></html>";

// Real-time test HTML
static const char realtime_test_html[] = 
"<!DOCTYPE html>"
"<html><head>"
"<title>Pokemon Trading Protocol - Real-time Monitor</title>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:'Courier New',monospace;background:#1a1a1a;color:#00ff00;margin:0;padding:20px}"
".container{max-width:1200px;margin:0 auto}"
"h1{text-align:center;color:#00ffff;text-shadow:0 0 10px #00ffff}"
".status{background:#2a2a2a;border:2px solid #00ff00;border-radius:10px;padding:15px;margin-bottom:20px;text-align:center}"
".status.connected{border-color:#00ff00;box-shadow:0 0 20px rgba(0,255,0,0.3)}"
".status.disconnected{border-color:#ff0000;color:#ff0000;box-shadow:0 0 20px rgba(255,0,0,0.3)}"
".log-container{background:#000;border:2px solid #333;border-radius:10px;height:400px;overflow-y:auto;padding:15px;font-size:14px;line-height:1.4}"
".log-entry{margin-bottom:5px;padding:3px 0}"
".log-entry.protocol{color:#00ffff;font-weight:bold}"
".log-entry.trade{color:#ffff00}"
".log-entry.error{color:#ff0000;background:rgba(255,0,0,0.1);padding:5px;border-left:3px solid #ff0000}"
".log-entry.system{color:#ff00ff}"
".controls{margin-bottom:20px;text-align:center}"
".btn{background:#333;color:#00ff00;border:2px solid #00ff00;padding:10px 20px;margin:0 10px;cursor:pointer;border-radius:5px;font-family:inherit}"
".btn:hover{background:#00ff00;color:#000}"
".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:20px}"
".stat-card{background:#2a2a2a;border:1px solid #555;border-radius:8px;padding:15px;text-align:center}"
".stat-value{font-size:24px;font-weight:bold;color:#00ffff}"
".stat-label{color:#aaa;font-size:12px;margin-top:5px}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>🎮 Pokemon Trading Protocol Monitor 🎮</h1>"
"<div class='status' id='connectionStatus'>Connecting to WebSocket...</div>"
"<div class='stats'>"
"<div class='stat-card'><div class='stat-value' id='protocolCount'>0</div><div class='stat-label'>Protocol Messages</div></div>"
"<div class='stat-card'><div class='stat-value' id='tradeCount'>0</div><div class='stat-label'>Trade Events</div></div>"
"<div class='stat-card'><div class='stat-value' id='errorCount'>0</div><div class='stat-label'>Errors</div></div>"
"<div class='stat-card'><div class='stat-value' id='connectionTime'>--</div><div class='stat-label'>Connected Time</div></div>"
"</div>"
"<div class='controls'>"
"<button class='btn' onclick='clearLog()'>Clear Log</button>"
"<button class='btn' onclick='toggleAutoScroll()'>Toggle Auto-scroll</button>"
"<button class='btn' onclick='downloadLog()'>Download Log</button>"
"</div>"
"<div class='log-container' id='logContainer'>"
"<div class='log-entry system'>System starting up...</div>"
"</div></div>"
"<script>"
"let ws=null,autoScroll=true,protocolCount=0,tradeCount=0,errorCount=0,connectionStartTime=null,logEntries=[];"
"function connectWebSocket(){"
"try{"
"ws=new WebSocket('ws://192.168.7.1:8080');"
"ws.onopen=function(e){console.log('WebSocket connected');connectionStartTime=Date.now();updateConnectionStatus('Connected - Real-time monitoring active',true);addLogEntry('system','WebSocket connected successfully')};"
"ws.onmessage=function(e){try{const data=JSON.parse(e.data);handleWebSocketMessage(data)}catch(err){console.error('Error parsing WebSocket message:',err);addLogEntry('error','Failed to parse WebSocket message: '+err.message)}};"
"ws.onclose=function(e){console.log('WebSocket disconnected');updateConnectionStatus('Disconnected - Attempting to reconnect...',false);addLogEntry('system','WebSocket disconnected, attempting to reconnect...');setTimeout(connectWebSocket,3000)};"
"ws.onerror=function(e){console.error('WebSocket error:',e);addLogEntry('error','WebSocket error occurred')};"
"}catch(e){console.error('Failed to create WebSocket:',e);updateConnectionStatus('Connection failed - Retrying...',false);setTimeout(connectWebSocket,3000)}}"
"function handleWebSocketMessage(data){"
"const timestamp=new Date().toLocaleTimeString();"
"switch(data.type){"
"case 'protocol':protocolCount++;addLogEntry('protocol',`[${timestamp}] PROTOCOL: RX: ${data.rx} → TX: ${data.tx} (${data.state})`);break;"
"case 'trade_event':tradeCount++;addLogEntry('trade',`[${timestamp}] ${data.event}: ${data.message}`);break;"
"case 'pokemon_update':addLogEntry('system',`[${timestamp}] Pokemon data updated`);break;"
"case 'status_update':addLogEntry('system',`[${timestamp}] Status updated`);break;"
"default:addLogEntry('system',`[${timestamp}] Unknown message type: ${data.type}`)}"
"updateStats()}"
"function addLogEntry(type,message){"
"const logContainer=document.getElementById('logContainer');"
"const logEntry=document.createElement('div');"
"logEntry.className=`log-entry ${type}`;logEntry.textContent=message;"
"logContainer.appendChild(logEntry);logEntries.push({type,message,timestamp:Date.now()});"
"if(logEntries.length>1000){logEntries.shift();logContainer.removeChild(logContainer.firstChild)}"
"if(autoScroll)logContainer.scrollTop=logContainer.scrollHeight;"
"if(type==='error'){errorCount++;updateStats()}}"
"function updateConnectionStatus(message,connected){"
"const statusElement=document.getElementById('connectionStatus');"
"statusElement.textContent=message;statusElement.className=connected?'status connected':'status disconnected'}"
"function updateStats(){"
"document.getElementById('protocolCount').textContent=protocolCount;"
"document.getElementById('tradeCount').textContent=tradeCount;"
"document.getElementById('errorCount').textContent=errorCount;"
"if(connectionStartTime){"
"const elapsed=Math.floor((Date.now()-connectionStartTime)/1000);"
"const minutes=Math.floor(elapsed/60);const seconds=elapsed%60;"
"document.getElementById('connectionTime').textContent=`${minutes}:${seconds.toString().padStart(2,'0')}`}}"
"function clearLog(){document.getElementById('logContainer').innerHTML='';logEntries=[];protocolCount=0;tradeCount=0;errorCount=0;updateStats();addLogEntry('system','Log cleared')}"
"function toggleAutoScroll(){autoScroll=!autoScroll;addLogEntry('system',`Auto-scroll ${autoScroll?'enabled':'disabled'}`)}"
"function downloadLog(){"
"const logText=logEntries.map(entry=>`[${new Date(entry.timestamp).toLocaleString()}] ${entry.type.toUpperCase()}: ${entry.message}`).join('\\n');"
"const blob=new Blob([logText],{type:'text/plain'});const url=URL.createObjectURL(blob);"
"const a=document.createElement('a');a.href=url;a.download=`pokemon_trading_log_${new Date().toISOString().slice(0,19).replace(/:/g,'-')}.txt`;a.click();URL.revokeObjectURL(url)}"
"setInterval(updateStats,1000);connectWebSocket();"
"</script></body></html>";

static const char *cgi_options(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (!strcmp(pcParam[i], "debug")) {
            debug_enable = (!strcmp(pcValue[i], "on"));
        }
    }
    return STATUS_FILE;
}

static const char *cgi_pokemon_list(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    return POKEMON_FILE;
}

static const char *cgi_trade_logs(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    return LOGS_FILE;
}

static const char *cgi_trade_status(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    return TRADE_FILE;
}

static const char *cgi_delete_pokemon(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (!strcmp(pcParam[i], "index")) {
            size_t index = atoi(pcValue[i]);
            pokemon_delete_stored(index);
            break;
        }
    }
    return POKEMON_FILE;
}

static const char *cgi_send_pokemon(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (!strcmp(pcParam[i], "index")) {
            size_t index = atoi(pcValue[i]);
            pokemon_send_stored(index);
            break;
        }
    }
    return TRADE_FILE;
}

static const char *cgi_reset_trading(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    pokemon_trading_reset();
    return STATUS_FILE;
}

static const char *cgi_reset_usb_boot(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    if (debug_enable) reset_usb_boot(0, 0);
    return ROOT_PAGE;
}

static const char *cgi_diagnostics(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    memset(file_buffer, 0, sizeof(file_buffer));
    
    // Read raw GPIO states
    bool sck_state = gpio_get(2);   // Clock pin
    bool sin_state = gpio_get(0);   // Serial In pin (from Game Boy)
    bool sout_state = gpio_get(3);  // Serial Out pin (to Game Boy)
    
    // Check PIO FIFO status
    bool tx_fifo_empty = pio_sm_is_tx_fifo_empty(LINKCABLE_PIO, LINKCABLE_SM);
    bool rx_fifo_empty = pio_sm_is_rx_fifo_empty(LINKCABLE_PIO, LINKCABLE_SM);
    uint32_t rx_fifo_level = pio_sm_get_rx_fifo_level(LINKCABLE_PIO, LINKCABLE_SM);
    uint32_t tx_fifo_level = pio_sm_get_tx_fifo_level(LINKCABLE_PIO, LINKCABLE_SM);
    
    trade_session_t* session = pokemon_get_current_session();
    
    snprintf((char*)file_buffer, sizeof(file_buffer),
        "{\"diagnostics\":{"
        "\"gpio\":{\"sck\":%s,\"sin\":%s,\"sout\":%s},"
        "\"pio\":{\"tx_empty\":%s,\"rx_empty\":%s,\"rx_level\":%lu,\"tx_level\":%lu},"
        "\"session\":{\"state\":\"%s\",\"resets\":%lu},"
        "\"debug_enabled\":%s"
        "}}",
        sck_state ? "true" : "false",
        sin_state ? "true" : "false", 
        sout_state ? "true" : "false",
        tx_fifo_empty ? "true" : "false",
        rx_fifo_empty ? "true" : "false",
        rx_fifo_level,
        tx_fifo_level,
        trade_state_to_string(pokemon_get_trade_state()),
        session->error_count,
        debug_enable ? "true" : "false"
    );
    
    return "/diagnostics.json";
}

static const char *cgi_gpio_monitor(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    memset(file_buffer, 0, sizeof(file_buffer));
    
    // Take multiple GPIO readings to catch transitions
    char* buffer = (char*)file_buffer;
    size_t remaining = sizeof(file_buffer);
    
    int written = snprintf(buffer, remaining, "{\"gpio_samples\":[");
    buffer += written;
    remaining -= written;
    
    // Sample GPIO states rapidly to catch any activity
    for (int i = 0; i < 50 && remaining > 50; i++) {
        bool sck = gpio_get(2);
        bool sin = gpio_get(0);
        bool sout = gpio_get(3);
        
        if (i > 0) {
            written = snprintf(buffer, remaining, ",");
            buffer += written;
            remaining -= written;
        }
        
        written = snprintf(buffer, remaining, 
            "{\"sample\":%d,\"sck\":%s,\"sin\":%s,\"sout\":%s}",
            i, 
            sck ? "true" : "false",
            sin ? "true" : "false",
            sout ? "true" : "false"
        );
        buffer += written;
        remaining -= written;
        
        // Small delay between samples
        sleep_us(100);
    }
    
    written = snprintf(buffer, remaining, "]}");
    buffer += written;
    
    return "/gpio_monitor.json";
}

static const tCGI cgi_handlers[] = {
    { "/options",           cgi_options },
    { "/pokemon/list",      cgi_pokemon_list },
    { "/pokemon/delete",    cgi_delete_pokemon },
    { "/pokemon/send",      cgi_send_pokemon },
    { "/trade/logs",        cgi_trade_logs },
    { "/trade/status",      cgi_trade_status },
    { "/reset",             cgi_reset_trading },
    { "/reset_usb_boot",    cgi_reset_usb_boot },
    { "/diagnostics",       cgi_diagnostics },
    { "/gpio_monitor",      cgi_gpio_monitor }
};

int fs_open_custom(struct fs_file *file, const char *name) {
    static const char *on_off[]     = {"off", "on"};
    static const char *true_false[] = {"false", "true"};
    
    if (!strcmp(name, ROOT_PAGE) || !strcmp(name, "/")) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = (const char *)pokemon_html;
        file->len = strlen(pokemon_html);
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, "/realtime_test.html")) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = (const char *)realtime_test_html;
        file->len = strlen(realtime_test_html);
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, STATUS_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data  = file_buffer;
        file->len   = snprintf((char *)file_buffer, sizeof(file_buffer),
                               "{\"result\":\"ok\"," \
                               "\"options\":{\"debug\":\"%s\"}," \
                               "\"status\":{\"stored_pokemon\":%zu,\"total_trades\":%lu,\"trade_state\":\"%s\"},"\
                               "\"system\":{\"fast\":%s}}",
                               on_off[debug_enable],
                               pokemon_get_stored_count(),
                               total_trades,
                               trade_state_to_string(pokemon_get_trade_state()),
                               true_false[speed_240_MHz]);
        file->index = file->len;
        return 1;
    } 
    else if (!strcmp(name, POKEMON_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = file_buffer;
        
        // Generate Pokemon list JSON
        char *buffer = (char *)file_buffer;
        size_t remaining = sizeof(file_buffer);
        int written = snprintf(buffer, remaining, "{\"pokemon\":[");
        buffer += written;
        remaining -= written;
        
        pokemon_slot_t* pokemon_list = pokemon_get_stored_list();
        size_t count = pokemon_get_stored_count();
        bool first = true;
        
        for (size_t i = 0; i < MAX_STORED_POKEMON && remaining > 100; i++) {
            if (!pokemon_list[i].occupied) continue;
            
            if (!first) {
                written = snprintf(buffer, remaining, ",");
                buffer += written;
                remaining -= written;
            }
            first = false;
            
            const pokemon_data_t* pokemon = &pokemon_list[i].pokemon;
            written = snprintf(buffer, remaining,
                "{\"slot\":%zu,\"species\":\"%s\",\"nickname\":\"%s\",\"level\":%d,"
                "\"type1\":\"%s\",\"type2\":\"%s\",\"trainer\":\"%s\","
                "\"trainer_id\":%u,\"timestamp\":%lu,\"game\":\"%s\"}",
                i,
                pokemon_get_species_name(pokemon->core.species),
                pokemon->nickname,
                pokemon->core.level,
                pokemon_get_type_name(pokemon->core.type1),
                pokemon_get_type_name(pokemon->core.type2),
                pokemon->ot_name,
                pokemon->core.original_trainer_id,
                pokemon_list[i].timestamp,
                pokemon_list[i].game_version);
            buffer += written;
            remaining -= written;
        }
        
        written = snprintf(buffer, remaining, "]}");
        buffer += written;
        
        file->len = buffer - (char *)file_buffer;
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, LOGS_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = file_buffer;
        
        const char* logs = pokemon_get_trade_log();
        char* buffer = (char*)file_buffer;
        size_t remaining = sizeof(file_buffer);
        
        // Start JSON
        int written = snprintf(buffer, remaining, "{\"logs\":\"");
        buffer += written;
        remaining -= written;
        
        // Escape logs string for JSON
        if (logs && remaining > 10) {
            const char* src = logs;
            while (*src && remaining > 10) {
                if (*src == '\n') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = 'n';
                        remaining -= 2;
                    }
                } else if (*src == '\r') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = 'r';
                        remaining -= 2;
                    }
                } else if (*src == '"') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = '"';
                        remaining -= 2;
                    }
                } else if (*src == '\\') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = '\\';
                        remaining -= 2;
                    }
                } else {
                    *buffer++ = *src;
                    remaining--;
                }
                src++;
            }
        }
        
        // End JSON
        if (remaining > 3) {
            written = snprintf(buffer, remaining, "\"}");
            buffer += written;
        }
        
        file->len = buffer - (char *)file_buffer;
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, TRADE_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = file_buffer;
        
        trade_session_t* session = pokemon_get_current_session();
        const char* error = pokemon_get_last_error();
        
        char* buffer = (char*)file_buffer;
        size_t remaining = sizeof(file_buffer);
        
        // Start JSON
        int written = snprintf(buffer, remaining, 
                             "{\"trade_state\":\"%s\",\"error\":\"", 
                             trade_state_to_string(session->state));
        buffer += written;
        remaining -= written;
        
        // Escape error string for JSON
        if (error && remaining > 10) {
            const char* src = error;
            while (*src && remaining > 10) {
                if (*src == '\n') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = 'n';
                        remaining -= 2;
                    }
                } else if (*src == '\r') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = 'r';
                        remaining -= 2;
                    }
                } else if (*src == '"') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = '"';
                        remaining -= 2;
                    }
                } else if (*src == '\\') {
                    if (remaining > 2) {
                        *buffer++ = '\\';
                        *buffer++ = '\\';
                        remaining -= 2;
                    }
                } else {
                    *buffer++ = *src;
                    remaining--;
                }
                src++;
            }
        }
        
        // End JSON
        if (remaining > 50) {
            written = snprintf(buffer, remaining, "\",\"session_time\":%lu}", 
                             session->session_start_time);
            buffer += written;
        }
        
        file->len = buffer - (char *)file_buffer;
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, "/diagnostics.json")) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = file_buffer;
        
        // Read raw GPIO states
        bool sck_state = gpio_get(2);   // Clock pin
        bool sin_state = gpio_get(0);   // Serial In pin (from Game Boy)
        bool sout_state = gpio_get(3);  // Serial Out pin (to Game Boy)
        
        // Check PIO FIFO status
        bool tx_fifo_empty = pio_sm_is_tx_fifo_empty(LINKCABLE_PIO, LINKCABLE_SM);
        bool rx_fifo_empty = pio_sm_is_rx_fifo_empty(LINKCABLE_PIO, LINKCABLE_SM);
        uint32_t rx_fifo_level = pio_sm_get_rx_fifo_level(LINKCABLE_PIO, LINKCABLE_SM);
        
        trade_session_t* session = pokemon_get_current_session();
        
        file->len = snprintf((char*)file_buffer, sizeof(file_buffer),
            "{\"diagnostics\":{"
            "\"gpio\":{\"sck\":%s,\"sin\":%s,\"sout\":%s},"
            "\"pio\":{\"tx_empty\":%s,\"rx_empty\":%s,\"rx_level\":%lu},"
            "\"session\":{\"state\":\"%s\",\"resets\":%lu}"
            "}}",
            sck_state ? "true" : "false",
            sin_state ? "true" : "false", 
            sout_state ? "true" : "false",
            tx_fifo_empty ? "true" : "false",
            rx_fifo_empty ? "true" : "false",
            rx_fifo_level,
            trade_state_to_string(pokemon_get_trade_state()),
            session->error_count
        );
        
        file->index = file->len;
        return 1;
    }
    else if (!strcmp(name, "/gpio_monitor.json")) {
        memset(file, 0, sizeof(struct fs_file));
        file->data = file_buffer;
        
        // Take multiple GPIO readings to catch transitions
        char* buffer = (char*)file_buffer;
        size_t remaining = sizeof(file_buffer);
        
        int written = snprintf(buffer, remaining, "{\"gpio_samples\":[");
        buffer += written;
        remaining -= written;
        
        // Sample GPIO states rapidly to catch any activity
        for (int i = 0; i < 50 && remaining > 50; i++) {
            bool sck = gpio_get(2);
            bool sin = gpio_get(0);
            bool sout = gpio_get(3);
            
            if (i > 0) {
                written = snprintf(buffer, remaining, ",");
                buffer += written;
                remaining -= written;
            }
            
            written = snprintf(buffer, remaining, 
                "{\"sample\":%d,\"sck\":%s,\"sin\":%s,\"sout\":%s}",
                i, 
                sck ? "true" : "false",
                sin ? "true" : "false",
                sout ? "true" : "false"
            );
            buffer += written;
            remaining -= written;
            
            // Small delay between samples
            sleep_us(100);
        }
        
        written = snprintf(buffer, remaining, "]}");
        buffer += written;
        
        file->len = buffer - (char *)file_buffer;
        file->index = file->len;
        return 1;
    }
    
    return 0;
}

void fs_close_custom(struct fs_file *file) {
    (void)(file);
}

// Main loop
int main(void) {
    speed_240_MHz = set_sys_clock_khz(240000, false);

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

    // Initialize network stack
    init_lwip();
    wait_for_netif_is_up();
    dhcpd_init();
    dns_init();
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));

    // Initialize WebSocket server for real-time updates
    websocket_server_init();

    // Initialize link cable with Pokemon trading handler
    linkcable_init(link_cable_ISR);

    // Set up watchdog timer
    add_alarm_in_us(MS(300), link_cable_watchdog, NULL, true);

    LED_OFF;

    printf("Pokemon Storage System Initialized\n");
    printf("Connect Game Boy Color with link cable to start trading\n");
    printf("Web interface: http://192.168.7.1\n");
    printf("WebSocket: ws://192.168.7.1:8080\n");

    while (true) {
        // Process USB
        tud_task();
        // Process network traffic
        service_traffic();
        // Process WebSocket connections
        websocket_server_process();
        // Update Pokemon trading state machine
        pokemon_trading_update();
    }

    return 0;
} 