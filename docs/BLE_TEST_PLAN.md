# Dev Board BLE Validation Test Plan

This document outlines a structured testing procedure to validate the ESP32 firmware's BLE module, specifically focusing on the new Global Spices + User Profile persistence architecture. Since you are testing on an ESP32 dev board without the physical hardware attached, the `SIMULATION_MODE` will handle mock hardware responses.

## Prerequisites
1.  Flash the ESP32 dev board with the current firmware.
2.  Open the Serial Monitor (Baud Rate: 115200). 
    *   **First Boot Expected:** `No global spice config found. Using factory defaults. Machine unconfigured.`
3.  Ensure `SIMULATION_MODE` is set to `1` in `Configuration.h`.
4.  Open your BLE Scanner App. Note: The machine requests an MTU of 512, but `get_levels` is now chunked, so it will work even if MTU negotiation fails.

---

## Test 1: Connection & Initial Handshake (Unconfigured Machine)
**Goal:** Verify the machine recognizes a new app UUID and flags the machine as unconfigured (blocking dispenses until setup is complete).

1.  **Connect:** Connect to the BLE device named `Spice Dispenser`.
2.  **Send JSON (Write to COMMAND char):**
    ```json
    {"type": "handshake", "uuid": "test-uuid-001", "version": 1}
    ```
3.  **Expected Outcome:**
    *   *BLE Notify (STATUS char):*
        ```json
        {"type":"ack","command":"handshake","status":"unconfigured","new_device":true}
        ```

---

## Test 2: Unconfigured Dispense Rejection
**Goal:** Prove the machine rejects dispensing if the global spice setup isn't finalized.

1.  **Send JSON:**
    ```json
    {
      "type": "dispense",
      "items": [{"slot": 1, "grams": 5.0}]
    }
    ```
2.  **Expected Outcome:**
    *   *BLE Notify:* `{"type":"ack","command":"dispense","status":"fail","reason":"unconfigured"}`

---

## Test 3: Slot Update & Global Configuration
**Goal:** Verify that configuring a slot saves to the GLOBAL config and marks the machine as configured.

1.  **Send JSON:**
    ```json
    {"type": "update_slot", "slot": 3, "name": "Custom Basil"}
    ```
2.  **Expected Outcome:**
    *   *Serial:* `[DEBUG] Updating slot 3 to Custom Basil. Saving to GLOBAL profile.` followed by `Global Spices saved to LittleFS.`
    *   *BLE Notify:* `{"type":"ack","command":"update_slot","status":"success"}`
    *   *Note:* The machine is now considered "Configured" for future handshakes.

---

## Test 4: Get Levels Validation (Chunked Data)
**Goal:** Verify that the 20 slots are returned safely across 4 chunked notifications.

1.  **Send JSON:**
    ```json
    {"type": "get_levels"}
    ```
2.  **Expected Outcome:**
    *   *BLE Notify (SYNC char):* You will receive 4 distinct JSON notifications rapidly.
    *   Look at `chunk` 1. It will contain slots 1-5. Slot 3 should show `"name": "Custom Basil"`.

---

## Test 5: Syncing Recipes to User Profile (Add & Update)
**Goal:** Verify the app can push a recipe list specifically to `test-uuid-001`.

1.  **Send JSON:**
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
        },
        {
          "id": "recipe_beta_2",
          "name": "Spicy Taco",
          "ingredients": [
            {"slot": 1, "grams": 10.0}
          ]
        }
      ]
    }
    ```
2.  **Expected Outcome:**
    *   *Serial:* `Profile test-uuid-001 saved to LittleFS`.
    *   *BLE Notify:* `{"type":"ack","command":"sync_recipes","status":"success","count":2}`

---

## Test 6: Debug Print Profile
**Goal:** Verify the recipes were stored correctly in RAM.

1.  **Send JSON:**
    ```json
    {"type": "print_profile"}
    ```
2.  **Expected Outcome:**
    *   *Serial:* You should see a formatted table `--- CURRENT PROFILE DEBUG ---` listing `Quick Garlic Mix` and `Spicy Taco` with their correct IDs and ingredients.

---

## Test 7: Recipe-ID Dispensing & Global Level Tracking
**Goal:** Trigger a dispense using a stored recipe ID. Verify levels drop in the global config.

1.  **Send JSON:**
    ```json
    {
      "type": "dispense",
      "recipe_id": "recipe_alpha_1"
    }
    ```
2.  **Expected Outcome:**
    *   *Serial:* `[DEBUG] Loading recipe 'Quick Garlic Mix' from profile.`
    *   *Serial:* Logs should print "Dispensing..." only once per ingredient.
    *   *Serial:* You will see `Global Spices saved to LittleFS` after each ingredient, proving the level drop is tracked globally.

---

## Test 8: Accurate Abort During Dispensing
**Goal:** Verify the emergency stop calculates only the *actually dispensed* amount.

1.  **Action:** Send a `dispense` command for a very large amount (e.g., 90 grams of slot 1).
2.  **Action:** Wait exactly 2 seconds while Serial prints `Dispensing...`, then send:
    ```json
    {"type": "abort"}
    ```
3.  **Expected Outcome:**
    *   *Serial:* `BLE: ABORT COMMAND`.
    *   *Serial:* Look for `[DEBUG] Aborted. Dispensed approx X.Xg of Paprika`. It should be a fraction of the 90g, not the full 90g.
    *   *Serial:* `Global Spices saved to LittleFS` (saving that fraction).
    *   *BLE Notify:* `{"type":"ack","command":"abort","status":"success"}`.

---

## Test 9: Low Spice Alert & Refill
**Goal:** Verify the alert system.

1.  **Send JSON (Drains slot 13 below 15%):**
    ```json
    {
      "type": "dispense",
      "items": [
        {"slot": 13, "grams": 90.0}
      ]
    }
    ```
2.  **Expected Outcome (Alert):**
    *   *BLE Notify:* Once dispensing finishes, you should receive:
        ```json
        {"type":"alert","code":"low_spice","slot":13}
        ```
3.  **Send JSON (Refill):**
    ```json
    {"type": "refill", "slot": 13}
    ```
4.  **Expected Outcome (Refill):**
    *   *Serial:* `[DEBUG] Refilling slot 13 to 100%. Saving to GLOBAL profile.`

---

## Test 10: Factory Reset
**Goal:** Prove we can wipe the system back to absolute factory conditions.

1.  **Send JSON:**
    ```json
    {"type": "factory_reset"}
    ```
2.  **Expected Outcome:**
    *   *Serial:* `Formatting LittleFS... Please wait.`
    *   *Serial:* The ESP32 will reboot, and you will see `No global spice config found. Using factory defaults. Machine unconfigured.` again, taking you back to Test 1.