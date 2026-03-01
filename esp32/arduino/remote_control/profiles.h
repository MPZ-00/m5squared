/*
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
const DriveProfile PROFILE_PRESETS[] PROGMEM = {
    // Standard
    {
        .id = 1,
        .name = "Standard",
        .level_1 = {
            .max_torque = 45,
            .max_speed_raw = 1111,
            .speed_bias = 20,
            .slope_inc = 28,
            .slope_dec = 56,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        },
        .level_2 = {
            .max_torque = 75,
            .max_speed_raw = 2361,
            .speed_bias = 20,
            .slope_inc = 42,
            .slope_dec = 42,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        }
    },
    // Active
    {
        .id = 4,
        .name = "Active",
        .level_1 = {
            .max_torque = 45,
            .max_speed_raw = 1250,
            .speed_bias = 30,
            .slope_inc = 56,
            .slope_dec = 56,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        },
        .level_2 = {
            .max_torque = 90,
            .max_speed_raw = 2361,
            .speed_bias = 20,
            .slope_inc = 70,
            .slope_dec = 28,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        }
    },
    // Sensitive
    {
        .id = 2,
        .name = "Sensitive",
        .level_1 = {
            .max_torque = 60,
            .max_speed_raw = 1111,
            .speed_bias = 30,
            .slope_inc = 42,
            .slope_dec = 42,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        },
        .level_2 = {
            .max_torque = 95,
            .max_speed_raw = 2361,
            .speed_bias = 40,
            .slope_inc = 42,
            .slope_dec = 28,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        }
    },
    // Soft
    {
        .id = 3,
        .name = "Soft",
        .level_1 = {
            .max_torque = 35,
            .max_speed_raw = 833,
            .speed_bias = 20,
            .slope_inc = 28,
            .slope_dec = 56,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        },
        .level_2 = {
            .max_torque = 50,
            .max_speed_raw = 2361,
            .speed_bias = 10,
            .slope_inc = 28,
            .slope_dec = 56,
            .p_factor = 5,
            .speed_factor = 11,
            .rotation_threshold = 2
        }
    },
    // SensitivePlus
    {
        .id = 5,
        .name = "SensitivePlus",
        .level_1 = {
            .max_torque = 65,
            .max_speed_raw = 1389,
            .speed_bias = 50,
            .slope_inc = 70,
            .slope_dec = 28,
            .p_factor = 5,
            .speed_factor = 50,
            .rotation_threshold = 1
        },
        .level_2 = {
            .max_torque = 100,
            .max_speed_raw = 2361,
            .speed_bias = 50,
            .slope_inc = 70,
            .slope_dec = 20,
            .p_factor = 5,
            .speed_factor = 50,
            .rotation_threshold = 1
        }
    },
};

#define PROFILE_COUNT 5

// ---------------------------------------------------------------------------
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
