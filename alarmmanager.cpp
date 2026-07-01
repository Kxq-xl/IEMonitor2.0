#include "alarmmanager.h"



// alarmmanager.cpp
// 这里做最基础的阈值报警：低于低限触发低报，高于高限触发高报。
// 当前版本没有做报警延时防抖，目的是让你更直接地看到报警流程。

AlarmManager::AlarmManager(QObject *parent)
    : QObject{parent}
{
    // 默认阈值：温度 0~60，压力 0~12。
    // 运行时可以在界面上直接修改。
    setLimit("温度1", 0.0, 60.0);
    setLimit("温度2", 0.0, 60.0);
    setLimit("温度3", 0.0, 60.0);
    setLimit("压力", 0.0, 12.0);

}

void AlarmManager::setLimit(const QString &channel, double low, double high)
{
    AlarmLimit limit = m_limits.value(channel);
    limit.low = low;
    limit.high = high;
    m_limits[channel] = limit;
}

void AlarmManager::setDelayMs(int delayMs)
{
    // 报警延时可以在界面上调整。
    // 设为 0 时，下一次检测到超限会立刻报警。
    m_delayMs = delayMs;
}


void AlarmManager::check(const SensorData &data)
{
    if (!data.valid) {
        return;
    }

    // 一包采样数据里有多个通道，逐个检查即可。
    checkOne("温度1", data.temp1);
    checkOne("温度2", data.temp2);
    checkOne("温度3", data.temp3);
    checkOne("压力", data.pressure);
}

void AlarmManager::checkOne(const QString &channel, double value)
{
    AlarmLimit limit = m_limits.value(channel);
    QString alarmType;
    double threshold = 0.0;

    // 判断是否超过阈值
    if (value > limit.high) {
        alarmType = "高报";
        threshold = limit.high;
    } else if (value < limit.low) {
        alarmType = "低报";
        threshold = limit.low;
    }

    if (!alarmType.isEmpty()) {

        //已经报警，忽略即可
        if (limit.active) {
            m_limits[channel] = limit;
            return;
        }

        // 第一次发现超限时，只记录“开始超限”的时间，不立刻报警。
        // 如果下一次又恢复正常，pending 会被清掉，这就是防抖。
        if (!limit.pending || limit.pendingType != alarmType) {
            limit.pending = true;
            limit.pendingType = alarmType;
            limit.pendingValue = value;
            limit.pendingThreshold = threshold;
            limit.exceedStartTime = QDateTime::currentDateTime();   //记录第一次超过阈值的时间
        } else {
            limit.pendingValue = value;
            // 持续超限时间达到设置值后，才真正触发报警信号。
            if (limit.exceedStartTime.msecsTo(QDateTime::currentDateTime()) >= m_delayMs) { //超过 m_delayMs 后真正报警
                limit.active = true;
                limit.pending = false;
                emit alarmTriggered(channel, alarmType, value, threshold);
            }
        }
    } else if (limit.active) {
        // 已经报警的通道恢复到正常范围，触发恢复信号。
        limit.active = false;
        limit.pending = false;
        emit alarmRecovered(channel);
    } else {
        // 只超限了，不满足时间限制。取消这次等待报警。
        limit.pending = false;
    }

    m_limits[channel] = limit;
}
