#!/usr/bin/env python3
"""
PlatformIO script to patch ESP32 I2S parallel DMA divider calculation
Only runs for specific patched configurations
"""

import os
import re
from pathlib import Path

Import("env")

def patch_i2s_divider():
    """Patch the I2S divider calculation in the ESP32 HUB75 library"""
    
    # Only run for patched configurations
    current_env = env.subst("$PIOENV")
    if not current_env.endswith("-patched"):
        return
    
    print(f"Running I2S divider patch for environment: {current_env}")
    
    # Path to the library source file
    lib_path = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / current_env / "ESP32 HUB75 LED MATRIX PANEL DMA Display" / "src" / "platforms" / "esp32" / "esp32_i2s_parallel_dma.cpp"
    
    if not lib_path.exists():
        print(f"Warning: Could not find I2S library file at {lib_path}")
        return
    
    # Read the file
    content = lib_path.read_text()
    
    # Pattern to match the incorrect divider line for ESP32 (non-S2), with optional comment
    incorrect_pattern = r'unsigned int _div_num = \(freq > 8000000\) \? 2:4;.*'
    
    # Replacement with correct calculation
    correct_calculation = 'unsigned int _div_num = 80000000L / freq / 2;'
    
    # Check if we need to patch
    if re.search(incorrect_pattern, content):
        print("⚙️  Patching I2S divider calculation...")
        content = re.sub(incorrect_pattern, correct_calculation, content)
        lib_path.write_text(content)
        print("✅ I2S divider patch applied successfully - now using: unsigned int _div_num = 80000000L / freq / 2;")
    elif correct_calculation in content:
        print("✅ I2S divider already patched correctly - using: unsigned int _div_num = 80000000L / freq / 2;")
    else:
        print("⚠️  Warning: Could not find expected I2S divider pattern to patch")
        print("    Expected to find: unsigned int _div_num = (freq > 8000000) ? 2:4;")
        print("    Or already patched: unsigned int _div_num = 80000000L / freq / 2;")

# Run the patch immediately when this script is loaded
patch_i2s_divider()