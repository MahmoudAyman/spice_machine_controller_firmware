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
| **SYNC** | `672a952d-8889-4824-9118-2e0e09252c84` | **Read/Write** | Used for bulk data (e.g., full level list). |

---

## 2. Testing with LightBlue / nRF Connect

1.  Connect to the device named **"Spice Dispenser"**.
2.  Locate the **COMMAND** characteristic (`...26a8`).
3.  Set the write format to **UTF-8 String** (not Hex).
4.  Subscribe to the **STATUS** characteristic (`...87f1`) to see responses.

---

## 3. Supported Commands (JSON)

### 3.1 Handshake
Always send this first to register your app.
```json
{"type": "handshake", "uuid": "user-unique-id-123", "version": 1}
```

### 3.2 Ping
Test connection latency. Returns a `pong` message.
```json
{"type": "ping"}
```

### 3.3 Dispense Recipe
Trigger a multi-ingredient dispense.
*   `slot`: 1 to 20
*   `grams`: weight to dispense
```json
{
  "type": "dispense",
  "items": [
    {"slot": 1, "grams": 2.5},
    {"slot": 5, "grams": 1.0}
  ]
}
```

### 3.4 Emergency Stop (Abort)
Stops all motors instantly.
```json
{"type": "abort"}
```

### 3.5 Get Status
Force the machine to send its current state and progress.
```json
{"type": "get_status"}
```

### 3.6 Get Levels
Request the current fill levels of all 20 slots.
```json
{"type": "get_levels"}
```

### 3.7 Toggle Simulation
Enable/Disable Simulation Mode (mock hardware) remotely.
```json
{"type": "toggle_sim"}
```

### 3.8 Update Slot (Draft)
Update the spice name for a specific slot (Persistence coming in Phase 4).
```json
{"type": "update_slot", "slot": 3, "name": "Smoked Paprika"}
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
