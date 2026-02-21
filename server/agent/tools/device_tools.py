import random
import time

def get_device_status(device_id):
    """
    Simulates retrieving the real-time status of a power grid monitoring device.
    """
    # In a real scenario, this would query the device or a database.
    statuses = ["Online", "Offline", "Warning", "Maintenance"]
    status = random.choice(statuses)
    
    battery_level = random.randint(20, 100)
    temperature = random.randint(10, 80) # Celsius
    signal_strength = random.randint(-90, -50) # dBm
    
    return {
        "device_id": device_id,
        "status": status,
        "battery": f"{battery_level}%",
        "temperature": f"{temperature}°C",
        "signal_strength": f"{signal_strength}dBm",
        "last_seen": time.strftime("%Y-%m-%d %H:%M:%S")
    }

def get_device_logs(device_id, limit=5):
    """
    Retrieves recent operational logs for a device.
    """
    log_types = ["INFO", "WARNING", "ERROR"]
    messages = [
        "Connected to server",
        "Image captured successfully",
        "Battery level low",
        "High temperature alert",
        "Foreign object detected",
        "Firmware update check",
        "Heartbeat received"
    ]
    
    logs = []
    for _ in range(limit):
        logs.append({
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - random.randint(0, 3600))),
            "level": random.choice(log_types),
            "message": random.choice(messages)
        })
    
    # Sort logs by timestamp simulated (not strictly sorted here but acceptable for mock)
    return logs

def perform_diagnostic(device_id):
    """
    Simulates a remote diagnostic check on the device.
    """
    checks = ["Camera Sensor", "Network Module", "Storage", "Power Management"]
    results = {}
    all_pass = True
    
    for check in checks:
        passed = random.random() > 0.1 # 90% chance of pass
        results[check] = "Pass" if passed else "Fail"
        if not passed:
            all_pass = False
            
    return {
        "device_id": device_id,
        "diagnostic_result": "Healthy" if all_pass else "Issues Found",
        "details": results
    }
