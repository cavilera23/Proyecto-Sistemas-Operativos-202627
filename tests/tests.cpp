#include "cli.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> execution(const sim::Model& model) {
    std::vector<int> result;
    for (const auto& segment : model.history())
        for (auto tick = segment.begin; tick < segment.end; ++tick) result.push_back(segment.pid);
    return result;
}

void invariant(const sim::Model& model) {
    int running = 0;
    sim::Tick consumed = 0;
    for (const auto& [pid, pcb] : model.processes()) {
        require(pid == pcb.pid && pcb.remaining >= 0 && pcb.remaining <= pcb.burst, "PCB corrupto");
        running += pcb.state == sim::State::Running;
        consumed += pcb.burst - pcb.remaining;
        require((pcb.state == sim::State::Terminated) == (pcb.remaining == 0), "Finalizacion invalida");
        if (pcb.completion) {
            require(pcb.first_run.has_value(), "Finalizado sin primera ejecucion");
            require(*pcb.completion - pcb.arrival == pcb.waiting + pcb.burst, "WT/TAT incorrectos");
        }
    }
    require(running <= 1, "Dos procesos ejecutando a la vez");
    require(consumed == model.busy_ticks(), "Tiempo CPU inconsistente");
}

void rr() {
    sim::Model model;
    require(model.create("P1", 5, 2) == 1, "Primer PID incorrecto");
    require(model.create("P2", 3, 1) == 2, "Segundo PID incorrecto");
    model.create("P3", 2, 3);
    for (int i = 0; i < 10; ++i) { model.step(); invariant(model); }
    require(execution(model) == std::vector<int>({1,1,2,2,3,3,1,1,2,1}), "Gantt RR incorrecto");
    const auto& processes = model.processes();
    require(processes.at(1).completion == 10 && processes.at(2).completion == 9 &&
            processes.at(3).completion == 6, "Tiempos finales RR incorrectos");
    require(processes.at(1).waiting == 5 && processes.at(2).waiting == 6 &&
            processes.at(3).waiting == 4, "Espera RR incorrecta");
    require(processes.at(1).first_run == 0 && processes.at(2).first_run == 2 &&
            processes.at(3).first_run == 4, "Respuesta RR incorrecta");
    model.step();
    require(model.history().back().pid == 0 && model.busy_ticks() == 10 && model.clock() == 11,
            "IDLE incorrecto");

    sim::Model boundary;
    boundary.create("Antes", 1);
    boundary.create("Exacto", 2);
    boundary.create("Despues", 3);
    for (int i = 0; i < 6; ++i) { boundary.step(); invariant(boundary); }
    require(execution(boundary) == std::vector<int>({1,2,2,3,3,3}), "Limites del quantum incorrectos");

    sim::Model unit;
    unit.set_policy(sim::Policy::RR, 1);
    unit.create("A", 2);
    unit.create("B", 2);
    for (int i = 0; i < 4; ++i) unit.step();
    require(execution(unit) == std::vector<int>({1,2,1,2}), "Quantum 1 incorrecto");
}

void srtf() {
    sim::Model model;
    model.set_policy(sim::Policy::SRTF);
    model.create("Job_A", 4, 2);
    model.step();
    model.create("Job_B", 2, 1); // Arrival t=1, no batch parser needed.
    model.step();
    model.step();
    model.create("Job_C", 1, 3); // Arrival t=3.
    for (int i = 0; i < 4; ++i) { model.step(); invariant(model); }
    require(execution(model) == std::vector<int>({1,2,2,3,1,1,1}), "Traza SRTF incorrecta");
    require(model.processes().at(1).waiting == 3 && model.processes().at(2).waiting == 0 &&
            model.processes().at(3).waiting == 0, "Metricas SRTF incorrectas");

    sim::Model tie;
    tie.set_policy(sim::Policy::SRTF);
    tie.create("Largo", 6);
    tie.step();
    tie.create("Corto", 2, 5);
    tie.step();
    tie.create("Igual", 1, 1);
    tie.step();
    require(tie.processes().at(2).completion == 3, "Un empate desapropio al actual");
    tie.step();
    require(tie.processes().at(3).completion == 4, "La prioridad altero SRTF");

    sim::Model ready_tie;
    ready_tie.set_policy(sim::Policy::SRTF);
    ready_tie.create("Primero", 2, 5);
    ready_tie.create("Segundo", 2, 1);
    ready_tie.step();
    require(ready_tie.processes().at(1).first_run == 0, "Desempate PID no determinista");
}

void batch() {
    sim::Model model;
    model.set_policy(sim::Policy::SRTF);
    model.load({{"Job_A", 4, 2, 0}, {"Job_B", 2, 1, 1}, {"Job_C", 1, 3, 3}});
    require(model.processes().at(2).state == sim::State::New, "Llegada futura no es NEW");
    for (int i = 0; i < 7; ++i) { model.step(); invariant(model); }
    require(execution(model) == std::vector<int>({1,2,2,3,1,1,1}), "Batch SRTF incorrecto");
    require(model.processes().at(1).waiting == 3, "Espera batch incorrecta");
    try {
        model.load({{"Valido", 1, 3, 7}, {"Pasado", 1, 3, 6}});
        require(false, "Se acepto llegada pasada");
    } catch (const std::invalid_argument&) {}
    require(model.processes().size() == 3 && model.create("Siguiente", 1) == 4,
            "Carga fallida altero procesos o PID");
    sim::Model idle;
    idle.load({{"Futuro", 1, 3, 3}});
    for (int i = 0; i < 4; ++i) { idle.step(); invariant(idle); }
    require(execution(idle) == std::vector<int>({0,0,0,1}), "No se respeto llegada con IDLE");
    require(idle.processes().at(1).waiting == 0 && idle.processes().at(1).first_run == 3,
            "NEW acumulo espera o respuesta");
    sim::Model rr_arrival;
    rr_arrival.load({{"A", 4, 3, 0}, {"B", 1, 3, 2}});
    for (int i = 0; i < 5; ++i) rr_arrival.step();
    require(execution(rr_arrival) == std::vector<int>({1,1,2,1,1}),
            "Llegada en limite de quantum incorrecta");
    sim::Model unordered;
    unordered.load({{"Tarde", 1, 3, 4}, {"Temprano", 1, 3, 1}});
    for (int i = 0; i < 5; ++i) unordered.step();
    require(execution(unordered) == std::vector<int>({0,2,0,0,1}), "Archivo desordenado incorrecto");
}

