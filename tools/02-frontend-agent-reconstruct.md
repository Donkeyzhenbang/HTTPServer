# Frontend & Agent Reconstruction Plan

## 1. Frontend Refactoring (Completed)
**Objective**: Improve maintainability of `index.html` by modularizing code.

### Changes Implemented:
- **CSS Extraction**:
  - Moved all internal styles from `<style>` tags in `index.html` to `server/frontend/css/style.css`.
  - Content includes: Root variables, layout grid, component styles (topbar, panel, viewer, timeline), and responsive rules.

- **JavaScript Extraction**:
  - Moved core application logic from `<script>` tags to `server/frontend/js/app.js`.
  - Content includes:
    - Global state management (`imagesList`, `currentIndex`, `currentChannel`).
    - API interaction functions (`loadDevices`, `loadImages`, `fetchJSON`).
    - UI interactions (Thumbnail rendering, Image viewer zooming/panning, Calendar logic).
    - Upgrade modal logic and simulation.

- **HTML Cleanup**:
  - `server/frontend/index.html` now only contains:
    - Semantic HTML structure.
    - Setup of `link` tag for CSS.
    - Setup of `script` tag for `app.js`.
    - Retained `agent_widget.js` inclusion.

## 2. Agent Upgrade (Completed)
**Objective**: Transform the empty Python agent into a functional "Power Grid Device Monitoring" assistant.

### Architecture (`server/agent/`):
- **`agent_core.py`**:
  - Main entry point.
  - Implements `PowerGridAgent` class with an interactive CLI loop.
  - Supports commands: `status`, `logs`, `diag`, `health`, `anomaly`.

- **`tools/device_tools.py`**:
  - `get_device_status(id)`: Simulates battery, temperature, signal strength.
  - `get_device_logs(id)`: Generates mock logs (INFO/WARN/ERROR).
  - `perform_diagnostic(id)`: Simulates component checks (Sensor, Network, Storage).

- **`tools/system_tools.py`**:
  - `check_server_health()`: Returns CPU/Memory stats.
  - `analyze_grid_health()`: Aggregates device statuses to calculate a "Grid Health Score".
  - `detect_grid_anomalies()`: Scans logs for patterns like "Foreign object" or "Fire hazard".

## 3. Future Roadmap

### Frontend
- [ ] **Data Visualization**: Integrate a charting library (e.g., ECharts) to visualize connection stats and device health trends over time.
- [ ] **Real-time WebSocket**: Replace polling (`setInterval`) with WebSocket for instant status updates.
- [ ] **Components**: Further split `app.js` into modules (e.g., `viewer.js`, `api.js`, `ui.js`) when it grows larger.

### Agent
- [ ] **Real API Integration**: Replace mock functions in `device_tools.py` with actual HTTP calls to the C++ server via `requests`.
- [ ] **Automated Alerts**: Run the agent in "daemon mode" to periodically check health and send emails/SMS on anomalies.
- [ ] **LLM Integration**: Connect `agent_core.py` to a local or remote LLM API to interpret complex natural language queries (e.g., "Which devices had battery issues last week?").

## 4. Verification
- **Frontend**: Open browser to `http://<server-ip>:<port>/` to verify layout and functionality remain unchanged.
- **Agent**: Run `python3 server/agent/agent_core.py` and type `help` to interact.
