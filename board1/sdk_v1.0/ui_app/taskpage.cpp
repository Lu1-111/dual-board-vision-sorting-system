#include "taskpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextEdit>
#include <QUuid>
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

QFrame *makeCard()
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    return card;
}

QFrame *makeStatCard(const QString &title, QLabel **value)
{
    QFrame *card = makeCard();
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(7);
    layout->addWidget(makeLabel(title, "muted"));
    *value = makeLabel(QStringLiteral("0"), "statValue");
    layout->addWidget(*value);
    return card;
}

QString priorityText(int priority)
{
    if (priority == 2) return QStringLiteral("高");
    if (priority == 0) return QStringLiteral("低");
    return QStringLiteral("普通");
}

class TaskDialog : public QDialog
{
public:
    TaskDialog(QWidget *parent, const TaskItem *task)
        : QDialog(parent), title_(new QLineEdit), description_(new QTextEdit),
          priority_(new QComboBox), hasDue_(new QCheckBox(QStringLiteral("设置截止日期"))),
          due_(new QDateEdit)
    {
        setWindowTitle(task ? QStringLiteral("编辑任务") : QStringLiteral("新增任务"));
        setMinimumWidth(520);
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 24, 24, 20);
        layout->setSpacing(16);

        QLabel *heading = makeLabel(windowTitle(), "sectionTitle");
        layout->addWidget(heading);
        QFormLayout *form = new QFormLayout;
        form->setSpacing(14);
        title_->setMaxLength(120);
        title_->setPlaceholderText(QStringLiteral("输入任务名称"));
        description_->setPlaceholderText(QStringLiteral("可选：记录任务说明"));
        description_->setMaximumHeight(140);
        priority_->addItem(QStringLiteral("低"), 0);
        priority_->addItem(QStringLiteral("普通"), 1);
        priority_->addItem(QStringLiteral("高"), 2);
        due_->setCalendarPopup(true);
        due_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        due_->setDate(QDate::currentDate());
        due_->setEnabled(false);
        connect(hasDue_, &QCheckBox::toggled, due_, &QWidget::setEnabled);

        form->addRow(QStringLiteral("名称"), title_);
        form->addRow(QStringLiteral("说明"), description_);
        form->addRow(QStringLiteral("优先级"), priority_);
        form->addRow(QString(), hasDue_);
        form->addRow(QStringLiteral("截止日期"), due_);
        layout->addLayout(form);

        if (task) {
            title_->setText(task->title);
            description_->setPlainText(task->description);
            priority_->setCurrentIndex(priority_->findData(task->priority));
            hasDue_->setChecked(task->dueDate.isValid());
            if (task->dueDate.isValid()) due_->setDate(task->dueDate);
        }

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (title_->text().trimmed().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("任务名称"),
                    QStringLiteral("请填写任务名称。"));
                title_->setFocus();
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        title_->setFocus();
    }

    void applyTo(TaskItem *task) const
    {
        task->title = title_->text().trimmed();
        task->description = description_->toPlainText().trimmed();
        task->priority = priority_->currentData().toInt();
        task->dueDate = hasDue_->isChecked() ? due_->date() : QDate();
    }

private:
    QLineEdit *title_;
    QTextEdit *description_;
    QComboBox *priority_;
    QCheckBox *hasDue_;
    QDateEdit *due_;
};

} // namespace

