# BLE Protocol Documentation - Spice Dispenser

This document provides the technical details required to test and interface with the Spice Dispenser machine via Bluetooth Low Energy (BLE).

## 1. Connection Details

*   **Device Name:** `Spice Dispenser`
*   **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
*   **MTU:** The device requests an MTU of 512 bytes on connection.

### Characteristics

1.  **COMMAND (Write)**
    *   **UUID:** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
    *   **Purpose:** Send JSON commands to the machine.
2.  **STATUS (Notify)**
    *   **UUID:** `a8a54823-36e1-4688-b7f5-ea07361b26a8`
    *   **Purpose:** Receive asynchronous status updates, progress, and alerts.
3.  **SYNC (Notify/Read)**
    *   **UUID:** `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
    *   **Purpose:** Receive bulk data like current spice levels.

---

## 2. Command Reference (Write to COMMAND char)

All commands must be valid JSON strings. Due to BLE MTU limits, the ESP32 buffers incoming commands; ensure your scanner app sends long commands in chunks if it doesn't support large MTUs.

### 2.1 Handshake (Required)
Initiates the connection, passes the App UUID, and checks if the machine is configured.
```json
{
  "type": "handshake",
  "uuid": "your-unique-app-uuid",
  "version": 1
}
```
**Response (STATUS):**
```json
{
  "type": "ack",
  "command": "handshake",
  "status": "success", // Or "unconfigured" if global setup is needed
  "new_device": false 
}
```

### 2.2 Dispense
Triggers the dispensing process. Can use raw items or a stored `recipe_id`.

**By Recipe ID:**
```json
{
  "type": "dispense",
  "recipe_id": "recipe_alpha_1"
}
```

**By Custom Items:**
```json
{
  "type": "dispense",
  "items": [
    {"slot": 1, "grams": 5.0},
    {"slot": 4, "grams": 2.5}
  ]
}
```

### 2.3 Sync Recipes
Overwrites the active user's profile with a new list of recipes. Maximum 30 recipes, 10 ingredients each.
```json
{
  "type": "sync_recipes",
  "recipes": [
    {
      "id": "recipe_alpha_1",
      "name": "Quick Garlic Mix",
      "ingredients": [
        {"slot": 13, "grams": 5.0},
        {"slot": 15, "grams": 2.0}
      ]
    }
  ]
}
```

### 2.4 Update Slot (Global Configuration)
Updates the name of a spice in a specific slot. This configures the global machine state.
```json
{
  "type": "update_slot",
  "slot": 4,
  "name": "Smoked Paprika"
}
```

### 2.5 Refill Slot
Resets the fill level of a specific slot to 100%.
```json
{
  "type": "refill",
  "slot": 4
}
```

### 2.6 Get Levels
Requests the current fill levels of all 20 slots. The response is sent as a compact Key-Value map to the SYNC characteristic.
```json
{
  "type": "get_levels"
}
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

### 2.7 Abort
Immediately stops the current dispensing process, accurately deducts the partial amount dispensed from the global level, and returns the machine home.
```json
{
  "type": "abort"
}
```

### 2.8 Debug Commands
*   **Ping:** `{"type": "ping"}` - Returns a pong with a timestamp.
*   **Toggle Simulation:** `{"type": "toggle_sim"}` - Enables/disables mock hardware.
*   **Print Profile:** `{"type": "print_profile"}` - Dumps the active user's recipes to the ESP32 Serial Monitor.
*   **Factory Reset:** `{"type": "factory_reset"}` - Wipes LittleFS and reboots the ESP32.

---

## 3. Notifications (Read from STATUS char)

### System Status (Pushed automatically during operation)
```json
{
  "type": "status",
  "state": "busy",
  "active_recipe": "Remote Custom",
  "progress": 45
}
```

### Alerts (Pushed on errors)
```json
{
  "type": "alert",
  "code": "low_spice", 
  "slot": 4
}
```
*(Other codes: `wrong_spice`)*