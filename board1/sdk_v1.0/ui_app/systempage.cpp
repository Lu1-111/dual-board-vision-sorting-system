#include <QShowEvent>
#include "systempage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStorageInfo>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLabel *makeLabel(const QString &text, const char *name)
{
    QLabel *result = new QLabel(text);
    result->setObjectName(QString::fromLatin1(name));
    result->setWordWrap(true);
    return result;
}

QFrame *metricCard(const QString &caption, QLabel **value, QProgressBar **bar)
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(caption, "muted"));
    *value = makeLabel(QStringLiteral("采样中"), "statValue");
    layout->addWidget(*value);
    if (bar) {
        *bar = new QProgressBar;
        (*bar)->setRange(0, 100);
        (*bar)->setValue(0);
        (*bar)->setTextVisible(false);
        layout->addWidget(*bar);
    }
    return card;
}

QString processState(const QString &state)
{
    if (state == QStringLiteral("R")) return QStringLiteral("运行");
    if (state == QStringLiteral("S")) return QStringLiteral("休眠");
    if (state == QStringLiteral("D")) return QStringLiteral("等待 I/O");
    if (state == QStringLiteral("Z")) return QStringLiteral("僵尸");
    if (state == QStringLiteral("T") || state == QStringLiteral("t"))
        return QStringLiteral("停止");
    return state;
}

QString memoryText(double mib)
{
    return QString::number(mib, 'f', 1) + QStringLiteral(" MiB");
}

} // namespace

SystemPage::SystemPage(QWidget *parent)
    : QWidget(parent), cpuValue_(nullptr), memoryValue_(nullptr),
      processValue_(nullptr), diskValue_(nullptr), uptimeValue_(nullptr),
      cpuBar_(nullptr), memoryBar_(nullptr), diskBar_(nullptr),
      search_(new QLineEdit), table_(new QTableWidget), timer_(new QTimer(this))
{
    setObjectName(QStringLiteral("systemPage"));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(17);
    layout->addWidget(makeLabel(QStringLiteral("系统监控"), "pageTitle"));
    layout->addWidget(makeLabel(
        QStringLiteral("读取本机 Linux 状态；每 2 秒刷新一次，进程列表仅供查看。"), "muted"));

    QHBoxLayout *metrics = new QHBoxLayout;
    metrics->setSpacing(14);
    metrics->addWidget(metricCard(QStringLiteral("CPU 使用率"), &cpuValue_, &cpuBar_));
    metrics->addWidget(metricCard(QStringLiteral("内存使用"), &memoryValue_, &memoryBar_));
    metrics->addWidget(metricCard(QStringLiteral("进程数量"), &processValue_, nullptr));
    metrics->addWidget(metricCard(QStringLiteral("根分区使用"), &diskValue_, &diskBar_));
    layout->addLayout(metrics);

    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->addWidget(makeLabel(QStringLiteral("进程列表"), "sectionTitle"));
    toolbar->addStretch();
    uptimeValue_ = makeLabel(QString(), "muted");
    toolbar->addWidget(uptimeValue_);
    search_->setPlaceholderText(QStringLiteral("按 PID 或进程名称搜索"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(260);
    toolbar->addSpacing(18);
    toolbar->addWidget(search_);
    QPushButton *refreshButton = new QPushButton(QStringLiteral("刷新"));
    refreshButton->setObjectName(QStringLiteral("secondaryButton"));
    toolbar->addWidget(refreshButton);
    layout->addLayout(toolbar);

    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("PID"),
        QStringLiteral("进程名称"), QStringLiteral("CPU %"),
        QStringLiteral("内存"), QStringLiteral("状态")});
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(43);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);
    layout->addWidget(makeLabel(
        QStringLiteral("CPU % 按单核 100% 计算；多线程进程可能超过 100%。"),
        "muted"));

    connect(search_, &QLineEdit::textChanged, this,
            [this]() { renderProcesses(); });
    connect(refreshButton, &QPushButton::clicked, this,
            [this]() { refresh(); });
    connect(timer_, &QTimer::timeout, this,
            [this]() { refresh(); });
    timer_->start(2000);
    refresh();
}

