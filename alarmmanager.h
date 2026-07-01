#ifndef ALARMMANAGER_H
#define ALARMMANAGER_H

#include <QObject>
#include "datamodel.h"

// 单个通道的报警配置。
// active 用来记录当前是否已经报警，
// pending 表示正在等待防抖时间确认。
struct AlarmLimit
{
    // 低报阈值。
    double low = 0.0;

    // 高报阈值。
    double high = 100.0;

    // 是否已经正式报警。
    bool active = false;

    // 是否已经发现超限，但还在等待防抖延时。
    bool pending = false;

    // 等待确认中的报警类型：高报/低报。
    QString pendingType;

    // 等待确认中的最新超限值。
    double pendingValue = 0.0;

    // 等待确认中的报警阈值。
    double pendingThreshold = 0.0;

    // 本轮持续超限开始时间。
    QDateTime exceedStartTime;
};

// alarmmanager.h / alarmmanager.cpp
// 报警模块：负责判断实时数据是否超过阈值。
// 它不直接操作界面，也不直接写数据库，只通过信号告诉 MainWindow 发生了什么。
class AlarmManager : public QObject
{
    Q_OBJECT

public:
    explicit AlarmManager(QObject *parent = nullptr);

    // 设置某个通道的低限和高限，例如 温度1：0~60。
    void setLimit(const QString &channel, double low, double high);

    // 设置报警防抖延时，单位 ms。
    void setDelayMs(int delayMs);

    // 检查一整包采样数据，内部会分别检查温度1、温度2、温度3、压力。
    void check(const SensorData &data);

signals:
    // 某个通道从正常进入报警状态时触发。
    void alarmTriggered(const QString &channel, const QString &type, double value, double threshold);

    // 某个通道从报警恢复到正常范围时触发。
    void alarmRecovered(const QString &channel);

private:
    // 检查单个通道是否高报、低报或恢复。
    void checkOne(const QString &channel, double value);

    // 每个通道独立保存一份报警阈值和报警状态。
    QMap<QString, AlarmLimit> m_limits;

    // 报警防抖延时，持续超限超过该时间才正式报警。
    int m_delayMs = 1000;
};


#endif // ALARMMANAGER_H
