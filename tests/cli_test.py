"""End-to-end checks against the executable, including EOF and strict exit syntax."""
import subprocess
import sys

binary = sys.argv[1]
commands = "\n".join([
    "create BadProcess", "pcb xyz", "set_sched FIFO", "create P1 5 2",
    "create P2 3", "ps", "pcb 1", "set_sched RR 2", "set_speed 50",
    "start", "pause", "set_sched SRTF", "start", "gantt", "stats",
    "exit extra", "help", "exit", "create NO_DEBE_EXISTIR 1", "",
])
result = subprocess.run([binary], input=commands, text=True, capture_output=True, timeout=5)
assert result.returncode == 0, result.stderr
for expected in ["PID: 1", "PID: 2", "CONTROL BLOCK", "P1", "P2",
                 "DIAGRAMA DE GANTT", "ESTADISTICAS", "Planificador: SRTF"]:
    assert expected in result.stdout, (expected, result.stdout)
assert result.stdout.count("[ERROR") == 4, result.stdout
assert "NO_DEBE_EXISTIR" not in result.stdout
for commands in ["", "start\n", "set_speed 5000\nstart\n", "pause\n"]:
    result = subprocess.run([binary], input=commands, text=True, capture_output=True, timeout=2)
    assert result.returncode == 0, result.stderr
print("OK: CLI real, validacion, exit y EOF")

# Real CSV parser: failures are atomic, including the valid rows before an error.
import tempfile
from pathlib import Path
import time

with tempfile.TemporaryDirectory() as directory:
    workload = Path(directory) / "workload.txt"
    invalid_files = [
        "", "# Solo comentarios\n", "Valido, 1, 3, 0\nMalo, 2, 3\n",
        "X, 1, 3, 0, extra\n", "X, 1, 3, 0,\n", "X, 0, 3, 0\n",
        "X, 1, 6, 0\n", "X, 1, 3, -1\n", "X, 1.5, 3, 0\n",
        "Dos nombres, 1, 3, 0\n", ", 1, 3, 0\n", "X, 1, 3, 2x\n",
        "X, 9999999999999999999999999, 3, 0\n",
    ]
    for content in invalid_files:
        workload.write_text(content)
        result = subprocess.run([binary], input=f"load {workload}\ncreate Control 1\nexit\n",
                                text=True, capture_output=True, timeout=3)
        assert result.returncode == 0, result.stderr
        assert "[ERROR" in result.stdout and "PID: 1" in result.stdout, result.stdout
        assert "Procesos cargados:" not in result.stdout, result.stdout
    workload.write_bytes(b"# NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME\r\n"
                         b" Job_A , 4 , 2 , 0\r\n\r\nJob_B,2,1,1\r\nJob_C,1,3,3\r\n")
    process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    process.stdin.write(f"set_sched SRTF\nload {workload}\nset_speed 50\nstart\n")
    process.stdin.flush()
    # Wait for observed completion instead of assuming host scheduling latency.
    import select
    import os
    output = b""
    deadline = time.monotonic() + 5
    completed = False
    while time.monotonic() < deadline:
        process.stdin.write("stats\n")
        process.stdin.flush()
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            output += os.read(process.stdout.fileno(), 65536)
        if b"3 / 3" in output:
            completed = True
            break
    process.stdin.write("pause\ngantt\npcb 1\nstats\nexit\n")
    process.stdin.flush()
    tail, errors = process.communicate(timeout=3)
    output = output.decode() + tail
    assert completed and process.returncode == 0, (output, errors)
    assert "Procesos cargados: 3" in output, output
    expected = "[0-1 ticks: Job_A] -> [1-3 ticks: Job_B] -> [3-4 ticks: Job_C] -> [4-7 ticks: Job_A]"
    assert expected in output, output
    for metric in ["1.00 ticks", "3.33 ticks", "0.00 ticks"]:
        assert metric in output, output
    assert "[ERROR" not in output, output
print("OK: CSV estricto, carga atomica y escenario batch SRTF real")