TaskPage::TaskPage(QWidget *parent)
    : QWidget(parent), table_(new QTableWidget), search_(new QLineEdit),
      filter_(new QComboBox), editButton_(new QPushButton(QStringLiteral("编辑"))),
      toggleButton_(new QPushButton(QStringLiteral("完成 / 恢复"))),
      deleteButton_(new QPushButton(QStringLiteral("删除"))),
      totalValue_(nullptr), pendingValue_(nullptr), doneValue_(nullptr),
      dueValue_(nullptr), detailTitle_(nullptr), detailMeta_(nullptr),
      detailBody_(nullptr)
{
    setObjectName(QStringLiteral("taskPage"));
    dataFile_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/tasks.json");
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(17);
    layout->addWidget(makeLabel(QStringLiteral("待办任务"), "pageTitle"));
    layout->addWidget(makeLabel(QStringLiteral("管理工作事项；修改会自动保存到本机。"), "muted"));

    QHBoxLayout *stats = new QHBoxLayout;
    stats->setSpacing(14);
    stats->addWidget(makeStatCard(QStringLiteral("全部任务"), &totalValue_));
    stats->addWidget(makeStatCard(QStringLiteral("待完成"), &pendingValue_));
    stats->addWidget(makeStatCard(QStringLiteral("已完成"), &doneValue_));
    stats->addWidget(makeStatCard(QStringLiteral("已到期未完成"), &dueValue_));
    layout->addLayout(stats);

    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    search_->setPlaceholderText(QStringLiteral("搜索任务名称或说明"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(260);
    toolbar->addWidget(search_, 1);
    filter_->addItems({QStringLiteral("全部"), QStringLiteral("待完成"),
                       QStringLiteral("已完成")});
    toolbar->addWidget(filter_);
    QPushButton *addButton = new QPushButton(QStringLiteral("＋ 新增任务"));
    addButton->setObjectName(QStringLiteral("primaryButton"));
    toolbar->addWidget(addButton);
    for (QPushButton *button : {editButton_, toggleButton_}) {
        button->setObjectName(QStringLiteral("secondaryButton"));
        toolbar->addWidget(button);
    }
    deleteButton_->setObjectName(QStringLiteral("dangerButton"));
    toolbar->addWidget(deleteButton_);
    layout->addLayout(toolbar);

    QHBoxLayout *body = new QHBoxLayout;
    body->setSpacing(16);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("任务"),
        QStringLiteral("优先级"), QStringLiteral("截止日期"), QStringLiteral("状态")});
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(50);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    body->addWidget(table_, 3);

    QFrame *detailCard = makeCard();
    QVBoxLayout *details = new QVBoxLayout(detailCard);
    details->setContentsMargins(22, 22, 22, 22);
    details->setSpacing(13);
    details->addWidget(makeLabel(QStringLiteral("任务详情"), "sectionTitle"));
    detailTitle_ = makeLabel(QStringLiteral("请选择任务"), "sectionTitle");
    detailMeta_ = makeLabel(QString(), "muted");
    detailBody_ = makeLabel(QStringLiteral("选择左侧任务查看说明。"), "detailText");
    detailBody_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    details->addWidget(detailTitle_);
    details->addWidget(detailMeta_);
    details->addWidget(detailBody_, 1);
    body->addWidget(detailCard, 1);
    layout->addLayout(body, 1);

    QLabel *storage = makeLabel(
        QStringLiteral("数据文件：%1").arg(dataFile_), "muted");
    storage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(storage);

    connect(addButton, &QPushButton::clicked, this, [this]() { addTask(); });
    connect(editButton_, &QPushButton::clicked, this, [this]() { editTask(); });
    connect(toggleButton_, &QPushButton::clicked, this, [this]() { toggleTask(); });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() { removeTask(); });
    connect(search_, &QLineEdit::textChanged, this,
            [this]() { refreshTable(); });
    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshTable(); });
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, [this]() { updateDetails(); });
    connect(table_, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *) { editTask(); });

    loadTasks();
    refreshTable();
}

