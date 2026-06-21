#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QChart>
#include <QLineSeries>
#include <QMainWindow>
#include <QValueAxis>
#include "modbuscomm.h"
#include "datamodel.h"
#include "databasemanager.h"
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 初始化整个主界面 Tab 容器。
    void initUI();
    // 初始化折线图
    void initChart();

    // 初始化“实时监控”页面：连接参数、实时数据、阈值、曲线。
    void initConnectionPage();

    // 初始化历史记录的页面
    void initHistoryPage();

    // 把新采样点追加到实时趋势曲线。
    void appendChartPoint(const SensorData &data);

private slots:
    //连接Modbus TCP按钮
    void on_conTcp_clicked();
    //断开连接按钮
    void on_disCon_clicked();

    void on_pushButton_clicked();

    void on_queryButton_clicked();

private:
    Ui::MainWindow *ui;
    //modbus 通信
    ModbusComm m_comm;
    //曲线横坐标计数器
    int m_sampleIndex = 0;
    //数据库
    DatabaseManager m_db;
    //数据库保存定时器
    QTimer *m_saveTimer = nullptr;
    //最近一次采集到的数据
    SensorData m_lastData;

    QChart *m_chart = nullptr;
    QLineSeries *m_temp1Series = nullptr;
    QLineSeries *m_temp2Series = nullptr;
    QLineSeries *m_temp3Series = nullptr;
    QLineSeries *m_pressureSeries = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

};
#endif // MAINWINDOW_H
