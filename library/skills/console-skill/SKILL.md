---
name: console-skill
description: Developer and system console management: terminal commands execution, process inspection, port management, IPC diagnosis, environment variables tuning, and real-time log streaming.
---

# Console Skill — Developer & System Console Automation

Monitor, control, and inspect local operating system processes, runtime environments, network sockets, and execution logs.

## Capabilities

1. **Process Management**
   - Query running processes by name, PID, memory usage, and CPU utilization.
   - Graceful termination (SIGTERM) with timeout escalation to SIGKILL.
   - Child process lifecycle monitoring, restart loops, and heartbeat health checks.

2. **Network & Port Management**
   - Active TCP/UDP socket listing (`ss -tlpn`, `netstat`, `lsof -i`).
   - Port conflict resolution and free port allocation.
   - Reverse proxy forwarding and socket connectivity validation.

3. **Log Streaming & Triage**
   - Real-time tailing of stdout/stderr logs.
   - Regex-based log filtering, error highlighting, and stack-trace parsing.
   - Resource exhaustion alerts (OOM killer warnings, disk capacity alerts).
