#!/usr/bin/env python3
"""
ProcWatch Web Dashboard
═══════════════════════

Real-time web interface for ProcWatch process monitoring.
Reads events from /dev/procwatch and serves them via a Flask
web application with live-updating charts and event stream.

Usage:
    sudo python3 app.py                    # Start on port 5000
    sudo python3 app.py --port 8080        # Custom port
    sudo python3 app.py --host 0.0.0.0     # Listen on all interfaces

Requirements:
    pip install flask

Author: ProcWatch Contributors
License: GPL-2.0
"""

import os
import sys
import json
import time
import threading
import argparse
import struct
from collections import deque, Counter
from datetime import datetime

from flask import Flask, render_template, jsonify, Response

# ─── Configuration ────────────────────────────────────────────────────────

DEVICE_PATH = "/dev/procwatch"
PROC_STATS  = "/proc/procwatch/stats"
MAX_EVENTS  = 500  # Keep last N events in memory
READ_SIZE   = 8192

# ─── Flask App ────────────────────────────────────────────────────────────

app = Flask(__name__)

# ─── Event Storage ────────────────────────────────────────────────────────

events_lock = threading.Lock()
events = deque(maxlen=MAX_EVENTS)
stats = {
    "fork_count": 0,
    "exec_count": 0,
    "exit_count": 0,
    "total_count": 0,
    "events_per_second": 0,
    "top_commands": {},
    "active_since": datetime.now().isoformat(),
}


def parse_event_line(line):
    """Parse a single event line from /dev/procwatch.
    
    Format: TYPE PID PPID UID TIMESTAMP_NS COMM [extra]
    Example: FORK  1234  1000  1000 1699999999999 bash
    """
    parts = line.strip().split()
    if len(parts) < 6:
        return None

    event_type = parts[0]
    try:
        pid = int(parts[1])
        ppid = int(parts[2])
        uid = int(parts[3])
        timestamp_ns = int(parts[4])
        comm = parts[5]
    except (ValueError, IndexError):
        return None

    extra = " ".join(parts[6:]) if len(parts) > 6 else ""

    # Convert nanosecond timestamp to human-readable
    ts_seconds = timestamp_ns / 1_000_000_000
    try:
        time_str = datetime.fromtimestamp(ts_seconds).strftime("%H:%M:%S.%f")[:-3]
    except (OSError, ValueError):
        time_str = str(timestamp_ns)

    return {
        "type": event_type,
        "pid": pid,
        "ppid": ppid,
        "uid": uid,
        "timestamp": time_str,
        "timestamp_ns": timestamp_ns,
        "command": comm,
        "extra": extra,
    }


def reader_thread():
    """Background thread that reads events from /dev/procwatch."""
    global stats
    
    event_times = deque(maxlen=100)  # Track last 100 event times for rate calc

    while True:
        try:
            if not os.path.exists(DEVICE_PATH):
                time.sleep(1)
                continue

            with open(DEVICE_PATH, "r") as dev:
                buf = ""
                while True:
                    try:
                        data = dev.read(READ_SIZE)
                    except IOError:
                        time.sleep(0.1)
                        continue

                    if not data:
                        time.sleep(0.05)
                        continue

                    buf += data
                    lines = buf.split("\n")
                    buf = lines[-1]  # Keep incomplete line

                    for line in lines[:-1]:
                        if not line.strip():
                            continue

                        event = parse_event_line(line)
                        if event is None:
                            continue

                        now = time.time()
                        event_times.append(now)

                        with events_lock:
                            events.append(event)

                            # Update counters
                            if event["type"] == "FORK":
                                stats["fork_count"] += 1
                            elif event["type"] == "EXEC":
                                stats["exec_count"] += 1
                            elif event["type"] == "EXIT":
                                stats["exit_count"] += 1
                            stats["total_count"] += 1

                            # Track command frequency
                            cmd = event["command"]
                            stats["top_commands"][cmd] = (
                                stats["top_commands"].get(cmd, 0) + 1
                            )

                            # Calculate events/second
                            cutoff = now - 1.0
                            recent = sum(1 for t in event_times if t > cutoff)
                            stats["events_per_second"] = recent

        except Exception as e:
            print(f"Reader error: {e}", file=sys.stderr)
            time.sleep(1)


# ─── Routes ───────────────────────────────────────────────────────────────

@app.route("/")
def index():
    """Serve the main dashboard page."""
    return render_template("index.html")


@app.route("/api/events")
def api_events():
    """Return recent events as JSON."""
    with events_lock:
        return jsonify(list(events))


@app.route("/api/stats")
def api_stats():
    """Return monitoring statistics."""
    with events_lock:
        # Get top 10 commands
        top_cmds = sorted(
            stats["top_commands"].items(),
            key=lambda x: x[1],
            reverse=True
        )[:10]

        return jsonify({
            "fork_count": stats["fork_count"],
            "exec_count": stats["exec_count"],
            "exit_count": stats["exit_count"],
            "total_count": stats["total_count"],
            "events_per_second": stats["events_per_second"],
            "top_commands": dict(top_cmds),
            "buffer_size": len(events),
            "active_since": stats["active_since"],
        })


@app.route("/api/stream")
def api_stream():
    """Server-Sent Events stream for real-time updates."""
    def generate():
        last_count = 0
        while True:
            with events_lock:
                current_count = stats["total_count"]
                if current_count > last_count:
                    new_events = list(events)[-10:]  # Last 10 events
                    data = json.dumps({
                        "events": new_events,
                        "stats": {
                            "fork_count": stats["fork_count"],
                            "exec_count": stats["exec_count"],
                            "exit_count": stats["exit_count"],
                            "total_count": stats["total_count"],
                            "events_per_second": stats["events_per_second"],
                        }
                    })
                    yield f"data: {data}\n\n"
                    last_count = current_count
            time.sleep(0.5)

    return Response(generate(), mimetype="text/event-stream")


# ─── Main ─────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ProcWatch Web Dashboard")
    parser.add_argument("--host", default="127.0.0.1", help="Host to bind")
    parser.add_argument("--port", type=int, default=5000, help="Port number")
    parser.add_argument("--debug", action="store_true", help="Debug mode")
    args = parser.parse_args()

    # Check root
    if os.geteuid() != 0:
        print("Warning: not running as root. May not be able to read "
              f"{DEVICE_PATH}", file=sys.stderr)

    # Start reader thread
    thread = threading.Thread(target=reader_thread, daemon=True)
    thread.start()
    print(f"ProcWatch Web Dashboard starting on http://{args.host}:{args.port}")

    app.run(host=args.host, port=args.port, debug=args.debug)
