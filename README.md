## Compilar y ejecutar

Requiere `g++` con C++23, Make y pthread;
La GUI requiere Qt 6 o Qt 5 Widgets y `pkg-config`.

```sh
make
./build/simulator
```

Qt se detecta automáticamente. `make GUI=1` exige GUI; `make GUI=0` compila
solo consola. El sistema inicia pausado, con RR, quantum 2 y 1000 ms/tick.

## Comandos

| Comando | Función |
|---|---|
| `create <nombre> <burst> [prioridad]` | Nombre sin espacios, burst positivo, prioridad 1–5 (defecto 3). |
| `load <archivo>` | Carga CSV validado; un error rechaza todo el lote sin consumir PID. |
| `ps` / `pcb <PID>` | Consulta procesos y PCB. |
| `set_sched RR [quantum]` / `set_sched SRTF` | Cambia la política; quantum positivo, defecto 2. |
| `set_speed <ms>` | Retardo por tick entre 50 y 5000 ms. |
| `start` / `pause` | Reanuda o congela el reloj. |
| `gantt` / `stats` | Historial de CPU y métricas. |
| `gui` | Abre o vuelve a mostrar el monitor. |
| `help` / `exit` | Ayuda o cierre limpio; EOF también cierra. |

## Ejemplo batch

```text
set_sched SRTF
load tests/workload_test.txt
gui
start
```

`gui` puede omitirse. Tras finalizar los procesos, usa `pause`, `gantt` y `stats`.
Si el motor sigue activo después, registra IDLE.

CSV: `nombre, burst, prioridad, arrival`. Admite líneas vacías, comentarios con
`#` al inicio y espacios alrededor de campos. Arrival es un tick absoluto no
negativo, nunca anterior al reloj al confirmar la carga.

## GUI

Muestra procesos, tiempos del PCB, política, reloj, pausa, velocidad, métricas
y Gantt; se actualiza cada 100 ms.
La consola sigue operativa; cerrar la ventana mantiene la simulación y `gui`
la reabre. `exit` cierra ambos.

## Modelo y estructura

`src/model.*` contiene PCB y planificación; `runtime.*`, motor y sincronización;
`cli.*`, comandos; `gui.*`, monitor y comunicación; `main.cpp`, arranque y cierre.
`tests/` contiene las pruebas y el workload del enunciado.

## Pruebas

```sh
make test       # Motor, fronteras de tick, batch, métricas, CLI y concurrencia
make gui-test   # Monitor offscreen, reapertura, cierre y fallo de Qt
make sanitize   # AddressSanitizer y UndefinedBehaviorSanitizer
```

La prueba gráfica guarda `build/gui-preview.png`.
