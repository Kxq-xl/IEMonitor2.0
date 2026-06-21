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


void AlarmManager::check(const SensorData &data)
{
    // 一包采样数据里有多个通道，逐个检查即可。
    checkOne("温度1", data.temp1);
    checkOne("温度2", data.temp2);
    checkOne("温度3", data.temp3);
    checkOne("压力", data.pressure);
}

void AlarmManager::checkOne(const QString &channel, double value)
{
    AlarmLimit limit = m_limits.value(channel);

    if (value > limit.high) {
        if (!limit.active) {
            // 只有从正常状态进入报警状态时才发信号，避免重复刷屏和重复入库。
            limit.active = true;
            emit alarmTriggered(channel, "高报", value, limit.high);
        }
    } else if (value < limit.low) {
        if (!limit.active) {
            limit.active = true;
            emit alarmTriggered(channel, "低报", value, limit.low);
        }
    } else if (limit.active) {
        limit.active = false;
        emit alarmRecovered(channel);
    }

    m_limits[channel] = limit;
}
