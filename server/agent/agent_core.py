import sys
import os
import time
import json
import logging
import random

# Ensure tools directory is in python path
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(current_dir, 'tools'))

try:
    from device_tools import get_device_status, get_device_logs, perform_diagnostic
    from system_tools import check_server_health, analyze_grid_health, detect_grid_anomalies
except ImportError as e:
    print(f"Error importing tools: {e}")
    sys.exit(1)

class PowerGridAgent:
    def __init__(self):
        # In a real app, these would be fetched from the API
        self.devices = ["Device-A001", "Device-A002", "Device-B101", "Device-B105", "Device-C200"]
        
        logging.basicConfig(
            level=logging.INFO, 
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler("agent.log"),
                logging.StreamHandler()
            ]
        )
        self.logger = logging.getLogger("GridAgent")

    def process_command(self, command_str):
        """Process a natural language or structured command."""
        cmd = command_str.strip().lower()
        
        if cmd in ['exit', 'quit']:
            return "EXIT"
            
        elif cmd == 'help':
            return self._get_help_text()
            
        elif cmd == 'list devices' or cmd == 'ls':
            return f"Monitored Devices:\n" + "\n".join([f"- {d}" for d in self.devices])
            
        elif cmd.startswith('status'):
            parts = cmd.split()
            if len(parts) > 1:
                dev_id = parts[1]
                # Fuzzy match for convenience
                target = next((d for d in self.devices if dev_id.lower() in d.lower()), dev_id)
                return self._report_device_status(target)
            else:
                return "Usage: status <device_id>"
                
        elif cmd.startswith('logs'):
            parts = cmd.split()
            if len(parts) > 1:
                dev_id = parts[1]
                target = next((d for d in self.devices if dev_id.lower() in d.lower()), dev_id)
                return self._report_device_logs(target)
            else:
                return "Usage: logs <device_id>"
                
        elif cmd.startswith('diag'):
            parts = cmd.split()
            if len(parts) > 1:
                dev_id = parts[1]
                target = next((d for d in self.devices if dev_id.lower() in d.lower()), dev_id)
                return self._run_diagnostics(target)
            else:
                return "Usage: diag <device_id>"
                
        elif cmd == 'health' or cmd == 'grid status':
            return self._analyze_full_grid()
            
        elif cmd == 'server' or cmd == 'system':
            return self._check_server()
            
        elif 'foreign object' in cmd or 'anomaly' in cmd:
             return self._scan_for_anomalies()
             
        else:
            return f"Unknown command: '{cmd}'. Type 'help' for available commands."

    def _get_help_text(self):
        return """
Available Commands:
  list devices       - List all monitored devices
  status <id>        - Check real-time status of a device
  logs <id>          - Analyze recent logs for a device
  diag <id>          - Run remote diagnostics on a device
  health             - Analyze overall grid health statistics
  server             - Check central server health metrics
  anomaly            - Scan all devices for specific anomalies (e.g. foreign objects)
  exit               - Quit the agent
"""

    def _report_device_status(self, device_id):
        self.logger.info(f"Querying status for {device_id}")
        info = get_device_status(device_id)
        return json.dumps(info, indent=2, ensure_ascii=False)

    def _report_device_logs(self, device_id):
        self.logger.info(f"Retrieving logs for {device_id}")
        logs = get_device_logs(device_id)
        return json.dumps(logs, indent=2, ensure_ascii=False)

    def _run_diagnostics(self, device_id):
        self.logger.info(f"Running diagnostics on {device_id}")
        result = perform_diagnostic(device_id)
        return json.dumps(result, indent=2, ensure_ascii=False)

    def _analyze_full_grid(self):
        self.logger.info("Analyzing full grid health")
        # Simulate gathering data from all devices
        summaries = [get_device_status(d) for d in self.devices]
        report = analyze_grid_health(summaries)
        return report

    def _check_server(self):
        stats = check_server_health()
        return json.dumps(stats, indent=2)

    def _scan_for_anomalies(self):
        self.logger.info("Scanning for anomalies across grid")
        all_logs = []
        for d in self.devices:
             device_logs = get_device_logs(d)
             # Enrich with device ID for context
             for l in device_logs:
                 l['device_id'] = d
             all_logs.extend(device_logs)
        
        return detect_grid_anomalies(all_logs)

    def run_interactive(self):
        print("=== Power Grid Intelligent Agent v2.0 ===")
        print("System initialized. Monitoring 5 devices.")
        print("Type 'help' for commands.")
        
        while True:
            try:
                command = input("\nAgent> ")
                response = self.process_command(command)
                if response == "EXIT":
                    print("Shutting down agent...")
                    break
                print(response)
            except KeyboardInterrupt:
                print("\nInterrupted. Exiting...")
                break
            except Exception as e:
                self.logger.error(f"Error processing command: {e}")
                print(f"Error: {e}")

if __name__ == "__main__":
    agent = PowerGridAgent()
    agent.run_interactive()
