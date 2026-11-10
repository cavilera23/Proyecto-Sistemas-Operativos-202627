"""Exercise the lazy Qt main loop alongside the POSIX CLI and engine."""
import os
import subprocess
import sys
import time

binary = sys.argv[1]
environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
for finish in ("exit\n", ""):
    process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=environment)
    try:
        process.stdin.write("gui extra\ncreate A 10\nset_speed 50\ngui\nstart\n")
        process.stdin.flush()
        time.sleep(0.25)  # Keep stdin open while Qt initializes and processes events.
        assert process.poll() is None, "GUI termino la CLI prematuramente"
        output, errors = process.communicate("gui\npause\nps\n" + finish, timeout=5)
        assert process.returncode == 0, (output, errors)
        assert output.count("Apertura del monitor grafico solicitada.") == 2, output
        assert output.count("[ERROR") == 1 and "ERROR GUI" not in errors, (output, errors)
        assert "BURST RESTANTE" in output, output
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()

# No graphical session must produce a recoverable validation error.
environment = {k: v for k, v in os.environ.items()
               if k not in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM")}
result = subprocess.run([binary], input="gui\ncreate Sigue 1\nexit\n", env=environment,
                        text=True, capture_output=True, timeout=3)
assert result.returncode == 0 and "[ERROR" in result.stdout and "PID: 1" in result.stdout, result
print("OK: gui desde CLI, solicitudes repetidas, exit, EOF y ausencia de sesion grafica")