void TaskPage::loadTasks()
{
    QFile file(dataFile_);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) {
        storageSafe_ = false;
        QMessageBox::warning(this, QStringLiteral("读取任务失败"), file.errorString());
        return;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().value(QStringLiteral("tasks")).isArray()) {
        const QString backup = dataFile_ + QStringLiteral(".invalid-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
        if (QFile::copy(dataFile_, backup)) {
            QMessageBox::warning(this, QStringLiteral("任务文件格式错误"),
                QStringLiteral("原文件已备份为：\n%1\n应用将从空列表开始。").arg(backup));
        } else {
            storageSafe_ = false;
            QMessageBox::warning(this, QStringLiteral("任务文件格式错误"),
                QStringLiteral("无法备份原文件，请先手动备份：\n%1").arg(dataFile_));
        }
        return;
    }
    const QJsonArray array = doc.object().value(QStringLiteral("tasks")).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) continue;
        const QJsonObject obj = value.toObject();
        TaskItem task;
        task.id = obj.value(QStringLiteral("id")).toString();
        task.title = obj.value(QStringLiteral("title")).toString().trimmed();
        if (task.id.isEmpty() || task.title.isEmpty()) continue;
        task.description = obj.value(QStringLiteral("description")).toString();
        task.priority = qBound(0, obj.value(QStringLiteral("priority")).toInt(1), 2);
        task.dueDate = QDate::fromString(
            obj.value(QStringLiteral("dueDate")).toString(), Qt::ISODate);
        task.completed = obj.value(QStringLiteral("completed")).toBool();
        task.createdAt = QDateTime::fromString(
            obj.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        tasks_.append(task);
    }
}

bool TaskPage::saveTasks()
{
    if (!storageSafe_) {
        QMessageBox::warning(this, QStringLiteral("保存任务失败"),
            QStringLiteral("原任务文件未能安全读取或备份，应用不会覆盖它。"));
        return false;
    }
    const QString folder = QFileInfo(dataFile_).absolutePath();
    if (!QDir().mkpath(folder)) {
        QMessageBox::warning(this, QStringLiteral("保存任务失败"),
            QStringLiteral("无法创建数据目录：%1").arg(folder));
        return false;
    }
    QJsonArray array;
    for (const TaskItem &task : tasks_) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), task.id);
        obj.insert(QStringLiteral("title"), task.title);
        obj.insert(QStringLiteral("description"), task.description);
        obj.insert(QStringLiteral("priority"), task.priority);
        obj.insert(QStringLiteral("dueDate"),
                   task.dueDate.isValid() ? task.dueDate.toString(Qt::ISODate) : QString());
        obj.insert(QStringLiteral("completed"), task.completed);
        obj.insert(QStringLiteral("createdAt"), task.createdAt.toString(Qt::ISODate));
        array.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("tasks"), array);
    QSaveFile file(dataFile_);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("保存任务失败"), file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("保存任务失败"), file.errorString());
        return false;
    }
    return true;
}

