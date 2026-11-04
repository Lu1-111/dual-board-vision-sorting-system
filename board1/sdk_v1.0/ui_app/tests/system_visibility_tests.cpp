#include "../systempage.h"
#include <QApplication>
#include <QTableWidget>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QThread>
#include <QProcess>
#include <QAbstractItemModel>
#include <cstdio>
static void pump(QApplication &app,int ms){QElapsedTimer clock;clock.start();while(clock.elapsed()<ms){app.processEvents();QThread::msleep(5);}}
static bool hasPid(QTableWidget *table,qint64 pid){for(int row=0;row<table->rowCount();++row)if(table->item(row,0)&&table->item(row,0)->text()==QString::number(pid))return true;return false;}
int main(int argc,char **argv){
 QApplication app(argc,argv);SystemPage page;page.resize(1200,800);
 auto *table=page.findChild<QTableWidget*>();auto *search=page.findChild<QLineEdit*>();if(!table||!search)return 1;
 int mutations=0;auto *model=table->model();
 QObject::connect(model,&QAbstractItemModel::rowsInserted,[&](){++mutations;});
 QObject::connect(model,&QAbstractItemModel::rowsRemoved,[&](){++mutations;});
 QObject::connect(model,&QAbstractItemModel::modelReset,[&](){++mutations;});
 QObject::connect(model,&QAbstractItemModel::dataChanged,[&](){++mutations;});
 pump(app,4300);if(page.sampleCount()!=0||table->rowCount()!=0||mutations)return 2;
 page.show();pump(app,100);if(page.sampleCount()==0||table->rowCount()==0||!hasPid(table,QCoreApplication::applicationPid()))return 3;
 const auto visibleSamples=page.sampleCount();pump(app,2200);if(page.sampleCount()<=visibleSamples)return 4;
 page.hide();pump(app,20);const auto hiddenSamples=page.sampleCount();const int beforeMutations=mutations;
 QProcess child;child.start("/bin/sleep",{"20"});if(!child.waitForStarted(1000))return 5;
 const qint64 childPid=child.processId();
 search->setText("hidden search should not rebuild");search->clear();pump(app,4300);
 if(page.sampleCount()!=hiddenSamples||mutations!=beforeMutations||hasPid(table,childPid)){child.kill();child.waitForFinished();return 6;}
 page.show();pump(app,100);
 const bool resumed=page.sampleCount()>hiddenSamples&&mutations>beforeMutations&&hasPid(table,childPid);
 child.terminate();if(!child.waitForFinished(1000)){child.kill();child.waitForFinished();}
 if(!resumed)return 7;
 std::printf("SYSTEM_VISIBILITY_PASS: hidden 2+2 timer periods no sampling/table mutation; visible 2s samples; show captures newly started PID; no control/GPIO used\n");return 0;
}
