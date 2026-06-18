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
};




#endif // DATAMODEL_H
