"""
M25 Drive Profile Defaults - What the factory presets actually look like.

Ever wonder what "Sensitive" vs "Active" actually means in terms of
raw motor parameters? Now you know. Spoiler: it's torque curves and
speed biases. Very exciting stuff for a 39C3 audience.

Extracted from DriveProfile - the Germans do love their enums.
"""

from m25_protocol_data import (
    PROFILE_ID_CUSTOMIZED, PROFILE_ID_STANDARD, PROFILE_ID_SENSITIVE,
    PROFILE_ID_SOFT, PROFILE_ID_ACTIVE, PROFILE_ID_SENSITIVE_PLUS,
    PROFILE_NAMES,
    MAX_SPEED_VALUES, MAX_SUPPORT_SPEED, MAX_SPEED_LEARNING,
    SPEED_BIAS_VALUES, SLOPE_VALUES
)

# Aliases for clarity
SLOPE_INC_VALUES = SLOPE_VALUES  # Startup time
SLOPE_DEC_VALUES = SLOPE_VALUES  # Coasting time
MAX_SPEED = MAX_SUPPORT_SPEED    # 8.5 km/h


def speed_raw_to_kmh(raw):
    """Convert raw speed value (mm/s) to km/h"""
    return raw * 0.0036

def speed_kmh_to_raw(kmh):
    """Convert km/h to raw speed value (mm/s)"""
    return int(kmh / 0.0036)

def get_speed_index(raw):
    """Get the index of a speed value in the speed range"""
    try:
        return MAX_SPEED_VALUES.index(raw)
    except ValueError:
        # Find nearest
        return min(range(len(MAX_SPEED_VALUES)), key=lambda i: abs(MAX_SPEED_VALUES[i] - raw))


# Default profile parameters
# Keys: max_torque (%), max_speed (raw mm/s), speed_bias, slope_inc, slope_dec,
#       p_factor, speed_factor, rotation_threshold