QString TaskPage::selectedTaskId() const
{
    if (table_->selectedItems().isEmpty()) return QString();
    const int row = table_->currentRow();
    QTableWidgetItem *item = row >= 0 ? table_->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

TaskItem *TaskPage::findTask(const QString &id)
{
    for (TaskItem &task : tasks_) {
        if (task.id == id) return &task;
    }
    return nullptr;
}

void TaskPage::refreshTable(const QString &preferredId)
{
    const QString selected = preferredId.isEmpty() ? selectedTaskId() : preferredId;
    const QString query = search_->text().trimmed();
    const int filter = filter_->currentIndex();
    QVector<int> indexes;
    int pending = 0;
    int overdue = 0;
    for (int i = 0; i < tasks_.size(); ++i) {
        const TaskItem &task = tasks_.at(i);
        if (!task.completed) {
            ++pending;
            if (task.dueDate.isValid() && task.dueDate <= QDate::currentDate())
                ++overdue;
        }
        if (filter == 1 && task.completed) continue;
        if (filter == 2 && !task.completed) continue;
        if (!query.isEmpty() && !task.title.contains(query, Qt::CaseInsensitive)
            && !task.description.contains(query, Qt::CaseInsensitive)) continue;
        indexes.append(i);
    }
    std::sort(indexes.begin(), indexes.end(), [this](int a, int b) {
        const TaskItem &left = tasks_.at(a);
        const TaskItem &right = tasks_.at(b);
        if (left.completed != right.completed) return !left.completed;
        if (left.dueDate.isValid() != right.dueDate.isValid())
            return left.dueDate.isValid();
        if (left.dueDate.isValid() && left.dueDate != right.dueDate)
            return left.dueDate < right.dueDate;
        return left.createdAt > right.createdAt;
    });

    QSignalBlocker blocker(table_);
    table_->setRowCount(0);
    int selectRow = -1;
    for (int index : indexes) {
        const TaskItem &task = tasks_.at(index);
        const int row = table_->rowCount();
        table_->insertRow(row);
        QTableWidgetItem *title = new QTableWidgetItem(task.title);
        title->setData(Qt::UserRole, task.id);
        table_->setItem(row, 0, title);
        table_->setItem(row, 1, new QTableWidgetItem(priorityText(task.priority)));
        table_->setItem(row, 2, new QTableWidgetItem(
            task.dueDate.isValid() ? task.dueDate.toString(Qt::ISODate)
                                   : QStringLiteral("—")));
        table_->setItem(row, 3, new QTableWidgetItem(
            task.completed ? QStringLiteral("已完成") : QStringLiteral("待完成")));
        if (task.id == selected) selectRow = row;
    }
    if (selectRow >= 0) table_->selectRow(selectRow);
    else table_->clearSelection();

    totalValue_->setText(QString::number(tasks_.size()));
    pendingValue_->setText(QString::number(pending));
    doneValue_->setText(QString::number(tasks_.size() - pending));
    dueValue_->setText(QString::number(overdue));
    updateDetails();
}

void TaskPage::updateDetails()
{
    TaskItem *task = findTask(selectedTaskId());
    const bool selected = task != nullptr;
    editButton_->setEnabled(selected);
    toggleButton_->setEnabled(selected);
    deleteButton_->setEnabled(selected);
    if (!task) {
        detailTitle_->setText(QStringLiteral("请选择任务"));
        detailMeta_->clear();
        detailBody_->setText(QStringLiteral("选择左侧任务查看说明。"));
        return;
    }
    detailTitle_->setText(task->title);
    QStringList meta;
    meta << (task->completed ? QStringLiteral("已完成") : QStringLiteral("待完成"));
    meta << QStringLiteral("优先级：%1").arg(priorityText(task->priority));
    if (task->dueDate.isValid())
        meta << QStringLiteral("截止：%1").arg(task->dueDate.toString(Qt::ISODate));
    detailMeta_->setText(meta.join(QStringLiteral("  ·  ")));
    detailBody_->setText(task->description.isEmpty()
        ? QStringLiteral("此任务没有填写说明。") : task->description);
    toggleButton_->setText(task->completed ? QStringLiteral("恢复待办")
                                           : QStringLiteral("标记完成"));
}

void TaskPage::addTask()
{
    TaskDialog dialog(this, nullptr);
    if (dialog.exec() != QDialog::Accepted) return;
    TaskItem task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.createdAt = QDateTime::currentDateTime();
    dialog.applyTo(&task);
    tasks_.append(task);
    saveTasks();
    filter_->setCurrentIndex(0);
    search_->clear();
    refreshTable(task.id);
}

void TaskPage::editTask()
{
    TaskItem *task = findTask(selectedTaskId());
    if (!task) return;
    const QString id = task->id;
    TaskDialog dialog(this, task);
    if (dialog.exec() != QDialog::Accepted) return;
    dialog.applyTo(task);
    saveTasks();
    refreshTable(id);
}

void TaskPage::toggleTask()
{
    TaskItem *task = findTask(selectedTaskId());
    if (!task) return;
    const QString id = task->id;
    task->completed = !task->completed;
    saveTasks();
    refreshTable(id);
}

void TaskPage::removeTask()
{
    const QString id = selectedTaskId();
    TaskItem *task = findTask(id);
    if (!task) return;
    if (QMessageBox::question(this, QStringLiteral("删除任务"),
        QStringLiteral("确定删除“%1”？此操作无法撤销。").arg(task->title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (int i = 0; i < tasks_.size(); ++i) {
        if (tasks_.at(i).id == id) {
            tasks_.remove(i);
            break;
        }
    }
    saveTasks();
    refreshTable();
}

