#ifndef TASKPAGE_H
#define TASKPAGE_H

#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

struct TaskItem {
    QString id;
    QString title;
    QString description;
    int priority = 1; // 0 low, 1 normal, 2 high
    QDate dueDate;
    bool completed = false;
    QDateTime createdAt;
};

class TaskPage : public QWidget
{
public:
    explicit TaskPage(QWidget *parent = nullptr);

private:
    void loadTasks();
    bool saveTasks();
    void refreshTable(const QString &preferredId = QString());
    void updateDetails();
    QString selectedTaskId() const;
    TaskItem *findTask(const QString &id);
    void addTask();
    void editTask();
    void toggleTask();
    void removeTask();

    QVector<TaskItem> tasks_;
    QString dataFile_;
    bool storageSafe_ = true;
    QTableWidget *table_;
    QLineEdit *search_;
    QComboBox *filter_;
    QPushButton *editButton_;
    QPushButton *toggleButton_;
    QPushButton *deleteButton_;
    QLabel *totalValue_;
    QLabel *pendingValue_;
    QLabel *doneValue_;
    QLabel *dueValue_;
    QLabel *detailTitle_;
    QLabel *detailMeta_;
    QLabel *detailBody_;
};

#endif

