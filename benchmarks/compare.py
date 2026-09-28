#!/usr/bin/env python3
"""Compare current benchmark with baseline."""
import json
import sys

THRESHOLD_PERCENT = 5.0  # 5% tolerance

def compare(current_file, baseline_file):
    with open(current_file) as f:
        current = json.load(f)
    
    with open(baseline_file) as f:
        baseline = json.load(f)
    
    regressions = []
    
    for key in ["firmware_size", "elf_size"]:
        if key not in baseline:
            continue
        
        current_val = current.get(key, 0)
        baseline_val = baseline[key]
        
        if baseline_val == 0:
            continue
        
        delta = current_val - baseline_val
        percent = (delta / baseline_val) * 100
        
        status = "✅" if percent <= THRESHOLD_PERCENT else "❌"
        print(f"{status} {key}: {current_val} (baseline: {baseline_val}, delta: {delta:+d}, {percent:+.2f}%)")
        
        if percent > THRESHOLD_PERCENT:
            regressions.append(key)
    
    if regressions:
        print(f"\n❌ REGRESSION DETECTED: {', '.join(regressions)}")
        sys.exit(1)
    
    print("\n✅ No regression")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: compare.py <current.json> <baseline.json>", file=sys.stderr)
        sys.exit(1)
    
    compare(sys.argv[1], sys.argv[2])