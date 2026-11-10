# Simulador de Kernel — Fase I

Implementación modular en C++17 para sistemas POSIX (Linux, WSL y macOS).
Requiere `g++`, Make y soporte pthread. Las pruebas de consola requieren Python 3.

```sh
make
./build/simulator
make test
make sanitize
```

## Estructura

- `src/model.hpp/.cpp`: PCB, estados, cola de listos, RR, SRTF y reloj discreto.
- `src/runtime.hpp/.cpp`: hilo motor, mutex, condición y operaciones sincronizadas.
- `src/cli.hpp/.cpp`: comandos, validación y presentación de copias coherentes.
- `src/main.cpp`: arranque de dos hilos POSIX (CLI y motor) y cierre mediante join.
- `tests/`: pruebas deterministas del motor y pruebas del ejecutable.

## Ejemplo

```text
set_sched RR 2
set_speed 1000
create P1 5 2
create P2 3 1
create P3 2 3
start
ps
pause
pcb 1
set_sched SRTF
start
gantt
stats
exit
```

El sistema inicia pausado, con RR, quantum 2 y 1000 ms por tick. `help` enumera
los comandos. Los nombres no pueden contener espacios. La prioridad está entre
1 y 5 (defecto 3). `set_speed` admite 50–5000 ms y afecta al siguiente intervalo.
`set_sched RR` usa quantum 2 si se omite; SRTF no acepta quantum. Todos los
comandos rechazan argumentos adicionales, enteros parciales y desbordamientos.
EOF también cierra limpiamente.

## Reglas de simulación

Los tiempos son ticks enteros: el tiempo real solo controla la presentación.
El motor avanza incluso en IDLE mientras esté iniciado. Cada paso consume el
intervalo `[t,t+1)`. Las creaciones llegan al reloj actual; SRTF evalúa la nueva
llegada antes de consumir el siguiente tick.

RR mantiene FIFO y reencola al vencer el quantum; finalizar tiene precedencia
sobre reencolar. SRTF desapropia solo por un restante estrictamente menor; los
empates conservan al actual. Entre listos se desempata por llegada y PID.
La prioridad se conserva en el PCB y no sustituye el criterio de SRTF.
El cambio de algoritmo conserva las colas y PCB, reinicia el contador del
quantum y se aplica a la siguiente decisión de CPU.

`pause` congela inmediatamente el reloj y marca PAUSED al proceso actual;
`start` lo restaura y comienza un intervalo real completo. Una pausa global no
acumula espera ni cambia el historial simulado. El modelo contempla NEW, READY,
RUNNING, PAUSED y TERMINATED; no se inventan operaciones de E/S bloqueante que
el enunciado no solicita. NEW es transitorio durante create.

Un único `pthread_mutex_t` protege procesos, colas, reloj y configuración.
Las consultas copian el modelo bajo el cerrojo e imprimen fuera de él. El motor
espera pausado mediante `pthread_cond_wait`, y realiza los retardos reales fuera
del mutex, en fragmentos de hasta 10 ms para responder al cierre. No imprime
sobre el prompt de la CLI. El propietario une los hilos antes de destruir recursos.

Los segmentos de Gantt adyacentes del mismo PID se agrupan; IDLE queda registrado.
WT acumula tiempo en READY, TAT = finalización − llegada y RT = primera ejecución
− llegada. Los promedios incluyen solo procesos finalizados y muestran N/A si no
hay ninguno. Uso de CPU = ticks ocupados / ticks transcurridos; en t=0 vale 0%.
Las unidades se rotulan en ticks, aunque algunos ejemplos del enunciado usen `s`.

## Validación

`make test` comprueba el Gantt RR `1,1,2,2,3,3,1,1,2,1`, con finalizaciones
10/9/6, esperas 5/6/4 y respuestas 0/2/4. Comprueba también la traza SRTF
A 0–1, B 1–3, C 3–4, A 4–7; empates; cambio de política; entradas inválidas;
consultas concurrentes; pausa/reanudación; cierre durante un retardo de 5000 ms;
y cierre con EOF. `make sanitize` ejecuta las pruebas C++ con AddressSanitizer
y UndefinedBehaviorSanitizer. La futura validación de #8 debe añadir el archivo
batch real y conservar las evidencias de los escenarios completos de la sección 7.

Uso de IA en esta entrega: Codex asistió con la propuesta, implementación y pruebas.
El equipo debe revisar los resultados y registrar en el anexo obligatorio las
herramientas, los prompts principales y la validación humana efectivamente realizada.
