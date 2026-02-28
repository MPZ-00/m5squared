#!/usr/bin/env python3
"""
Generate C++ drive profile presets from Python definitions.

Reads m25_ecs_driveprofiles.py and generates profiles.h for ESP32 Arduino.
"""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

import m25_ecs_driveprofiles as profiles


def generate_profiles_header(output_path: str):
    """Generate profiles.h from m25_ecs_driveprofiles.py"""
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("""/*
 * profiles.h - Drive profile presets for M25 wheels
 * 
 * AUTO-GENERATED from m25_ecs_driveprofiles.py
 * DO NOT EDIT MANUALLY - run tools/generate_profiles.py to regenerate
 * 
 * Profile presets define motor response characteristics for different
 * driving styles (Standard, Active, Sensitive, etc.)
 */

#ifndef PROFILES_H
#define PROFILES_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Profile Parameter Structure
// ---------------------------------------------------------------------------
struct ProfileParams {
    uint8_t  max_torque;           // 10-100 (%)
    uint16_t max_speed_raw;        // mm/s (e.g., 2361 = 8.5 km/h)
    uint8_t  speed_bias;           // 10-50 (sensor sensitivity)
    uint8_t  slope_inc;            // 1-70 (startup/acceleration time)
    uint8_t  slope_dec;            // 1-70 (coasting/deceleration time)
    uint16_t p_factor;             // 1-999 (proportional gain)
    uint8_t  speed_factor;         // 1-100
    uint8_t  rotation_threshold;   // 1-150
    
    // Computed helper (read-only, not sent to wheel)
    float max_speed_kmh() const {
        return max_speed_raw * 0.0036f;  // mm/s to km/h
    }
};

// ---------------------------------------------------------------------------
// Complete Profile (2 assist levels per profile)
// ---------------------------------------------------------------------------
struct DriveProfile {
    uint8_t       id;              // Profile ID (0-5)
    const char*   name;            // Human-readable name
    ProfileParams level_1;         // Assist level 1 (Indoor/Normal)
    ProfileParams level_2;         // Assist level 2 (Outdoor/Higher assist)
};

// ---------------------------------------------------------------------------
// Profile Presets (PROGMEM - stored in flash, not RAM)
// ---------------------------------------------------------------------------
""")
        
        # Generate profile presets
        profile_defs = [
            ('Standard', profiles.PROFILES['Standard']),
            ('Active', profiles.PROFILES['Active']),
            ('Sensitive', profiles.PROFILES['Sensitive']),
            ('Soft', profiles.PROFILES['Soft']),
            ('SensitivePlus', profiles.PROFILES['SensitivePlus']),
        ]
        
        f.write("const DriveProfile PROFILE_PRESETS[] PROGMEM = {\n")
        
        for profile_name, profile_data in profile_defs:
            lvl1 = profile_data['level_1']
            lvl2 = profile_data['level_2']
            
            f.write(f"    // {profile_name}\n")
            f.write("    {\n")
            f.write(f"        .id = {profile_data['id']},\n")
            f.write(f'        .name = "{profile_name}",\n')
            f.write("        .level_1 = {\n")
            f.write(f"            .max_torque = {lvl1['max_torque']},\n")
            f.write(f"            .max_speed_raw = {lvl1['max_speed']},\n")
            f.write(f"            .speed_bias = {lvl1['speed_bias']},\n")
            f.write(f"            .slope_inc = {lvl1['slope_inc']},\n")
            f.write(f"            .slope_dec = {lvl1['slope_dec']},\n")
            f.write(f"            .p_factor = {lvl1['p_factor']},\n")
            f.write(f"            .speed_factor = {lvl1['speed_factor']},\n")
            f.write(f"            .rotation_threshold = {lvl1['rotation_threshold']}\n")
            f.write("        },\n")
            f.write("        .level_2 = {\n")
            f.write(f"            .max_torque = {lvl2['max_torque']},\n")
            f.write(f"            .max_speed_raw = {lvl2['max_speed']},\n")
            f.write(f"            .speed_bias = {lvl2['speed_bias']},\n")
            f.write(f"            .slope_inc = {lvl2['slope_inc']},\n")
            f.write(f"            .slope_dec = {lvl2['slope_dec']},\n")
            f.write(f"            .p_factor = {lvl2['p_factor']},\n")
            f.write(f"            .speed_factor = {lvl2['speed_factor']},\n")
            f.write(f"            .rotation_threshold = {lvl2['rotation_threshold']}\n")
            f.write("        }\n")
            f.write("    },\n")
        
        f.write("};\n\n")
        f.write(f"#define PROFILE_COUNT {len(profile_defs)}\n\n")
        
        # Helper functions
        f.write("""// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/**
 * Get profile by ID (0-5)
 * Returns nullptr if ID is invalid
 */
inline const DriveProfile* getProfileById(uint8_t id) {
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        if (pgm_read_byte(&PROFILE_PRESETS[i].id) == id) {
            return &PROFILE_PRESETS[i];
        }
    }
    return nullptr;
}

/**
 * Get profile by name
 * Returns nullptr if name not found
 */
inline const DriveProfile* getProfileByName(const char* name) {
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        const char* profile_name = (const char*)pgm_read_ptr(&PROFILE_PRESETS[i].name);
        if (strcmp_P(name, profile_name) == 0) {
            return &PROFILE_PRESETS[i];
        }
    }
    return nullptr;
}

/**
 * Load profile parameters from PROGMEM
 * 
 * Usage:
 *   ProfileParams params;
 *   loadProfileParams(&PROFILE_PRESETS[2], 1, &params);  // Sensitive, level 2
 */
inline void loadProfileParams(const DriveProfile* profile, uint8_t assist_level, ProfileParams* out) {
    if (profile == nullptr || out == nullptr) return;
    
    const ProfileParams* src = (assist_level == 0) 
        ? &profile->level_1 
        : &profile->level_2;
    
    // Copy from PROGMEM to RAM
    memcpy_P(out, src, sizeof(ProfileParams));
}

#endif // PROFILES_H
""")
    
    print(f"Generated: {output_path}")
    print(f"  - {len(profile_defs)} profiles defined")
    print(f"  - 2 assist levels per profile")
    print(f"  - {len(profile_defs) * 2} total parameter sets")


def main():
    """Generate profiles.h"""
    script_dir = Path(__file__).parent
    output_path = script_dir / '../esp32/arduino/remote_control/profiles.h'
    
    print("Generating profiles.h from m25_ecs_driveprofiles.py...")
    generate_profiles_header(str(output_path))
    print("Done!")


if __name__ == '__main__':
    main()
