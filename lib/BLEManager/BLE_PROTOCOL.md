# BLE Protocol Documentation - Spice Dispenser

This document provides the technical details required to test and interface with the Spice Dispenser machine via Bluetooth Low Energy (BLE).

## 1. Connection Details

*   **Device Name:** `Spice Dispenser`
*   **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

### Characteristics

| Name | UUID | Type | Description |
| :--- | :--- | :--- | :--- |
| **COMMAND** | `beb5483e-36e1-4688-b7f5-ea07361b26a8` | **Write** | Send JSON commands to the machine. |
| **STATUS** | `d670f5e7-3f36-4c9c-b1cc-1365532587f1` | **Notify** | Receives real-time status, progress, and ACKs. |
| **SYNC** | `672a952d-8889-4824-9118-2e0e09252c84` | **Read/Write/Notify** | Used for bulk data (e.g., levels and spice manifest). |

---

## 2. Testing with LightBlue / nRF Connect

1.  Connect to the device named **"Spice Dispenser"**.
2.  Locate the **COMMAND** characteristic (`...26a8`).
3.  Set the write format to **UTF-8 String** (not Hex).
4.  Subscribe to the **STATUS** characteristic (`...87f1`) and **SYNC** characteristic (`...2c84`) to see responses.

---

## 3. Supported Commands (JSON to COMMAND char)

### 3.1 Handshake
Always send this first to register your app.
```json
{"type": "handshake", "uuid": "user-unique-id-123", "version": 1}
```
**Response (STATUS Characteristic):**
```json
{
  "type": "ack",
  "command": "handshake",
  "status": "ready",     // "ready" (configured), "unconfigured" (needs setup), or "error"
  "new_device": false    // true if machine is unconfigured
}
```

### 3.2 Ping
Test connection latency. Returns a `pong` message on STATUS.
```json
{"type": "ping"}
```

### 3.3 Dispense Recipe (By Custom Items)
Trigger a multi-ingredient dispense.
*   `name`: name of the spice to dispense (lookup and matching done by machine)
*   `grams`: weight to dispense
```json
{
  "type": "dispense",
  "items": [
    {"name": "BLACK", "grams": 1.0},
    {"name": "BLUE", "grams": 1.5}
  ]
}
```

### 3.4 Sync Recipes (Streamed / Persistent Cache)
Streams a list of user recipes to the machine's RAM to populate the local physical LCD menu. Max 30 recipes, 10 ingredients each. On successful completion (`sync_recipes_end`), the machine caches these recipes persistently to LittleFS so they survive disconnections and reboots, but get overwritten when another user connects and syncs.

*Key Abbreviations:* `n` = Name, `i` = Ingredients array, `g` = Grams.

**1. Start Sync:**
```json
{"type": "sync_recipes_start", "total": 5}
```
**Response (STATUS):** `{"type": "ack", "command": "sync_recipes_start", "status": "success"}`

**2. Stream Item (Repeat for each recipe, `index` 0 to N-1):**
```json
{
  "type": "sync_recipe_item",
  "index": 0,
  "id": "rec_1",
  "n": "Italian Mix",
  "i": [
    {"n": "Oregano", "g": 2.0},
    {"n": "Garlic", "g": 1.0}
  ]
}
```
**Response (STATUS):**
*Success:*
```json
{"type": "ack", "command": "sync_recipe_item", "index": 0, "status": "success"}
```
*Failure (if recipe contains a spice name not physically available in global physical slots):*
```json
{
  "type": "ack",
  "command": "sync_recipe_item",
  "index": 0,
  "status": "fail",
  "reason": "invalid_ingredient",
  "detail": "Saffron" // Name of the missing/unregistered spice
}
```

**3. End Sync (Triggers LittleFS file write on ESP32):**
```json
{"type": "sync_recipes_end"}
```
**Response (STATUS):** `{"type": "ack", "command": "sync_recipes_end", "status": "success"}`

### 3.5 Emergency Stop (Abort)
Stops all motors instantly, saves remaining spice level, and returns the machine home.
```json
{"type": "abort"}
```

### 3.6 Get Status
Force the machine to send its current state and progress on STATUS.
```json
{"type": "get_status"}
```

### 3.7 Get Levels
Request the current fill levels of all 20 slots. Responses are sent on SYNC.
```json
{"type": "get_levels"}
```
**Response (SYNC):**
```json
{
  "type": "levels",
  "data": {
    "1": 100,
    "2": 85,
    "3": 42
  }
}
```

### 3.8 Get Manifest
Request the full Spice Manifest (slot number, name, fill level, and optional expiry epoch for all 20 slots) to sync the app's database. Responses are sent on SYNC.
```json
{"type": "get_manifest"}
```
**Response Stream (SYNC):**
1. Start Stream: `{"type": "manifest_start", "total": 20}`
2. Streaming Item (for slots 1 to 20):
```json
{"type": "manifest_item", "s": 1, "n": "Cinnamon", "l": 100, "e": 1798675200}
```
*Note:* `e` is the Unix Epoch Expiry Date timestamp (seconds since Jan 1, 1970). It is `0` if not set or unspecified.
3. End Stream: `{"type": "manifest_end"}`

### 3.9 Update Slot (Global Configuration)
Update the spice name and optional expiry date for a specific slot. This configures the global machine state and persists to persistent memory.
```json
{
  "type": "update_slot",
  "slot": 3,
  "name": "Smoked Paprika",
  "expiry": 1798675200 // Optional: Unix epoch expiry timestamp (can use key "e" as abbreviation)
}
```

### 3.10 Refill Slot
Resets the fill level of a specific slot to 100% and optionally updates its expiry date. Persists to LittleFS.
```json
{
  "type": "refill",
  "slot": 4,
  "expiry": 1798675200 // Optional: Unix epoch expiry timestamp for the newly refilled spice (can use key "e" as abbreviation)
}
```

### 3.11 Toggle Simulation
Enable/Disable Simulation Mode (mock hardware) remotely.
```json
{"type": "toggle_sim"}
```

### 3.12 Print Spices (Debug)
Dumps the current global persistent spice configuration to the ESP32 Serial Monitor.
```json
{"type": "print_spices"}
```

### 3.13 Factory Reset
Wipes the persistent LittleFS database (including global spices) and restarts the ESP32.
```json
{"type": "factory_reset"}
```

---

## 4. Machine Responses (STATUS Characteristic)

### Status Update (Periodic or on request)
```json
{
  "type": "status",
  "state": "busy",
  "active_recipe": "Remote",
  "progress": 45
}
```

### Acknowledgment (ACK)
```json
{
  "type": "ack",
  "command": "dispense",
  "status": "success"
}
```

### Alerts
```json
{
  "type": "alert",
  "code": "low_spice",
  "slot": 4
}
```