void switching() {
    sim::Model model;
    model.create("A", 5);
    model.create("B", 2);
    model.step();
    model.pause();
    require(model.processes().at(1).state == sim::State::Paused, "PAUSED no representado");
    model.set_policy(sim::Policy::SRTF);
    model.resume();
    model.step();
    require(model.processes().at(2).first_run == 1, "Cambio RR a SRTF no desapropio");
    model.set_policy(sim::Policy::RR, 1);
    for (int i = 0; i < 5; ++i) { model.step(); invariant(model); }
    require(model.busy_ticks() == 7 && model.processes().at(1).completion &&
            model.processes().at(2).completion, "Cambio de politica perdio procesos");
}

void parser() {
    sim::Runtime runtime;
    const std::vector<std::string> invalid = {
        "create", "create X", "create X 0", "create X -1", "create X 1 0",
        "create X 1 6", "create X 1 2 extra", "create X 2x", "create X 1.5",
        "create X 9999999999999999999999999999999", "create X 1 9999999999999999999",
        "pcb", "pcb abc", "pcb 0", "pcb -1", "pcb 2147483648", "pcb 1", "pcb 1 extra",
        "set_sched", "set_sched FIFO", "set_sched rr", "set_sched RR 0",
        "set_sched RR -1", "set_sched SRTF 2", "set_sched RR 2 extra",
        "set_speed", "set_speed 49", "set_speed 5001", "set_speed abc",
        "ps extra", "gantt extra", "stats extra", "start extra", "pause extra",
        "help extra", "exit extra", "load", "load a b", "desconocido"
    };
    for (const auto& command : invalid) {
        std::ostringstream out;
        require(sim::execute_command(command, runtime, out), "Entrada invalida termino CLI");
        require(out.str().find("[ERROR") != std::string::npos, "Entrada invalida aceptada");
        require(runtime.snapshot().processes().empty(), "Entrada invalida creo procesos");
    }
    std::ostringstream out;
    sim::execute_command("create P1 5", runtime, out);
    require(runtime.snapshot().processes().at(1).priority == 3, "Prioridad por defecto incorrecta");
    sim::execute_command("set_sched RR", runtime, out);
    require(runtime.snapshot().quantum() == 2, "Quantum por defecto incorrecto");
    for (const auto& command : {"ps", "pcb 1", "gantt", "stats", "help", "load archivo.txt"})
        require(sim::execute_command(command, runtime, out), "Consulta termino CLI");
    require(out.str().find("N/A") != std::string::npos, "Metricas prematuras fabricadas");
    require(out.str().find("[ERROR") != std::string::npos, "Archivo inexistente aceptado");
    require(!sim::execute_command("exit", runtime, out), "exit no termino CLI");
}

void concurrency() {
    using namespace std::chrono;
    sim::Runtime runtime;
    runtime.launch();
    runtime.set_speed(50);
    runtime.create("Inicial", 50, 3);
    runtime.start();
    const auto deadline = steady_clock::now() + seconds(3);
    while (runtime.snapshot().clock() < 2 && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds(5));
    require(runtime.snapshot().clock() >= 2, "Motor no avanzo");
    // CLI-like concurrent creation, policy changes and snapshots while engine runs.
    for (int i = 0; i < 100; ++i) {
        runtime.create("Concurrente", 1, 3);
        runtime.set_policy(i % 2 ? sim::Policy::RR : sim::Policy::SRTF, 2);
        invariant(runtime.snapshot());
    }
    runtime.pause();
    const auto frozen = runtime.snapshot();
    std::this_thread::sleep_for(milliseconds(120));
    require(runtime.snapshot().clock() == frozen.clock(), "pause no congelo el reloj");
    runtime.start();
    runtime.start(); // Idempotent.
    const auto resume_deadline = steady_clock::now() + seconds(2);
    while (runtime.snapshot().clock() == frozen.clock() && steady_clock::now() < resume_deadline)
        std::this_thread::sleep_for(milliseconds(5));
    require(runtime.snapshot().clock() > frozen.clock(), "start no reanudo");
    runtime.shutdown();
    runtime.join();
    require(runtime.error().empty(), "Fallo interno del motor");

    sim::Runtime slow;
    slow.launch();
    slow.set_speed(5000);
    slow.start();
    std::this_thread::sleep_for(milliseconds(30));
    const auto before_exit = steady_clock::now();
    slow.shutdown();
    slow.join();
    require(steady_clock::now() - before_exit < milliseconds(500), "exit espero el tick completo");

    sim::Runtime paused;
    paused.launch();
    paused.shutdown();
    paused.join(); // Wakes a motor blocked in pthread_cond_wait.
}
}

int main() {
    try {
        rr();
        srtf();
        batch();
        switching();
        parser();
        concurrency();
        std::cout << "OK: RR, SRTF, batch, PCB, cambios, CLI y concurrencia\n";
    } catch (const std::exception& ex) {
        std::cerr << "FALLO: " << ex.what() << '\n';
        return 1;
    }
}
