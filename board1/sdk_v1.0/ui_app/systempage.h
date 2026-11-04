#ifndef SYSTEMPAGE_H
#define SYSTEMPAGE_H

#include <QHash>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QTableWidget;
class QTimer;
class QShowEvent;

struct ProcessInfo {
    qint64 pid = 0;
    QString name;
    QString state;
    quint64 ticks = 0;
    double cpuPercent = -1.0;
    double memoryMiB = 0.0;
};

class SystemPage : public QWidget
{
public:
    explicit SystemPage(QWidget *parent = nullptr);
    // Read-only diagnostic count of real process samples (never cached-as-fresh).
    quint64 sampleCount() const { return sampleCount_; }
protected:
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void renderProcesses();
    bool readCpu(quint64 *total, quint64 *idle, int *cores) const;
    QVector<ProcessInfo> readProcesses() const;

    QLabel *cpuValue_;
    QLabel *memoryValue_;
    QLabel *processValue_;
    QLabel *diskValue_;
    QLabel *uptimeValue_;
    QProgressBar *cpuBar_;
    QProgressBar *memoryBar_;
    QProgressBar *diskBar_;
    QLineEdit *search_;
    QTableWidget *table_;
    QTimer *timer_;
    QVector<ProcessInfo> processes_;
    QHash<qint64, quint64> previousProcessTicks_;
    quint64 sampleCount_ = 0;
    quint64 previousTotal_ = 0;
    quint64 previousIdle_ = 0;
};

#endif

