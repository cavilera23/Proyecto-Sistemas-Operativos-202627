#include "cli.hpp"

#include <charconv>
#include <iomanip>
#include <fstream>
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace sim {
namespace {
Tick number(const std::string& text, Tick minimum, Tick maximum) {
    Tick value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("[ERROR DE TIPO]: Se esperaba un entero valido.");
    if (value < minimum || value > maximum)
        throw std::invalid_argument("[ERROR DE PARAMETRO]: Entero fuera de rango.");
    return value;
}

void count(const std::vector<std::string>& args, std::size_t minimum,
           std::size_t maximum, const char* usage) {
    if (args.size() < minimum || args.size() > maximum)
        throw std::invalid_argument(std::string("[ERROR DE SINTAXIS]: Cantidad de parametros invalida.\nUso correcto: ") + usage);
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::vector<WorkloadEntry> read_workload(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("No se pudo abrir el archivo: " + path);
    std::vector<WorkloadEntry> entries;
    std::string line;
    std::size_t row = 0;
    while (std::getline(file, line)) {
        ++row;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        try {
            std::array<std::string, 4> fields;
            std::size_t begin = 0;
            for (std::size_t i = 0; i < fields.size(); ++i) {
                const auto comma = line.find(',', begin);
                if ((i < 3 && comma == std::string::npos) ||
                    (i == 3 && comma != std::string::npos))
                    throw std::invalid_argument("Se requieren cuatro campos CSV.");
                fields[i] = trim(line.substr(begin, comma == std::string::npos ? comma : comma - begin));
                begin = comma == std::string::npos ? line.size() : comma + 1;
            }
            if (fields[0].empty() || fields[0].find_first_of(" \t\r\n") != std::string::npos)
                throw std::invalid_argument("El nombre debe ser una cadena sin espacios.");
            entries.push_back({fields[0], number(fields[1], 1, INT64_MAX),
                               static_cast<int>(number(fields[2], 1, 5)),
                               number(fields[3], 0, INT64_MAX)});
        } catch (const std::exception& ex) {
            throw std::invalid_argument("Archivo " + path + ", linea " +
                                        std::to_string(row) + ": " + ex.what());
        }
    }
    if (file.bad()) throw std::runtime_error("Error de lectura del archivo: " + path);
    if (entries.empty()) throw std::invalid_argument("El archivo no contiene procesos.");
    return entries;
}

void help(std::ostream& out) {
    out << "create <nombre> <burst> [prioridad]  (prioridad 1-5; defecto 3)\n"
        << "load <archivo>  (CSV: nombre, burst, prioridad, llegada absoluta)\n"
        << "ps | pcb <PID>\n"
        << "set_sched RR [quantum] | set_sched SRTF  (quantum por defecto 2)\n"
        << "set_speed <ms_por_tick>  (50-5000; defecto 1000)\n"
        << "start | pause | gantt | stats | help | exit\n";
}

void show_processes(const Model& model, std::ostream& out) {
    out << "PID    NOMBRE         ESTADO       BURST RESTANTE    PRIORIDAD\n"
        << "--------------------------------------------------------------\n";
    for (const auto& [pid, pcb] : model.processes())
        out << std::left << std::setw(7) << pid << std::setw(15) << pcb.name
            << std::setw(13) << state_name(pcb.state)
            << std::setw(18) << (std::to_string(pcb.remaining) + " ticks")
            << pcb.priority << '\n';
}

void show_pcb(const PCB& pcb, std::ostream& out) {
    out << "==================================================\n"
        << "        CONTROL BLOCK DE PROCESO (PCB) - PID: " << pcb.pid << '\n'
        << "==================================================\n"
        << "Nombre del Proceso : " << pcb.name << '\n'
        << "Estado Actual      : " << state_name(pcb.state) << '\n'
        << "Prioridad          : " << pcb.priority << '\n'
        << "Tiempo de Llegada  : " << pcb.arrival << " ticks\n"
        << "Rafaga Inicial     : " << pcb.burst << " ticks\n"
        << "Rafaga Restante    : " << pcb.remaining << " ticks\n"
        << "Tiempo de Espera   : " << pcb.waiting << " ticks\n"
        << "Tiempo de Retorno  : ";
    if (pcb.completion) out << *pcb.completion - pcb.arrival << " ticks\n";
    else out << "N/A (Sin finalizar)\n";
    out << "Tiempo de Respuesta: ";
    if (pcb.first_run) out << *pcb.first_run - pcb.arrival << " ticks\n";
    else out << "N/A (Sin ejecutar)\n";
    out << "==================================================\n";
}

void show_gantt(const Model& model, std::ostream& out) {
    out << "DIAGRAMA DE GANTT (0 - " << model.clock() << " ticks):\n";
    bool first = true;
    for (const auto& segment : model.history()) {
        if (!first) out << " -> ";
        first = false;
        out << '[' << segment.begin << '-' << segment.end << " ticks: ";
        if (segment.pid) out << model.processes().at(segment.pid).name;
        else out << "IDLE";
        out << ']';
    }
    if (first) out << "(Sin ejecucion)";
    out << '\n';
}

void show_stats(const Model& model, std::ostream& out) {
    std::size_t finished = 0;
    long double waiting = 0, turnaround = 0, response = 0;
    for (const auto& [pid, pcb] : model.processes()) {
        (void)pid;
        if (pcb.completion) {
            ++finished;
            waiting += pcb.waiting;
            turnaround += *pcb.completion - pcb.arrival;
            response += *pcb.first_run - pcb.arrival;
        }
    }
    const auto flags = out.flags();
    const auto precision = out.precision();
    out << "ESTADISTICAS DEL SISTEMA:\n- Procesos Finalizados      : "
        << finished << " / " << model.processes().size() << '\n';
    auto average = [&](const char* label, long double total) {
        out << label;
        if (finished) out << std::fixed << std::setprecision(2) << total / finished << " ticks\n";
        else out << "N/A (Sin procesos finalizados)\n";
    };
    average("- Tiempo Promedio de Espera : ", waiting);
    average("- Tiempo Promedio de Retorno: ", turnaround);
    average("- Tiempo Promedio Respuesta : ", response);
    out << "- Uso de CPU                : " << std::fixed << std::setprecision(1)
        << (model.clock() ? 100.0L * model.busy_ticks() / model.clock() : 0.0L) << "%\n";
    out.flags(flags);
    out.precision(precision);
}
}

bool execute_command(const std::string& line, Runtime& runtime, std::ostream& out) {
    try {
        std::istringstream input(line);
        std::vector<std::string> args;
        for (std::string word; input >> word;) args.push_back(word);
        if (args.empty()) return true;
        const auto& command = args.front();
        constexpr Tick max_tick = INT64_MAX;
        if (command == "create") {
            count(args, 3, 4, "create <nombre> <burst> [prioridad]");
            const Tick burst = number(args[2], 1, max_tick);
            const int priority = args.size() == 4 ? static_cast<int>(number(args[3], 1, 5)) : 3;
            const int pid = runtime.create(args[1], burst, priority);
            out << "Proceso creado. PID: " << pid << '\n';
        } else if (command == "pcb") {
            count(args, 2, 2, "pcb <PID>");
            const int pid = static_cast<int>(number(args[1], 1, INT32_MAX));
            const auto model = runtime.snapshot();
            const auto found = model.processes().find(pid);
            if (found == model.processes().end())
                throw std::invalid_argument("[ERROR DE PARAMETRO]: El PID no existe.");
            show_pcb(found->second, out);
        } else if (command == "set_sched") {
            count(args, 2, 3, "set_sched RR [quantum] | set_sched SRTF");
            if (args[1] != "RR" && args[1] != "SRTF")
                throw std::invalid_argument("[ERROR DE PARAMETRO]: Algoritmos validos: RR, SRTF.");
            if (args[1] == "SRTF") count(args, 2, 2, "set_sched SRTF");
            const Tick quantum = args.size() == 3 ? number(args[2], 1, max_tick) : 2;
            runtime.set_policy(args[1] == "RR" ? Policy::RR : Policy::SRTF, quantum);
            out << "Planificador: " << args[1];
            if (args[1] == "RR") out << "; quantum: " << quantum;
            out << '\n';
        } else if (command == "set_speed") {
            count(args, 2, 2, "set_speed <ms_por_tick>");
            runtime.set_speed(static_cast<int>(number(args[1], 50, 5000)));
            out << "Velocidad: " << args[1] << " ms/tick\n";
        } else if (command == "load") {
            count(args, 2, 2, "load <archivo>");
            const auto entries = read_workload(args[1]);
            const auto loaded = runtime.load(entries);
            out << "Procesos cargados: " << loaded << '\n';
        } else if (command == "ps" || command == "gantt" || command == "stats" ||
                   command == "start" || command == "pause" || command == "help" || command == "exit") {
            count(args, 1, 1, command.c_str());
            if (command == "ps") show_processes(runtime.snapshot(), out);
            else if (command == "gantt") show_gantt(runtime.snapshot(), out);
            else if (command == "stats") show_stats(runtime.snapshot(), out);
            else if (command == "start") { runtime.start(); out << "Simulacion iniciada.\n"; }
            else if (command == "pause") { runtime.pause(); out << "Simulacion pausada.\n"; }
            else if (command == "help") help(out);
            else { runtime.shutdown(); return false; }
        } else {
            out << "[ERROR DE COMANDO]: Comando desconocido. Consulte help.\n";
        }
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (message.rfind("[ERROR", 0) != 0) out << "[ERROR]: ";
        out << message << '\n';
    }
    return true;
}

void* cli_entry(void* context) {
    auto& shell = *static_cast<Shell*>(context);
    try {
        shell.output << "Simulador de Kernel (C++17 / pthread). Escriba help.\n";
        std::string line;
        while (shell.output << "> " << std::flush, std::getline(shell.input, line)) {
            if (!execute_command(line, shell.runtime, shell.output)) break;
        }
        if (shell.input.bad()) shell.status = 1;
    } catch (const std::exception& ex) {
        shell.status = 1;
        shell.output << "[ERROR CLI]: " << ex.what() << '\n';
    }
    shell.runtime.shutdown();
    return nullptr;
}
} // namespace sim