bool SystemPage::readCpu(quint64 *total, quint64 *idle, int *cores) const
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> lines = file.readAll().split('\n');
    if (lines.isEmpty()) return false;
    const QList<QByteArray> parts = lines.first().simplified().split(' ');
    if (parts.size() < 6 || parts.first() != "cpu") return false;
    *total = 0;
    for (int i = 1; i < parts.size(); ++i) {
        bool ok = false;
        const quint64 value = parts.at(i).toULongLong(&ok);
        if (!ok) return false;
        *total += value;
    }
    *idle = parts.at(4).toULongLong() + parts.at(5).toULongLong();
    *cores = 0;
    for (const QByteArray &line : lines) {
        if (line.startsWith("cpu") && line.size() > 3
            && line.at(3) >= '0' && line.at(3) <= '9') ++*cores;
    }
    if (*cores == 0) *cores = qMax(1, QThread::idealThreadCount());
    return true;
}

QVector<ProcessInfo> SystemPage::readProcesses() const
{
    QVector<ProcessInfo> result;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool validPid = false;
        const qint64 pid = entry.toLongLong(&validPid);
        if (!validPid || pid <= 0) continue;
        QFile statFile(QStringLiteral("/proc/%1/stat").arg(entry));
        if (!statFile.open(QIODevice::ReadOnly)) continue;
        const QByteArray raw = statFile.readAll().trimmed();
        const int open = raw.indexOf('(');
        const int close = raw.lastIndexOf(')');
        if (open < 0 || close <= open || close + 2 >= raw.size()) continue;
        const QList<QByteArray> fields = raw.mid(close + 2).simplified().split(' ');
        if (fields.size() <= 12) continue;
        bool okUser = false;
        bool okSystem = false;
        const quint64 userTicks = fields.at(11).toULongLong(&okUser);
        const quint64 systemTicks = fields.at(12).toULongLong(&okSystem);
        if (!okUser || !okSystem) continue;

        ProcessInfo process;
        process.pid = pid;
        process.name = QString::fromLocal8Bit(raw.mid(open + 1, close - open - 1));
        process.state = processState(QString::fromLatin1(fields.at(0)));
        process.ticks = userTicks + systemTicks;
        QFile statusFile(QStringLiteral("/proc/%1/status").arg(entry));
        if (statusFile.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> lines = statusFile.readAll().split('\n');
            for (const QByteArray &line : lines) {
                if (line.startsWith("VmRSS:")) {
                    process.memoryMiB = line.mid(6).simplified().split(' ')
                        .first().toDouble() / 1024.0;
                    break;
                }
            }
        }
        result.append(process);
    }
    return result;
}

void SystemPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh(); // First visible frame gets a real sample, not stale hidden data.
}

