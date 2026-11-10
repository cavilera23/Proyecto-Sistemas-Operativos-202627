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
