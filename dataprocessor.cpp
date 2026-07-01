#include "dataprocessor.h"

DataProcessor::DataProcessor() {}

SensorData DataProcessor::fromRegisters(const QVector<quint16> &values) const
{
    SensorData data;
    data.timestamp = QDateTime::currentDateTime();

    // Modbus 读保持寄存器时，通信层只保证“读到了几个原始整数”。
    // 如果数量不够，说明设备回复不符合当前项目约定，直接标记无效。
    if (values.size() < 4) {
        data.valid = false;
        data.invalidReason = "寄存器数量不足";
        return data;
    }

    // 这里是本项目的寄存器映射规则：
    // 0~2 是三路温度，3 是压力；原始值除以 10 得到真实工程量。
    data.temp1 = values.at(0) / 10.0;
    data.temp2 = values.at(1) / 10.0;
    data.temp3 = values.at(2) / 10.0;
    data.pressure = values.at(3) / 10.0;
    validate(&data);
    return data;
}

bool DataProcessor::validate(SensorData *data) const
{
    if (!data) {
        return false;
    }

    // 这里只做演示项目需要的简单量程判断。
    // 真正项目里通常会把量程、系数、偏移量做成配置项。
    const bool tempOk = data->temp1 >= -50 && data->temp1 <= 300
                        && data->temp2 >= -50 && data->temp2 <= 300
                        && data->temp3 >= -50 && data->temp3 <= 300;
    const bool pressureOk = data->pressure >= 0 && data->pressure <= 100;

    data->valid = tempOk && pressureOk;
    if (!data->valid) {
        data->invalidReason = "数据超出量程";
    }
    return data->valid;
}
