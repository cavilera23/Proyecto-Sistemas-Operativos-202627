#include "gui.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifdef SIM_WITH_QT
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace sim {
namespace {
std::atomic<bool> requested{false};

QColor process_color(int pid) {
    return pid ? QColor::fromHsv((static_cast<qint64>(pid) * 137) % 360, 125, 210)
               : QColor(110, 120, 135);
}

QString ticks(const std::optional<Tick>& value, Tick arrival) {
    return value ? QString::number(*value - arrival) : QStringLiteral("—");
}

// Fit the full timeline to the viewport. Tooltips preserve exact intervals when
// a long simulation makes individual segments too narrow to label.
class Timeline final : public QWidget {
public:
    explicit Timeline(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(125);
        setObjectName("gantt");
    }
    void set_model(const Model& model) {
        clock_ = model.clock();
        history_ = model.history();
        names_.clear();
        QStringList intervals;
        for (const auto& [pid, pcb] : model.processes())
            names_[pid] = QString::fromStdString(pcb.name);
        names_[0] = "IDLE";
        for (const auto& segment : history_)
            intervals << QString("[%1–%2: %3]").arg(segment.begin).arg(segment.end)
                         .arg(names_.at(segment.pid));
        setToolTip(intervals.join(" → "));
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int span = qMax(1, width() - 40);
        painter.setPen(palette().color(QPalette::Text));
        if (!clock_) {
            painter.drawText(rect(), Qt::AlignCenter, "Sin ejecución. Inicia la CPU con start en la consola.");
            return;
        }
        painter.drawText(20, 20, QString("0 — %1 ticks").arg(clock_));
        for (const auto& segment : history_) {
            const qreal left = 20 + span * (static_cast<long double>(segment.begin) / clock_);
            const qreal right = 20 + span * (static_cast<long double>(segment.end) / clock_);
            const QRectF box(left, 35, qMax(qreal(1), right - left), 48);
            painter.fillRect(box, process_color(segment.pid));
            painter.setPen(QColor("#18202c"));
            painter.drawRect(box);
            if (box.width() > 55)
                painter.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter,
                    painter.fontMetrics().elidedText(names_.at(segment.pid), Qt::ElideRight,
                                                     static_cast<int>(box.width()) - 8));
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(20, 108, "Coloca el cursor sobre el Gantt para consultar los intervalos exactos.");
    }
private:
    Tick clock_ = 0;
    std::vector<Segment> history_;
    std::map<int, QString> names_;
};

class Dashboard final : public QWidget {
public:
    explicit Dashboard(Runtime& runtime) : runtime_(runtime) {
        setWindowTitle("Simulador de Kernel — Monitor de procesos");
        resize(1150, 680);
        auto* layout = new QVBoxLayout(this);
        auto* title = new QLabel("Monitor del kernel", this);
        auto title_font = title->font();
        title_font.setPointSize(21);
        title_font.setBold(true);
        title->setFont(title_font);
        layout->addWidget(title);
        layout->addWidget(new QLabel("Controla la simulación desde la consola. Esta ventana se actualiza cada 100 ms.", this));
        summary_ = new QLabel(this);
        summary_->setObjectName("summary");
        summary_->setWordWrap(true);
        layout->addWidget(summary_);
        table_ = new QTableWidget(0, 10, this);
        table_->setObjectName("processes");
        table_->setHorizontalHeaderLabels({"PID", "Nombre", "Estado", "Prioridad", "Llegada",
                                           "Ráfaga", "Restante", "WT", "TAT", "RT"});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setAlternatingRowColors(true);
        layout->addWidget(table_, 1);
        metrics_ = new QLabel(this);
        metrics_->setObjectName("metrics");
        metrics_->setWordWrap(true);
        layout->addWidget(metrics_);
        layout->addWidget(new QLabel("Historial de CPU · Gantt", this));
        timeline_ = new Timeline(this);
        layout->addWidget(timeline_);
        layout->addWidget(new QLabel("Tiempos en ticks · WT: espera · TAT: retorno · RT: respuesta · —: aún no disponible", this));
        auto* timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        timer->start(100);
        refresh();
    }
private:
    Runtime& runtime_;
    QLabel* summary_;
    QLabel* metrics_;
    QTableWidget* table_;
    Timeline* timeline_;

    void refresh() {
        // No widget rendering or formatting occurs while holding Runtime's mutex.
        const auto snapshot = runtime_.monitor_snapshot();
        const auto& model = snapshot.model;
        const auto selected = table_->currentRow() >= 0
            ? table_->item(table_->currentRow(), 0)->text() : QString();
        const int scroll = table_->verticalScrollBar()->value();
        table_->setRowCount(static_cast<int>(model.processes().size()));
        int row = 0, finished = 0, ready = 0, future = 0;
        long double waiting = 0, turnaround = 0, response = 0;
        QString current = "IDLE / sin proceso asignado";
        for (const auto& [pid, pcb] : model.processes()) {
            if (pcb.state == State::Running || pcb.state == State::Paused)
                current = QString("P%1 · %2 · %3").arg(pid).arg(QString::fromStdString(pcb.name))
                          .arg(state_name(pcb.state));
            ready += pcb.state == State::Ready;
            future += pcb.state == State::New;
            if (pcb.completion) {
                ++finished;
                waiting += pcb.waiting;
                turnaround += *pcb.completion - pcb.arrival;
                response += *pcb.first_run - pcb.arrival;
            }
            const QStringList cells = {QString::number(pid), QString::fromStdString(pcb.name),
                state_name(pcb.state), QString::number(pcb.priority), QString::number(pcb.arrival),
                QString::number(pcb.burst), QString::number(pcb.remaining), QString::number(pcb.waiting),
                ticks(pcb.completion, pcb.arrival), ticks(pcb.first_run, pcb.arrival)};
            for (int col = 0; col < cells.size(); ++col) {
                auto* cell = table_->item(row, col);
                if (!cell) { cell = new QTableWidgetItem; table_->setItem(row, col, cell); }
                cell->setText(cells[col]);
                cell->setToolTip(cells[col]);
                if (col == 0) cell->setForeground(process_color(pid));
            }
            if (cells[0] == selected) table_->selectRow(row);
            ++row;
        }
        table_->verticalScrollBar()->setValue(scroll);
        summary_->setText(QString("Reloj: %1 ticks   |   %2%3   |   Listos: %4   |   Futuros: %5\nCPU: %6   |   %7   |   %8 ms/tick")
            .arg(model.clock()).arg(model.policy() == Policy::RR ? "RR" : "SRTF")
            .arg(model.policy() == Policy::RR ? QString(" · quantum %1").arg(model.quantum()) : QString())
            .arg(ready).arg(future).arg(current)
            .arg(snapshot.active ? "En marcha" : "Pausado").arg(snapshot.milliseconds));
        const auto average = [finished](long double total) {
            return finished ? QString::number(static_cast<double>(total / finished), 'f', 2) : QString("—");
        };
        const double usage = model.clock() ? 100.0 * model.busy_ticks() / model.clock() : 0;
        metrics_->setText(QString("Finalizados: %1 / %2   |   WT promedio: %3   |   TAT promedio: %4   |   RT promedio: %5   |   CPU: %6%")
            .arg(finished).arg(model.processes().size()).arg(average(waiting))
            .arg(average(turnaround)).arg(average(response)).arg(usage, 0, 'f', 1));
        timeline_->set_model(model);
    }
};
}

std::unique_ptr<QWidget> create_gui_window(Runtime& runtime) {
    return std::make_unique<Dashboard>(runtime);
}

void request_gui() {
    const auto present = [](const char* key) { const char* value = std::getenv(key); return value && *value; };
    if (!present("DISPLAY") && !present("WAYLAND_DISPLAY") && !present("QT_QPA_PLATFORM"))
        throw std::runtime_error("No hay una sesion grafica disponible para abrir gui.");
    requested.store(true);
}

void gui_event_loop(Runtime& runtime, const std::atomic<bool>& cli_finished) {
    try {
        // Lazy initialization keeps the console usable over SSH or without DISPLAY.
        while (!cli_finished.load() && !requested.exchange(false))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (cli_finished.load()) return;
        int argc = 1;
        char name[] = "simulator";
        char* argv[] = {name, nullptr};
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        auto window = create_gui_window(runtime);
        window->show();
        QTimer lifecycle;
        QObject::connect(&lifecycle, &QTimer::timeout, &app, [&] {
            if (cli_finished.load()) app.quit();
            else if (requested.exchange(false)) {
                window->showNormal();
                window->raise();
                window->activateWindow();
            }
        });
        lifecycle.start(20);
        app.exec();
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR GUI]: " << ex.what() << '\n';
        while (!cli_finished.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace sim
#else
namespace sim {
void request_gui() {
    throw std::runtime_error("GUI no disponible en esta compilacion. Instale Qt Widgets y recompile con make GUI=1.");
}
void gui_event_loop(Runtime&, const std::atomic<bool>& cli_finished) {
    while (!cli_finished.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
} // namespace sim
#endif
