#ifndef MODBUSCOMM_H
#define MODBUSCOMM_H

#include <QObject>
#include <QModbusClient>
#include <QModbusTcpClient>
#include <QTimer>
#include <QDebug>
#include <QVariant>

#include "datamodel.h"

class ModbusComm : public QObject
{
    Q_OBJECT
public:
    explicit ModbusComm(QObject *parent = nullptr);
    ~ModbusComm();

    // 连接 Modbus TCP 从站，并按 intervalMs 周期读取寄存器。
    void connectTcp(const QString &ip, int port, int slaveId, int intervalMs);

    // 断开真实设备或停止内置模拟。
    void disconnectDevice();


private:
    //定时读取 Modbus 保持寄存器。
    void poll();

signals:

    // 每次采集成功后，把已经换算好的工程量抛给主界面。
    void dataReady(const SensorData &data);

private:
    // 把 Modbus 原始寄存器值转换成温度、压力等工程量。
    SensorData parseRegisters(const QVector<quint16> &values) const;


    //真实 Modbus TCP 客户端对象
    QModbusClient *m_client = nullptr;
    //采集定时器
    QTimer m_pollTimer;
    //Modbus 从站地址
    int m_slaveId = 1;
};

#endif // MODBUSCOMM_H
