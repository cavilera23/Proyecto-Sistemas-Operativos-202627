#include "gui.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifdef SIM_WITH_QT
#include <QApplication>
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
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

struct ViewModel {
    Tick clock_value = 0, busy_value = 0, quantum_value = 2;
    Policy policy_value = Policy::RR;
    std::map<int, PCB> pcbs;
    std::vector<Segment> segments;
    Tick clock() const { return clock_value; }
    Tick busy_ticks() const { return busy_value; }
    Tick quantum() const { return quantum_value; }
    Policy policy() const { return policy_value; }
    const std::map<int, PCB>& processes() const { return pcbs; }
    const std::vector<Segment>& history() const { return segments; }
};
struct View {
    ViewModel model;
    bool active = false;
    int milliseconds = 1000;
};

View make_view(const MonitorSnapshot& snapshot) {
    return {{snapshot.model.clock(), snapshot.model.busy_ticks(), snapshot.model.quantum(),
             snapshot.model.policy(), snapshot.model.processes(), snapshot.model.history()},
            snapshot.active, snapshot.milliseconds};
}

QByteArray encode(const View& view, bool show) {
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << show << view.active << qint32(view.milliseconds)
        << qint64(view.model.clock()) << qint64(view.model.busy_ticks())
        << qint64(view.model.quantum()) << qint32(view.model.policy())
        << quint32(view.model.pcbs.size());
    for (const auto& [pid, pcb] : view.model.pcbs) {
        out << qint32(pid) << QString::fromStdString(pcb.name) << qint32(pcb.priority)
            << qint64(pcb.burst) << qint64(pcb.remaining) << qint64(pcb.arrival)
            << qint64(pcb.waiting) << qint64(pcb.first_run.value_or(-1))
            << qint64(pcb.completion.value_or(-1)) << qint32(pcb.state);
    }
    out << quint32(view.model.segments.size());
    for (const auto& segment : view.model.segments)
        out << qint64(segment.begin) << qint64(segment.end) << qint32(segment.pid);
    QByteArray frame;
    QDataStream header(&frame, QIODevice::WriteOnly);
    header << payload;
    return frame;
}

bool decode(const QByteArray& payload, View& view, bool& show) {
    QDataStream in(payload);
    qint32 speed, policy;
    qint64 clock, busy, quantum;
    quint32 count;
    in >> show >> view.active >> speed >> clock >> busy >> quantum >> policy >> count;
    view.milliseconds = speed;
    view.model.clock_value = clock;
    view.model.busy_value = busy;
    view.model.quantum_value = quantum;
    view.model.policy_value = static_cast<Policy>(policy);
    view.model.pcbs.clear();
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        qint32 pid, priority, state;
        qint64 burst, remaining, arrival, waiting, first, completion;
        QString name;
        in >> pid >> name >> priority >> burst >> remaining >> arrival >> waiting
           >> first >> completion >> state;
        view.model.pcbs.emplace(pid, PCB{pid, name.toStdString(), priority, burst,
            remaining, arrival, waiting, first < 0 ? std::nullopt : std::optional<Tick>(first),
            completion < 0 ? std::nullopt : std::optional<Tick>(completion), static_cast<State>(state)});
    }
    in >> count;
    view.model.segments.clear();
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        qint64 begin, end;
        qint32 pid;
        in >> begin >> end >> pid;
        view.model.segments.push_back({begin, end, pid});
    }
    return in.status() == QDataStream::Ok;
}


QColor process_color(int pid) {
    return pid ? QColor::fromHsv((static_cast<qint64>(pid) * 137) % 360, 125, 210)
               : QColor(110, 120, 135);
}

QString ticks(const std::optional<Tick>& value, Tick arrival) {
    return value ? QString::number(*value - arrival) : QStringLiteral("—");
}

