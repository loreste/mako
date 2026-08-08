#!/usr/bin/env python3
"""Scripted DAP client used by scripts/test-dap.sh to exercise `mako dap`.

Usage: dap_drive.py <mako-binary> <probe.mko> <breakpoint-line>
Exits 0 and prints PASS when the session reaches the breakpoint, reports a
stack frame in the .mko file, and shows MakoString/MakoIntArray formatted
values; exits 1 with FAIL details otherwise.
"""

import json
import subprocess
import sys

mako, probe, bp_line = sys.argv[1], sys.argv[2], int(sys.argv[3])
expect_build_fail = "--expect-build-fail" in sys.argv

proc = subprocess.Popen(
    [mako, "dap"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
)

_seq = [0]


def send(command, arguments=None):
    _seq[0] += 1
    msg = {"seq": _seq[0], "type": "request", "command": command}
    if arguments is not None:
        msg["arguments"] = arguments
    body = json.dumps(msg).encode()
    proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    proc.stdin.flush()
    return _seq[0]


def recv():
    headers = {}
    while True:
        line = proc.stdout.readline().decode().strip()
        if not line:
            break
        k, _, v = line.partition(":")
        headers[k.lower()] = v.strip()
    n = int(headers.get("content-length", "0"))
    if n == 0:
        return None
    return json.loads(proc.stdout.read(n))


def wait_for(pred, timeout_secs=60, what="message"):
    import time
    deadline = time.monotonic() + timeout_secs
    while time.monotonic() < deadline:
        msg = recv()
        if msg is None:
            raise RuntimeError("unexpected EOF from adapter")
        if msg.get("type") == "event" and msg.get("event") == "module":
            continue  # dyld can stream hundreds of these; don't log or count them
        print("  .. saw %s %s" % (msg.get("type"), msg.get("event") or msg.get("command")),
              file=sys.stderr)
        if pred(msg):
            return msg
    raise RuntimeError("timed out waiting for " + what)


def fail(why):
    print("FAIL: " + why)
    proc.kill()
    sys.exit(1)


try:
    init_seq = send("initialize", {"adapterID": "mako-test"})
    wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == init_seq
             and m.get("success"))

    launch_seq = send("launch", {
        "program": probe,
        "args": [],
        "stopOnEntry": False,
    })
    resp = wait_for(lambda m: m.get("type") == "response"
                    and m.get("request_seq") == launch_seq, timeout_secs=90)
    if expect_build_fail:
        if resp.get("success"):
            fail("launch unexpectedly succeeded for a broken source")
        if "build failed" not in (resp.get("message") or ""):
            fail("launch error does not mention build failure: %r" % resp.get("message"))
        send("disconnect", {})
        proc.wait(timeout=10)
        print("PASS: build failure reported as DAP error (%s)" % resp.get("message"))
        sys.exit(0)
    if not resp.get("success"):
        fail("launch: " + resp.get("message", "?"))
    # Standard DAP order: breakpoints go in after the `initialized` event.
    wait_for(lambda m: m.get("type") == "event" and m.get("event") == "initialized",
             timeout_secs=90)

    bp_seq = send("setBreakpoints", {
        "source": {"path": probe},
        "breakpoints": [{"line": bp_line}],
    })
    bp_resp = wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == bp_seq,
                       timeout_secs=90)
    bps = bp_resp.get("body", {}).get("breakpoints", [])
    if not bps or not bps[0].get("verified"):
        fail("breakpoint not verified: %r" % (bps,))

    send("configurationDone")
    # Stopped event at our breakpoint (breakpoint may bind before/after launch).
    stopped = wait_for(lambda m: m.get("type") == "event" and m.get("event") == "stopped",
                       timeout_secs=90)
    thread_id = stopped.get("body", {}).get("threadId", 1)

    st_seq = send("stackTrace", {"threadId": thread_id})
    st = wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == st_seq)
    frames = st.get("body", {}).get("stackFrames", [])
    if not frames:
        fail("empty stackTrace")
    top = frames[0]
    src = (top.get("source") or {}).get("path") or (top.get("source") or {}).get("name") or ""
    if not src.endswith(".mko"):
        fail("top frame not in .mko source: %r" % src)
    if top.get("line") != bp_line:
        fail("top frame line %r != breakpoint line %r" % (top.get("line"), bp_line))

    sc_seq = send("scopes", {"frameId": top["id"]})
    sc = wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == sc_seq)
    scopes = sc.get("body", {}).get("scopes", [])
    if not scopes:
        fail("no scopes")
    ref = scopes[0]["variablesReference"]

    var_seq = send("variables", {"variablesReference": ref})
    vr = wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == var_seq)
    variables = {v["name"]: v.get("value", "") for v in vr.get("body", {}).get("variables", [])}
    if variables.get("name") != '"mako"':
        fail("MakoString not formatted: %r" % variables.get("name"))
    nums = variables.get("nums", "")
    if "10" not in nums:
        fail("MakoIntArray not formatted: %r" % nums)

    send("continue", {"threadId": thread_id})
    wait_for(lambda m: m.get("type") == "event" and m.get("event") == "terminated",
             timeout_secs=90)
    send("disconnect", {})
except Exception as e:  # noqa: BLE001
    fail(str(e))

proc.wait(timeout=10)
print("PASS: stopped at %s:%d, frames in .mko, variables formatted" % (probe, bp_line))
