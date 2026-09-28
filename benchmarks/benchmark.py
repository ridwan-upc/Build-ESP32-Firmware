#!/usr/bin/env python3
"""Measure firmware metrics."""
import json
import os
import sys

def measure_firmware_size(env):
    """Measure firmware size from build output."""
    firmware_path = f".pio/build/{env}/firmware.bin"
    
    if not os.path.exists(firmware_path):
        print(f"❌ Firmware not found: {firmware_path}", file=sys.stderr)
        return 0
    
    return os.path.getsize(firmware_path)

def measure_elf_size(env):
    """Measure ELF size."""
    elf_path = f".pio/build/{env}/firmware.elf"
    
    if not os.path.exists(elf_path):
        return 0
    
    return os.path.getsize(elf_path)

def main():
    if len(sys.argv) < 2:
        print("Usage: benchmark.py <env>", file=sys.stderr)
        sys.exit(1)
    
    env = sys.argv[1]
    
    result = {
        "env": env,
        "firmware_size": measure_firmware_size(env),
        "elf_size": measure_elf_size(env),
    }
    
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()