PROFILES = {
    'Standard': {
        'id': PROFILE_ID_STANDARD,
        'level_1': {
            'max_torque': 45,
            'max_speed': 1111,           # 4.0 km/h
            'speed_bias': 20,            # Sensor sensitivity
            'slope_inc': 28,             # Startup time (longer)
            'slope_dec': 56,             # Coasting time (medium)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
        'level_2': {
            'max_torque': 75,
            'max_speed': 2361,           # 8.5 km/h
            'speed_bias': 20,
            'slope_inc': 42,             # Startup time (medium)
            'slope_dec': 42,             # Coasting time (medium)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
    },

    'Active': {
        'id': PROFILE_ID_ACTIVE,
        'level_1': {
            'max_torque': 45,
            'max_speed': 1250,           # 4.5 km/h
            'speed_bias': 30,            # Sensor sensitivity (medium)
            'slope_inc': 56,             # Startup time (medium-short)
            'slope_dec': 56,             # Coasting time (medium-short)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
        'level_2': {
            'max_torque': 90,
            'max_speed': 2361,           # 8.5 km/h
            'speed_bias': 20,
            'slope_inc': 70,             # Startup time (shortest/fastest)
            'slope_dec': 28,             # Coasting time (longer)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
    },

    'Sensitive': {
        'id': PROFILE_ID_SENSITIVE,
        'level_1': {
            'max_torque': 60,
            'max_speed': 1111,           # 4.0 km/h
            'speed_bias': 30,            # Sensor sensitivity (medium)
            'slope_inc': 42,             # Startup time (medium)
            'slope_dec': 42,             # Coasting time (medium)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
        'level_2': {
            'max_torque': 95,
            'max_speed': 2361,           # 8.5 km/h
            'speed_bias': 40,            # Sensor sensitivity (high)
            'slope_inc': 42,             # Startup time (medium)
            'slope_dec': 28,             # Coasting time (longer)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
    },

    'Soft': {
        'id': PROFILE_ID_SOFT,
        'level_1': {
            'max_torque': 35,
            'max_speed': 833,            # 3.0 km/h
            'speed_bias': 20,            # Sensor sensitivity (low-medium)
            'slope_inc': 28,             # Startup time (longer)
            'slope_dec': 56,             # Coasting time (medium-short)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
        'level_2': {
            'max_torque': 50,
            'max_speed': 2361,           # 8.5 km/h
            'speed_bias': 10,            # Sensor sensitivity (lowest)
            'slope_inc': 28,             # Startup time (longer)
            'slope_dec': 56,             # Coasting time (medium-short)
            'p_factor': 5,
            'speed_factor': 11,
            'rotation_threshold': 2,
        },
    },

    'SensitivePlus': {
        'id': PROFILE_ID_SENSITIVE_PLUS,
        'level_1': {
            'max_torque': 65,
            'max_speed': 1389,           # 5.0 km/h
            'speed_bias': 50,            # Sensor sensitivity (highest)
            'slope_inc': 70,             # Startup time (shortest/fastest)
            'slope_dec': 28,             # Coasting time (longer)
            'p_factor': 5,
            'speed_factor': 50,          # Higher speed factor (unique)
            'rotation_threshold': 1,     # Most sensitive (unique)
        },
        'level_2': {
            'max_torque': 100,
            'max_speed': 2361,           # 8.5 km/h
            'speed_bias': 50,            # Sensor sensitivity (highest)
            'slope_inc': 70,             # Startup time (shortest/fastest)
            'slope_dec': 20,             # Coasting time (longest)
            'p_factor': 5,
            'speed_factor': 50,          # Higher speed factor (unique)
            'rotation_threshold': 1,     # Most sensitive (unique)
        },
    },
}


# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def get_profile(name):
    """Get profile by name (case-insensitive)"""
    name_lower = name.lower().replace(' ', '').replace('+', 'plus')
    for pname, pdata in PROFILES.items():
        if pname.lower().replace('+', 'plus') == name_lower:
            return pdata
    return None

def get_profile_by_id(profile_id):
    """Get profile by firmware ID"""
    for pname, pdata in PROFILES.items():
        if pdata['id'] == profile_id:
            return pdata
    return None

def get_level_params(profile_name, level):
    """Get parameters for a specific profile and level

    Args:
        profile_name: Profile name (e.g., 'Active')
        level: 1 or 2

    Returns:
        dict with profile parameters or None
    """
    profile = get_profile(profile_name)
    if profile:
        return profile.get(f'level_{level}')
    return None

def format_profile(profile_data):
    """Format profile data for display"""
    lines = []
    for level in ['level_1', 'level_2']:
        params = profile_data[level]
        speed_kmh = speed_raw_to_kmh(params['max_speed'])

        level_num = level[-1]
        lines.append(f"  Level {level_num}:")
        lines.append(f"    Max Torque:      {params['max_torque']}%")
        lines.append(f"    Max Speed:       {speed_kmh:.1f} km/h")
        lines.append(f"    Sensor Sens.:    {params['speed_bias']} (speedBias)")
        lines.append(f"    Startup Time:    {params['slope_inc']} (slopeInc)")
        lines.append(f"    Coasting Time:   {params['slope_dec']} (slopeDec)")
        lines.append(f"    P-Factor:        {params['p_factor']}")
        lines.append(f"    Speed Factor:    {params['speed_factor']}")
        lines.append(f"    Rot. Threshold:  {params['rotation_threshold']}")
    return '\n'.join(lines)


# =============================================================================
# CLI
# =============================================================================

if __name__ == '__main__':
    import sys

    if len(sys.argv) > 1:
        name = ' '.join(sys.argv[1:])
        profile = get_profile(name)
        if profile:
            print(f"{name}:")
            print(format_profile(profile))
        else:
            print(f"Unknown profile: {name}")
            print(f"Available: {', '.join(PROFILES.keys())}")
    else:
        print("M25 Drive Profile Defaults\n")
        for name, data in PROFILES.items():
            print(f"=== {name} (ID: {data['id']}) ===")
            print(format_profile(data))
            print()
