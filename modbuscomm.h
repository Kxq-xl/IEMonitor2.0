#ifndef MODBUSCOMM_H
#define MODBUSCOMM_H

#include <QObject>
#include <QModbusClient>
#include <QModbusTcpClient>
#include <QTimer>
#include <QDebug>
#include <QVariant>
#include <QSerialPort>
#include <QModbusRtuSerialClient>


#include "datamodel.h"

// modbus 协议， TCP， RTU，自动模拟
enum class CommMode
{
    None,
    Tcp,
    Rtu,
    Simulation
};



class ModbusComm : public QObject
{
    Q_OBJECT
public:
    explicit ModbusComm(QObject *parent = nullptr);
    ~ModbusComm();

    /*
     *
     * 这俩 connect 最后一个参数 目前采用 intervalMs ，暂时不实现断线重连
     * */

    // 连接 Modbus TCP 从站，并按 intervalMs 周期读取寄存器。
    void connectTcp(const QString &ip, int port, int slaveId, int intervalMs);

    // 连接 Modbus RTU 串口从站
    void connectRtu(const QString &portName,
                    int baudRate,
                    QSerialPort::DataBits dataBits,
                    QSerialPort::StopBits stopBits,
                    QSerialPort::Parity parity,
                    int slaveId,
                    int intervalMs);


    // 断开真实设备或停止内置模拟。
    void disconnectDevice();


    // 给 UI 和数据库定时保存逻辑判断当前是否处于采集状态。
    bool isConnected() const;

private:
    //定时读取 Modbus 保持寄存器。
    void poll();

signals:
    // 连接状态文字变化，用于状态栏和状态标签显示。
    void statusChanged(const QString &status);
    // 设备在线/离线状态变化。
    void onlineChanged(bool online);
    // 每次采集成功后，把已经换算好的工程量抛给主界面。
    void dataReady(const SensorData &data);

private:

    // 统一设置 TCP/RTU 客户端的状态信号、错误信号和超时参数。
    void setupClient(QModbusClient *client);
    // 把 Modbus 原始寄存器值转换成温度、压力等工程量。
    SensorData parseRegisters(const QVector<quint16> &values) const;

    // 当前 TCP 或 RTU 客户端，基类指针统一管理。
    QModbusClient *m_client = nullptr;


    //Modbus TCP 客户端对象
    CommMode m_mode = CommMode::None;
    QString m_tcpIp = "127.0.0.1";
    int m_tcpPort = 502;

    // RTU 重连时需要复用的串口参数。
    QString m_rtuPortName;
    int m_rtuBaudRate = 9600;
    QSerialPort::DataBits m_rtuDataBits = QSerialPort::Data8;
    QSerialPort::StopBits m_rtuStopBits = QSerialPort::OneStop;
    QSerialPort::Parity m_rtuParity = QSerialPort::NoParity;


    int m_intervalMs;
    //采集定时器
    QTimer m_pollTimer;
    //Modbus 从站地址
    int m_slaveId = 1;
};

#endif // MODBUSCOMM_H