void SystemPage::refresh()
{
    // This page shares the arm controller's GUI thread. A hidden monitoring
    // page must not scan /proc, query storage or rebuild its table every 2 s.
    if (!isVisible()) return;
    ++sampleCount_;
    quint64 total = 0;
    quint64 idle = 0;
    int cores = 1;
    const bool cpuAvailable = readCpu(&total, &idle, &cores);
    if (cpuAvailable && previousTotal_ > 0 && total > previousTotal_) {
        const quint64 totalDelta = total - previousTotal_;
        const quint64 idleDelta = idle >= previousIdle_ ? idle - previousIdle_ : 0;
        const double usage = 100.0 * double(totalDelta - qMin(totalDelta, idleDelta))
                             / double(totalDelta);
        cpuValue_->setText(QString::number(usage, 'f', 1) + QStringLiteral("%"));
        cpuBar_->setValue(qRound(usage));
    } else {
        cpuValue_->setText(cpuAvailable ? QStringLiteral("采样中")
                                        : QStringLiteral("不可用"));
        cpuBar_->setValue(0);
    }

    processes_ = readProcesses();
    QHash<qint64, quint64> currentTicks;
    const quint64 interval = cpuAvailable && total > previousTotal_
                             ? total - previousTotal_ : 0;
    for (ProcessInfo &process : processes_) {
        if (interval > 0 && previousProcessTicks_.contains(process.pid)
            && process.ticks >= previousProcessTicks_.value(process.pid)) {
            const quint64 delta = process.ticks
                                  - previousProcessTicks_.value(process.pid);
            process.cpuPercent = 100.0 * double(delta) * cores / double(interval);
        }
        currentTicks.insert(process.pid, process.ticks);
    }
    previousProcessTicks_ = currentTicks;
    if (cpuAvailable) {
        previousTotal_ = total;
        previousIdle_ = idle;
    }

    std::sort(processes_.begin(), processes_.end(),
              [](const ProcessInfo &a, const ProcessInfo &b) {
        if (a.cpuPercent != b.cpuPercent) return a.cpuPercent > b.cpuPercent;
        return a.memoryMiB > b.memoryMiB;
    });
    processValue_->setText(QString::number(processes_.size()));
    renderProcesses();

    QFile memFile(QStringLiteral("/proc/meminfo"));
    quint64 memTotal = 0;
    quint64 memAvailable = 0;
    if (memFile.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = memFile.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("MemTotal:"))
                memTotal = line.mid(9).simplified().split(' ').first().toULongLong();
            else if (line.startsWith("MemAvailable:"))
                memAvailable = line.mid(13).simplified().split(' ').first().toULongLong();
        }
    }
    if (memTotal > 0 && memAvailable <= memTotal) {
        const quint64 used = memTotal - memAvailable;
        memoryValue_->setText(
            QStringLiteral("%1 / %2 MiB")
                .arg(QString::number(used / 1024.0, 'f', 0),
                     QString::number(memTotal / 1024.0, 'f', 0)));
        memoryBar_->setValue(qRound(100.0 * used / memTotal));
    } else {
        memoryValue_->setText(QStringLiteral("不可用"));
        memoryBar_->setValue(0);
    }

    QStorageInfo storage(QStringLiteral("/"));
    if (storage.isValid() && storage.isReady() && storage.bytesTotal() > 0) {
        const qint64 totalBytes = storage.bytesTotal();
        const qint64 usedBytes = totalBytes - storage.bytesAvailable();
        const double percent = 100.0 * usedBytes / totalBytes;
        diskValue_->setText(QString::number(percent, 'f', 1) + QStringLiteral("%"));
        diskBar_->setValue(qRound(percent));
    } else {
        diskValue_->setText(QStringLiteral("不可用"));
        diskBar_->setValue(0);
    }

    QFile uptimeFile(QStringLiteral("/proc/uptime"));
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        const double seconds = uptimeFile.readLine().split(' ').first().toDouble();
        const int hours = int(seconds / 3600.0);
        const int minutes = int(seconds / 60.0) % 60;
        uptimeValue_->setText(QStringLiteral("运行 %1 小时 %2 分钟")
                              .arg(hours).arg(minutes));
    } else {
        uptimeValue_->setText(QStringLiteral("运行时间不可用"));
    }
}

void SystemPage::renderProcesses()
{
    if (!isVisible()) return;
    const QString query = search_->text().trimmed();
    table_->setRowCount(0);
    for (const ProcessInfo &process : processes_) {
        if (!query.isEmpty()
            && !process.name.contains(query, Qt::CaseInsensitive)
            && !QString::number(process.pid).contains(query)) continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        QTableWidgetItem *pidItem = new QTableWidgetItem(QString::number(process.pid));
        pidItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, pidItem);
        table_->setItem(row, 1, new QTableWidgetItem(process.name));
        QTableWidgetItem *cpuItem = new QTableWidgetItem(
            process.cpuPercent < 0 ? QStringLiteral("—")
                                   : QString::number(process.cpuPercent, 'f', 1));
        cpuItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 2, cpuItem);
        QTableWidgetItem *memoryItem = new QTableWidgetItem(memoryText(process.memoryMiB));
        memoryItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 3, memoryItem);
        table_->setItem(row, 4, new QTableWidgetItem(process.state));
    }
}

