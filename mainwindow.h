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
#include <QTableWidgetItem>
#include "alarmmanager.h"
#include <QThread>
#include <QSettings>

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

    // 初始化报警界面
    void initAlarmPage();

    // 创建通信线程和数据库线程，并把工作对象移动到对应线程。
    void initWorkers();

    // 从界面控件组装运行配置，传给通信线程使用。
    RuntimeConfig runtimeConfig() const;

    // 更新某个通道的实时数值标签；无效数据会显示为“无效”。
    void updateSensorLabel(const QString &name, double value, const QString &unit, bool valid);

    // 把新采样点追加到实时趋势曲线。
    void appendChartPoint(const SensorData &data);

    //将表格项制作成居中不可修改的属性
    QTableWidgetItem* makeCell(const QString& text);

    // 从数据库读取报警记录并显示到表格。
    void refreshAlarmTable();

    // 把界面上填写的阈值同步到 AlarmManager。
    void applyAlarmLimits();
    // 从 QSettings 读取上次保存的连接参数、阈值和运行参数。
    void loadSettings();

    // 把当前界面配置保存到 QSettings。
    void saveSettings();
private slots:
    //连接Modbus TCP按钮
    void on_conTcp_clicked();
    //断开连接按钮
    void on_disCon_clicked();

    void on_pushButton_clicked();

    void on_queryButton_clicked();

    void on_refreshButton_clicked();

    void on_pauseChartButton_clicked();

private:
    Ui::MainWindow *ui;
    // 通信工作对象，实际运行在 m_commThread 中。
    ModbusComm *m_comm = nullptr;
    // 通信线程：处理 Modbus 连接、采集、写寄存器、重连。
    QThread *m_commThread = nullptr;
    //曲线横坐标计数器 表示第几次采样。
    int m_sampleIndex = 0;
    // 曲线暂停标记；暂停时仍采集数据，但不追加曲线点。
    bool m_chartPaused = false;

    // 数据库工作对象，实际运行在 m_dbThread 中。
    DatabaseManager *m_db = nullptr;
    // 数据库线程：处理 SQLite 写入和查询。
    QThread *m_dbThread = nullptr;
    //数据库保存定时器
    QTimer *m_saveTimer = nullptr;
    //最近一次采集到的数据
    SensorData m_lastData;

    //报警模块
    AlarmManager m_alarm;

    // 当前设备是否在线；用于控制断开后不再重复保存最后一包数据。
    bool m_deviceOnline = false;

    //
    QMap<QString, QLabel *> m_valueLabels;

    QChart *m_chart = nullptr;
    QLineSeries *m_temp1Series = nullptr;
    QLineSeries *m_temp2Series = nullptr;
    QLineSeries *m_temp3Series = nullptr;
    QLineSeries *m_pressureSeries = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

};
#endif // MAINWINDOW_H
