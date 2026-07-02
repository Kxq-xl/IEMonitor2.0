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
#include "dataprocessor.h"

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
    void connectTcp(const QString &ip, int port, int slaveId, const RuntimeConfig &config);

    // 连接 Modbus RTU 串口从站
    void connectRtu(const QString &portName,
                    int baudRate,
                    QSerialPort::DataBits dataBits,
                    QSerialPort::StopBits stopBits,
                    QSerialPort::Parity parity,
                    int slaveId,
                    const RuntimeConfig &config);

    // 断开真实设备或停止内置模拟。
    void disconnectDevice();


    // 给 UI 和数据库定时保存逻辑判断当前是否处于采集状态。
    bool isConnected() const;

private slots:
    //定时读取 Modbus 保持寄存器。
    void poll();

    // 自动重连定时器触发后执行。
    void reconnect();

signals:
    // 连接状态文字变化，用于状态栏和状态标签显示。
    void statusChanged(const QString &status);
    // 设备在线/离线状态变化。
    void onlineChanged(bool online);
    // 每次采集成功后，把已经换算好的工程量抛给主界面。
    void dataReady(const SensorData &data);
    // 通信错误提示，例如连接失败、读写失败。
    void errorOccurred(const QString &message);

private:

    // 统一设置 TCP/RTU 客户端的状态信号、错误信号和超时参数。
    void setupClient(QModbusClient *client);

    // 连接成功后启动定时采集。
    void startPolling();

    // 处理通信错误，累计超时次数并按需启动重连。
    void handleCommError(const QString &message);

    // 清理旧 Modbus 客户端，避免重复连接和旧信号干扰。
    void clearClient();

    // 当前 TCP 或 RTU 客户端，基类指针统一管理。
    QModbusClient *m_client = nullptr;

    //当前的通信模式
    CommMode m_mode = CommMode::None;


    // TCP 重连时需要复用的 IP 和端口。
    QString m_tcpIp = "127.0.0.1";
    int m_tcpPort = 502;

    // RTU 重连时需要复用的串口参数。
    QString m_rtuPortName;
    int m_rtuBaudRate = 9600;
    QSerialPort::DataBits m_rtuDataBits = QSerialPort::Data8;
    QSerialPort::StopBits m_rtuStopBits = QSerialPort::OneStop;
    QSerialPort::Parity m_rtuParity = QSerialPort::NoParity;

    // RuntimeConfig 参数
    // 当前运行参数：采集周期、重连间隔、重连次数等。
    RuntimeConfig m_config;
    // 原始寄存器到工程量的转换器。
    DataProcessor m_processor;

    //采集定时器
    QTimer *m_pollTimer = nullptr;

    // 自动重连定时器。
    QTimer *m_reconnectTimer = nullptr;

    //Modbus 从站地址
    int m_slaveId = 1;

    // 连续通信失败次数，达到阈值后认为设备离线。
    int m_timeoutCount = 0;

    // 当前自动重连次数。
    int m_reconnectCount = 0;
};

#endif // MODBUSCOMM_H
