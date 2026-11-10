#include "gui.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QPixmap>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void wait_events(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        sim::Runtime runtime;
        runtime.launch();
        runtime.set_policy(sim::Policy::SRTF, 2);
        runtime.load({{"Job_A", 4, 2, 0}, {"Job_B", 2, 1, 1}, {"Job_C", 1, 3, 3}});
        runtime.set_speed(50);
        auto window = sim::create_gui_window(runtime);
        window->show();
        wait_events(120);
        auto* table = window->findChild<QTableWidget*>("processes");
        auto* summary = window->findChild<QLabel*>("summary");
        auto* metrics = window->findChild<QLabel*>("metrics");
        auto* gantt = window->findChild<QWidget*>("gantt");
        require(table && summary && metrics && gantt, "Faltan componentes del monitor");
        require(table->rowCount() == 3 && table->item(1, 2)->text() == "NEW", "Llegadas futuras incorrectas");
        require(summary->text().contains("Pausado") && summary->text().contains("SRTF"), "Estado inicial incorrecto");
        require(metrics->text().contains("WT promedio: —"), "Metricas prematuras inventadas");
        runtime.start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!runtime.snapshot().processes().at(3).completion ||
               !runtime.snapshot().processes().at(1).completion) {
            require(std::chrono::steady_clock::now() < deadline, "Motor no termino");
            wait_events(10);
        }
        runtime.pause();
        wait_events(150);
        require(table->item(0, 2)->text() == "TERMINATED", "Tabla no se actualizo");
        require(table->item(0, 7)->text() == "3" && table->item(0, 8)->text() == "7", "PCB grafico incorrecto");
        require(metrics->text().contains("WT promedio: 1.00") &&
                metrics->text().contains("TAT promedio: 3.33"), "Promedios graficos incorrectos");
        require(gantt->toolTip().contains("[4–7: Job_A]"), "Gantt grafico incorrecto");
        const auto frozen = summary->text();
        wait_events(150);
        require(summary->text() == frozen, "Pausa no congelo el monitor");
        window->close();
        require(!window->isVisible(), "Ventana no cerro");
        runtime.create("Nuevo_desde_CLI", 2, 3);
        window->show();
        wait_events(150);
        require(window->isVisible() && table->rowCount() == 4, "Reapertura no actualizo la tabla");
        require(window->grab().save("build/gui-preview.png"), "No se pudo guardar vista de prueba");
        runtime.shutdown();
        runtime.join();
        require(runtime.error().empty(), "Error del motor");
        std::cout << "OK: monitor, PCB, metricas, Gantt, pausa y reapertura\n";
    } catch (const std::exception& ex) {
        std::cerr << "FALLO GUI: " << ex.what() << '\n';
        return 1;
    }
}