class Timeline final : public QWidget {
public:
    explicit Timeline(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(125);
        setObjectName("gantt");
    }
    void set_model(const ViewModel& model) {
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
    explicit Dashboard(std::function<View()> provider) : provider_(std::move(provider)) {
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
        table_->setHorizontalHeaderLabels({"PID", "Nombre", "Estado", "Prioridad", "AT",
                                           "BT", "Restante", "WT", "TAT", "RT"});
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
    std::function<View()> provider_;
    QLabel* summary_;
    QLabel* metrics_;
    QTableWidget* table_;
    Timeline* timeline_;

    void refresh() {
        const auto snapshot = provider_();
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
    return std::make_unique<Dashboard>([&runtime] { return make_view(runtime.monitor_snapshot()); });
}

void request_gui() {
    const auto present = [](const char* key) { const char* value = std::getenv(key); return value && *value; };
    if (!present("DISPLAY") && !present("WAYLAND_DISPLAY") && !present("QT_QPA_PLATFORM"))
        throw std::runtime_error("No hay una sesion grafica disponible para abrir gui.");
    requested.store(true);
}

class MonitorProcess {
public:
    pid_t pid = -1;
    int socket = -1;
    QByteArray pending;
    qsizetype offset = 0;
    ~MonitorProcess() { stop(); }

    bool start(const char* executable) {
        int sockets[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;
        for (int fd : sockets) {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1 ||
                fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
                ::close(sockets[0]); ::close(sockets[1]); return false;
            }
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        }
        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) != 0) {
            ::close(sockets[0]); ::close(sockets[1]); return false;
        }
        int result = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
        if (!result && sockets[0] != STDIN_FILENO) result = posix_spawn_file_actions_addclose(&actions, sockets[0]);
        if (!result && sockets[1] != STDIN_FILENO) result = posix_spawn_file_actions_addclose(&actions, sockets[1]);
        char option[] = "--gui-monitor";
        char* args[] = {const_cast<char*>(executable), option, nullptr};
        if (!result) result = posix_spawnp(&pid, executable, &actions, nullptr, args, environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(sockets[1]);
        if (result) { ::close(sockets[0]); pid = -1; return false; }
        socket = sockets[0];
        return true;
    }

    bool alive() {
        if (pid < 0) return false;
        int status;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == 0 || (result < 0 && errno == EINTR)) return true;
        pid = -1;
        if (socket >= 0) { ::close(socket); socket = -1; }
        pending.clear(); offset = 0;
        return false;
    }

    void flush() {
        if (socket < 0 || pending.isEmpty()) return;
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = ::send(socket, pending.constData() + offset,
                                  pending.size() - offset, flags);
        if (count > 0) {
            offset += count;
            if (offset == pending.size()) { pending.clear(); offset = 0; }
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            ::close(socket); socket = -1;
        }
    }

    void stop() {
        if (socket >= 0) { ::close(socket); socket = -1; }
        for (int i = 0; pid >= 0 && i < 50; ++i) {
            if (!alive()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pid >= 0) {
            ::kill(pid, SIGTERM);
            for (int i = 0; i < 50 && alive(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pid >= 0) {
            ::kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
    }
};

void gui_event_loop(Runtime& runtime, const std::atomic<bool>& cli_finished, const char* executable) {
    MonitorProcess monitor;
    bool show = false;
    auto next_update = std::chrono::steady_clock::now();
    while (!cli_finished.load()) {
        try {
            if (monitor.pid >= 0 && !monitor.alive())
                std::cerr << "[ERROR GUI]: El monitor se cerro. La consola sigue disponible; puede reintentar gui.\n";
            if (requested.exchange(false)) {
                if (monitor.pid < 0 && !monitor.start(executable))
                    std::cerr << "[ERROR GUI]: No se pudo iniciar el monitor.\n";
                else show = true;
            }
            if (monitor.pid >= 0) {
                const auto now = std::chrono::steady_clock::now();
                if (monitor.pending.isEmpty() && (show || now >= next_update)) {
                    monitor.pending = encode(make_view(runtime.monitor_snapshot()), show);
                    show = false;
                    next_update = now + std::chrono::milliseconds(100);
                }
                monitor.flush();
            }
        } catch (const std::exception& ex) {
            std::cerr << "[ERROR GUI]: " << ex.what() << '\n';
            monitor.stop();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int gui_monitor_main() {
    qunsetenv("QT_QPA_PLATFORMTHEME");
    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "[Qt] %s\n", message.toLocal8Bit().constData());
        if (type == QtFatalMsg) ::_exit(EXIT_FAILURE);
    });
    try {
        int argc = 1;
        char name[] = "simulator-monitor";
        char* argv[] = {name, nullptr};
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        View latest;
        Dashboard window([&] { return latest; });
        QByteArray buffer;
        QTimer receiver;
        QObject::connect(&receiver, &QTimer::timeout, &app, [&] {
            char bytes[16384];
            for (int i = 0; i < 64; ++i) {
                const auto count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
                if (count > 0) buffer.append(bytes, static_cast<int>(count));
                else if (count == 0) { app.quit(); return; }
                else if (errno == EINTR) continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                else { app.quit(); return; }
            }
            while (buffer.size() >= 4) {
                const auto size = qFromBigEndian<quint32>(buffer.constData());
                if (size > 64 * 1024 * 1024) { app.quit(); return; }
                if (buffer.size() - 4 < static_cast<qsizetype>(size)) break;
                bool show = false;
                View next;
                if (!decode(buffer.mid(4, size), next, show)) { app.quit(); return; }
                buffer.remove(0, size + 4);
                latest = std::move(next);
                if (show) { window.showNormal(); window.raise(); window.activateWindow(); }
            }
        });
        receiver.start(20);
        return app.exec();
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR GUI]: " << ex.what() << '\n';
        return 1;
    }
}
} // namespace sim
#else
namespace sim {
void request_gui() {
    throw std::runtime_error("GUI no disponible en esta compilacion. Instale Qt Widgets y recompile con make GUI=1.");
}
int gui_monitor_main() { return 1; }
void gui_event_loop(Runtime&, const std::atomic<bool>& cli_finished, const char*) {
    while (!cli_finished.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
} // namespace sim
#endif
