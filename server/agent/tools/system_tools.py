import time

def check_server_health():
    """
    Simulates checking the health of the central management server.
    """
    return {
        "status": "Running",
        "cpu_usage": "15%",
        "memory_usage": "2.4GB / 16GB",
        "disk_space": "150GB Free",
        "uptime": "5d 12h 45m"
    }

def analyze_grid_health(devices_summary):
    """
    Simulate analyzing the overall health of the monitored grid area.
    Takes a list of device statuses or summary.
    """
    total_devices = len(devices_summary or [])
    if total_devices == 0:
        return "No devices monitored."

    online_count = sum(1 for d in devices_summary if d.get('status') == 'Online')
    warning_count = sum(1 for d in devices_summary if d.get('status') == 'Warning')
    
    health_score = (online_count / total_devices) * 100
    
    report = f"Grid Health Analysis:\n"
    report += f"- Total Devices: {total_devices}\n"
    report += f"- Online: {online_count} ({health_score:.1f}%)\n"
    report += f"- Warnings: {warning_count}\n"
    
    if health_score < 70:
        report += "CRITICAL: Significant portion of the grid monitoring is offline.\n"
    elif warning_count > total_devices * 0.2:
         report += "WARNING: High number of device warnings reported.\n"
    else:
        report += "STATUS: Normal operations.\n"
        
    return report

def detect_grid_anomalies(logs):
    """
    Detects potential anomalies across multiple device logs.
    """
    anomaly_patterns = ["Foreign object detected", "Fire hazard detected", "Excessive heat"]
    anomalies = []
    
    for log in logs or []:
        for pattern in anomaly_patterns:
            if pattern in log.get('message', ''):
                anomalies.append(f"Detected {pattern} at {log.get('timestamp')}")
                
    if not anomalies:
        return "No significant anomalies found in recent logs."
    
    return "Anomalies Detected:\n- " + "\n- ".join(anomalies)
