"""Run an independent CHIP controller against the native server and real FlashDB."""

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def wait_for(server, log, marker, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if server.poll() is not None:
            raise RuntimeError(f"Matter server exited ({server.returncode}); see {log}")
        if marker in log.read_text(errors="replace"):
            return
        time.sleep(0.05)
    raise TimeoutError(f"Missing {marker!r}; see {log}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--ipv4", required=True)
    parser.add_argument("--controller-python", default=sys.executable)
    parser.add_argument("--controller-container")
    parser.add_argument("--controller-site-packages", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=False)  # Never overwrite commissioned state.
    socket.inet_pton(socket.AF_INET, args.ipv4)
    controller = [args.controller_python]
    if args.controller_container:
        if args.controller_site_packages is None:
            parser.error("--controller-container requires --controller-site-packages")
        site = args.controller_site_packages.resolve()
        common = Path(os.path.commonpath([ROOT, build, site]))
        if common == Path("/"):
            parser.error("container paths must share a directory other than /")
        controller = [
            "docker",
            "run",
            "--rm",
            "--network",
            "host",
            "--user",
            f"{os.getuid()}:{os.getgid()}",
            "--mount",
            f"type=bind,src={common},dst={common}",
            "-e",
            f"PYTHONPATH={site}",
            args.controller_container,
            "python",
        ]
    stages = []

    def run_controller(name, storage, *options):
        log = build / f"{name}-controller.log"
        with log.open("w") as output:
            subprocess.run(
                [
                    *controller,
                    str(ROOT / "tests/server/controller.py"),
                    "--storage",
                    str(storage),
                    "--paa",
                    str(ROOT / "tests/server/paa"),
                    *options,
                ],
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=180,
                check=True,
            )
        checks = [
            line for line in log.read_text().splitlines() if line.startswith("PASS ")
        ]
        assert checks, f"No assertions reported in {log}"
        stages.append({"stage": name, "checks": checks})
        print(f"PASS {name}: {len(checks)} checks", flush=True)

    def start(name, state, **env):
        log = build / f"{name}-device.log"
        with log.open("w") as output:
            process = subprocess.Popen(
                [str(args.binary.resolve()), str(state), args.ipv4],
                stdout=output,
                stderr=subprocess.STDOUT,
                env={**os.environ, **env},
            )
        try:
            wait_for(process, log, "Starting UDP/5540")
        except BaseException:
            stop(process)
            raise
        return process, log

    def stop(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)

    storage = build / "controller.json"
    state = build / "device"
    server, log = start("commission", state)
    try:
        run_controller("commission", storage, "--discover", "--wrong-pin")
        assert "PAKE failure 1 of" in log.read_text()
    finally:
        stop(server)
    server, log = start("reboot", state)
    try:
        run_controller("reboot", storage, "--existing")
        server.send_signal(signal.SIGUSR1)
        wait_for(server, log, "WARM_RESTART")
        run_controller("warm-restart", storage, "--existing")
    finally:
        stop(server)

    failure_state = build / "failure-device"
    server, log = start(
        "storage-failure", failure_state, LTM_TEST_FAIL_FABRIC_WRITE="1"
    )
    try:
        run_controller(
            "storage-failure",
            build / "failure-controller.json",
            "--expect-storage-failure",
        )
        assert "STORE key=0001 bytes=" in log.read_text()
        assert "error=159" in log.read_text(), "NOR write failure not exercised"
    finally:
        stop(server)
    server, log = start("storage-recovery", failure_state)
    try:
        assert "commissioned=false" in log.read_text(), "failed fabric survived reboot"
        run_controller("storage-recovery", build / "recovery-controller.json")
    finally:
        stop(server)

    # The host callback injects entropy failure; the Rust runtime must stop before PASE.
    log = build / "entropy-failure-device.log"
    with log.open("w") as output:
        result = subprocess.run(
            [str(args.binary.resolve()), str(build / "entropy-device"), args.ipv4],
            stdout=output,
            stderr=subprocess.STDOUT,
            env={**os.environ, "LTM_TEST_FAIL_ENTROPY": "1"},
            timeout=15,
        )
    assert result.returncode == -signal.SIGABRT
    assert "Hardware entropy failed; stopping Matter" in log.read_text()
    stages.append(
        {"stage": "entropy-failure", "checks": ["failed closed before commissioning"]}
    )
    report = {
        "controller": "ConnectedHomeIP 2025.7.0",
        "hardware_tested": False,
        "stages": stages,
    }
    (build / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
