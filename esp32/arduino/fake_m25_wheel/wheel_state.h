/**
 * Wheel State Manager
 * 
 * Manages simulated wheel state including speed, battery, and distance tracking
 */

#ifndef WHEEL_STATE_H
#define WHEEL_STATE_H

#include <Arduino.h>

// Simulated wheel state
struct WheelState {
    int16_t currentSpeed = 0;      // Current speed (-32768 to +32767 raw units)
    int16_t lastSpeed = 0;         // Previous speed (for change detection)
    int batteryLevel = 100;        // Battery percentage (0-100)
    int assistLevel = 1;           // Assist level (0-2)
    bool hillHold = false;         // Hill hold active
    int driveProfile = 0;          // Drive profile (0 = standard)
    long wheelRotation = 0;        // Total wheel rotations
    float distanceTraveled = 0.0;  // Distance in meters (approx 2m per rotation)
    unsigned long lastSpeedUpdate = 0;  // Last speed change timestamp
    
    /**
     * Simulate wheel rotation and update distance
     */
    void simulateRotation(int rotations) {
        wheelRotation += rotations;
        distanceTraveled += rotations * 2.0;  // Assuming ~2m per rotation
        
        Serial.print("Wheel turned by ");
        Serial.print(rotations);
        Serial.print(" rotation(s) - Total: ");
        Serial.print(wheelRotation);
        Serial.print(" rotations, ");
        Serial.print(distanceTraveled);
        Serial.println(" m traveled");
        
        // Simulate small battery drain with distance
        if (wheelRotation % 100 == 0 && batteryLevel > 0) {
            batteryLevel--;
            Serial.print("Battery drained to ");
            Serial.print(batteryLevel);
            Serial.println("% (long journey)");
        }
    }
    
    /**
     * Reset wheel rotation counter
     */
    void resetRotation() {
        wheelRotation = 0;
        distanceTraveled = 0.0;
        Serial.println("Wheel rotation counter reset");
    }
    
    /**
     * Print current status
     */
    void printStatus() {
        Serial.println("\n=== Wheel Status ===");
        Serial.print("Speed: ");
        Serial.print(currentSpeed);
        Serial.println(" (raw units)");
        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");
        Serial.print("Assist Level: ");
        Serial.println(assistLevel);
        Serial.print("Drive Profile: ");
        Serial.println(driveProfile);
        Serial.print("Hill Hold: ");
        Serial.println(hillHold ? "ON" : "OFF");
        Serial.print("Wheel Rotations: ");
        Serial.println(wheelRotation);
        Serial.print("Distance: ");
        Serial.print(distanceTraveled);
        Serial.println(" m");
        Serial.println("===================\n");
    }
    
    /**
     * Simulate slow battery drain over time
     */
    void updateBattery() {
        if (batteryLevel > 5) {
            batteryLevel--;
            Serial.printf("Battery drained to %d%% (time)\n", batteryLevel);
        } else if (batteryLevel > 0) {
            batteryLevel--;
            Serial.printf("WARNING: Low battery! (%d%%)\n", batteryLevel);
        }
    }
};

#endif // WHEEL_STATE_H
