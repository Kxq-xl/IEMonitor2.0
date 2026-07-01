#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H


#include "datamodel.h"

// dataprocessor.h / dataprocessor.cpp
// 数据处理模块：把 Modbus 原始寄存器值转换成上位机真正关心的工程量。
// 通信层只负责拿到寄存器，DataProcessor 负责“这些寄存器代表什么”。

class DataProcessor
{
public:
    DataProcessor();
    // Holding Register 0~3:
    // 0 温度1，1 温度2，2 温度3，3 压力；原始值 / 10 = 工程量。
    SensorData fromRegisters(const QVector<quint16> &values) const;

private:
    // 简单量程过滤：超出常见工业演示范围时标记为无效值。
    bool validate(SensorData *data) const;

};

#endif // DATAPROCESSOR_H
