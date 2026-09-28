import json
import sys

def compare(current_file, baseline_file):
    with open(current_file) as f:
        current = json.load(f)
    with open(baseline_file) as f:
        baseline = json.load(f)
    
    size_delta = current["firmware_size"] - baseline["firmware_size"]
    size_percent = (size_delta / baseline["firmware_size"]) * 100
    
    print(f"Firmware size: {current['firmware_size']} (baseline: {baseline['firmware_size']})")
    print(f"Delta: {size_delta} bytes ({size_percent:.2f}%)")
    
    if size_percent > 5.0:
        print("❌ REGRESSION DETECTED")
        sys.exit(1)
    
    print("✅ No regression")

if __name__ == "__main__":
    compare(sys.argv[1], sys.argv[2])