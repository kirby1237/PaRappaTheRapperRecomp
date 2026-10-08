"""Local TCP tooling for the isolated kirby1237 PaRappa timing experiment.

Uses the runtime's frame-based input route, never wall-clock button timing.
Run with the native Windows Python, not the MSYS Python shim.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import time


class Client:
    def __init__(self, port: int = 9453):
        self.port = port
        self.sequence = 0

    def call(self, command: str, **params):
        self.sequence += 1
        request = dict(id=self.sequence, cmd=command, **params)
        # This framework pin serves one request per connection.
        with socket.create_connection(("127.0.0.1", self.port), timeout=20) as sock:
            with sock.makefile("rwb") as stream:
                stream.write((json.dumps(request) + "\n").encode())
                stream.flush()
                while line := stream.readline():
                    result = json.loads(line)
                    if result.get("id") != self.sequence:
                        continue
                    if not result.get("ok"):
                        raise RuntimeError(result)
                    return result
        raise ConnectionError("Runtime closed its debug connection")

    def close(self):
        pass

    def route(self, steps):
        self.call("input_route_clear")
        for frames, buttons in steps:
            self.call("input_route_append", frames=frames, buttons=buttons)
        return self.call("input_route_start")

    def wait_route(self, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.call("input_route_status")
            if not status["active"]:
                return status
            time.sleep(0.1)
        raise TimeoutError("Input route did not complete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9453)
    sub = parser.add_subparsers(dest="action", required=True)
    command = sub.add_parser("command")
    command.add_argument("name")
    command.add_argument("params", nargs="*", help="key=value fields, or @JSON-file")
    route = sub.add_parser("route")
    route.add_argument("steps", help="JSON list of [frames, inverted PSX buttons]")
    route.add_argument("--timeout", type=float, default=60)
    capture = sub.add_parser("capture")
    capture.add_argument("directory", type=Path)
    args = parser.parse_args()
    client = Client(args.port)
    try:
        if args.action == "command":
            params = {}
            for item in args.params:
                if item.startswith("@"):
                    params.update(json.loads(Path(item[1:]).read_text()))
                else:
                    key, value = item.split("=", 1)
                    try:
                        params[key] = json.loads(value)
                    except json.JSONDecodeError:
                        params[key] = value
            result = client.call(args.name, **params)
        elif args.action == "route":
            start = client.route(json.loads(args.steps))
            result = dict(start=start, completion=client.wait_route(args.timeout))
        else:
            args.directory.mkdir(parents=True, exist_ok=True)
            result = {}
            for name in ("frame", "audio_stats", "latency", "get_registers", "fn_entry_tail"):
                result[name] = client.call(name)
            result["screenshot"] = client.call(
                "screenshot", path=str((args.directory / "screen.png").resolve()))
            (args.directory / "capture.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
    finally:
        client.close()


if __name__ == "__main__":
    main()
