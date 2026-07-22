#ifndef MOTION_CONTROLLER_H
#define MOTION_CONTROLLER_H

#include <Arduino.h>
#include <AccelStepper.h>

// Motion tracking states
enum RotationState {
    ROT_IDLE,
    ROT_EXITING,      // Leaving the current limit switch trigger
    ROT_ENTERING,     // Searching for the next switch trigger
    ROT_ALIGNING      // Driving the slow dynamic release alignment
};

enum BootAlignState {
    BOOT_ALIGN_IDLE,
    BOOT_ALIGN_INIT,
    BOOT_ALIGN_EXITING,
    BOOT_ALIGN_ENTERING,
    BOOT_ALIGN_ALIGNING,
    BOOT_ALIGN_READ_AND_MATCH
};

enum HomingState {
    HOME_IDLE,
    HOME_INIT,
    HOME_EXITING,   // Move off current position if already pressed
    HOME_ENTERING,  // Move fast until homing switch triggers
    HOME_ALIGNING,  // Move slow until homing switch releases
    HOME_ALIGN_NEXT_SLOT_ENTERING, // Move fast until alignment limit switch triggers
    HOME_ALIGN_NEXT_SLOT_ALIGNING  // Move slow until alignment limit switch releases
};

// Core APIs
void initMotionController();
void startRotationToSlot(int targetSlotIndex);
bool tickRotation(); // Returns true when target alignment is complete
void startBootRecoveryAlignment();
bool tickBootRecovery(int &matchedSlotIndex); // Returns true when start slot is found
void startHoming();  // Starts the homing process on the homing limit switch
bool tickHoming();   // Processes homing state machine, returns true when absolute home alignment is complete

// Status Getters and Setters
RotationState getRotationState();
int getSlotsRemainingToMove();
int getCurrentSlotIndex();
void setCurrentSlotIndex(int slotIndex);
void disableStepperMotor();
void enableStepperMotor();

#endif
