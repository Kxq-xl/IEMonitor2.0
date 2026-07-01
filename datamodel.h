#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QDateTime>

// datamodel.h
// 项目的公共数据模型文件。
// 上位机采集到的数据最终都会整理成 SensorData，再交给界面、曲线、报警和数据库模块使用。

// 一次采样得到的设备运行数据。
// 在真实工业项目里，这些值通常来自 PLC、仪表或嵌入式控制器的 Modbus 寄存器。
struct SensorData
{
    QDateTime timestamp;
    double temp1 = 0.0;
    double temp2 = 0.0;
    double temp3 = 0.0;
    double pressure = 0.0;

    // 数据有效性标记。
    // DataProcessor 做量程判断后，如果发现数据明显异常，会把 valid 置为 false。
    // UI 收到无效数据后不画曲线、不参与报警，避免错误数据污染显示和记录。
    bool valid = true;
    QString invalidReason;
};




#endif // DATAMODEL_H
