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

### 3.4 Sync Recipes (Volatile)
Syncs a list of user recipes to the machine's RAM to populate the local physical LCD menu. Max 30 recipes, 10 ingredients each.
**Note:** These recipes are volatile and reset on reboot.
```json
{
  "type": "sync_recipes",
  "recipes": [
    {
      "id": "recipe_1",
      "name": "Quick Garlic Mix",
      "ingredients": [
        {"name": "Garlic Powder", "grams": 2.5}
      ]
    }
  ]
}
```

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
Request the full Spice Manifest (slot number, name, and fill level for all 20 slots) to sync the app's database. Responses are sent on SYNC.
```json
{"type": "get_manifest"}
```
**Response (SYNC):**
```json
{
  "type": "manifest",
  "spices": [
    {"slot": 1, "name": "Cinnamon", "level": 100},
    {"slot": 2, "name": "Oregano", "level": 85}
  ]
}
```

### 3.9 Update Slot (Global Configuration)
Update the spice name for a specific slot. This configures the global machine state and persists to persistent memory.
```json
{"type": "update_slot", "slot": 3, "name": "Smoked Paprika"}
```

### 3.10 Refill Slot
Resets the fill level of a specific slot to 100% and persists to persistent memory.
```json
{"type": "refill", "slot": 4}
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
