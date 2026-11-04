#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>
class QLineEdit;class QCheckBox;class QTableWidget;class QLabel;
class SortSettings:public QWidget {
public:
    SortSettings(std::function<QJsonObject()> status,std::function<void()> changed,QWidget *parent=nullptr);
    QJsonObject config() const;
    void setConfig(const QJsonObject &config);
private:
    QLineEdit *path_;
    QLineEdit *fields_[9];
    QCheckBox *confirmed_;
    QTableWidget *zones_;
    QLabel *observation_,*message_;
    QJsonObject reference_;
};
