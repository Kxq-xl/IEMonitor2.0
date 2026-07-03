#include "modbuscomm.h"



ModbusComm::ModbusComm(QObject *parent)
    : QObject{parent}
{
    // 采集周期
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusComm::poll);
    // 断线重连
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ModbusComm::reconnect);
}

ModbusComm::~ModbusComm()
{
    disconnectDevice();
}

void ModbusComm::connectTcp(const QString &ip, int port, int slaveId, const RuntimeConfig &config)
{
    qDebug() << "ModbusComm::connectTcp 线程 : " << QThread::currentThread();
    // reconnecting 用来区分“用户主动连接”和“自动重连”。
    // 主动连接要清零重连次数；自动重连不能清零，否则最大重连次数会失效。
    const bool reconnecting = m_reconnectTimer->isActive();

    // 重新连接前先断开旧连接，避免多个客户端同时读同一个设备。
    disconnectDevice();

    m_mode = CommMode::Tcp;
    m_tcpIp = ip;
    m_tcpPort = port;
    m_slaveId = slaveId;
    m_config = config;    //配置文件不实现
    if (!reconnecting) {
        m_reconnectCount = 0;
    }


    // Modbus TCP 最关键的三个参数：IP、端口、从站地址。
    // IP/端口定位设备，从站地址定位设备内部的 Modbus 从站。
    auto *client = new QModbusTcpClient(this);
    client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);
    client->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    setupClient(client);

    emit statusChanged("正在连接 Modbus TCP...");
    //连接设备
    if (!m_client->connectDevice()) {
        qDebug() << "m_client->connectDevice() error:  " << m_client->errorString();
    }
}

void ModbusComm::connectRtu(const QString &portName,
                            int baudRate, QSerialPort::DataBits dataBits,
                            QSerialPort::StopBits stopBits,
                            QSerialPort::Parity parity,
                            int slaveId,
                            const RuntimeConfig &config)
{

    qDebug() << "ModbusComm::connectRtu 线程 : " << QThread::currentThread();
    // RTU 和 TCP 的上层接口保持一致，区别只在底层连接参数：
    // TCP 用 IP/端口，RTU 用串口号、波特率、数据位、停止位、校验位。
    const bool reconnecting = m_reconnectTimer->isActive();
    disconnectDevice();
    m_mode = CommMode::Rtu;
    m_rtuPortName = portName;
    m_rtuBaudRate = baudRate;
    m_rtuDataBits = dataBits;
    m_rtuStopBits = stopBits;
    m_rtuParity = parity;
    m_slaveId = slaveId;
    m_config = config;
    if (!reconnecting){
        m_reconnectCount = 0;
    }

    //设置 RTU 连接参数
    auto client = new QModbusRtuSerialClient(this);
    client->setConnectionParameter(QModbusDevice::SerialPortNameParameter, portName);
    client->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, baudRate);
    client->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, dataBits);
    client->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, stopBits);
    client->setConnectionParameter(QModbusDevice::SerialParityParameter, parity);
    setupClient(client);


    emit statusChanged("正在连接 Modbus RTU...");
    //连接设备
    if (!m_client->connectDevice()) {
        qDebug() << "m_client->connectDevice() error:  " << m_client->errorString();
    }
}



void ModbusComm::disconnectDevice()
{
    // 已经断开，直接返回，不输出日志
    if (!m_client || m_client->state() != QModbusDevice::ConnectedState)
        return;

    qDebug() << "ModbusComm::disconnectDevice 线程 : " << QThread::currentThread();
    // 停止采集和重连定时器。
    m_pollTimer->stop();
    m_reconnectTimer->stop();

    // 恢复采集定时器默认连接到 poll()，避免从模拟模式切回真实模式时仍触发模拟数据。
    disconnect(m_pollTimer, nullptr, this, nullptr);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusComm::poll);

    // 清理旧 TCP/RTU 客户端。
    clearClient();
    m_mode = CommMode::None;
    m_timeoutCount = 0;
    emit onlineChanged(false);
    emit statusChanged("已断开");
}

void ModbusComm::writeRegister(int address, quint16 value)
{
    // 新增：限制仅允许 0/1/2/3 寄存器写入
    QList<int> allowAddr = {0,1,2,3};
    if (!allowAddr.contains(address))
    {
        emit writeFinished(false, QString("禁止写入地址%1，仅允许0、1、2、3寄存器下发").arg(address));
        return;
    }

    // 内置模拟没有真实寄存器，这里只返回成功提示，方便体验“参数下发”流程。
    if (m_mode == CommMode::Simulation) {
        emit writeFinished(true, QString("模拟写入成功：地址 %1 = %2").arg(address).arg(value));
        return;
    }

    if (!m_client || m_client->state() != QModbusDevice::ConnectedState) {
        emit writeFinished(false, "设备未连接，无法写寄存器");
        return;
    }

    // 写单个保持寄存器，对应 Modbus 功能码 0x06。
    // Qt SerialBus 会根据 DataUnit 和 sendWriteRequest 自动组帧。
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, 1);
    unit.setValue(0, value);
    QModbusReply *reply = m_client->sendWriteRequest(unit, m_slaveId);
    if (!reply) {
        emit writeFinished(false, m_client->errorString());
        return;
    }

    connect(reply, &QModbusReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const bool ok = reply->error() == QModbusDevice::NoError;
        emit writeFinished(ok, ok ? "参数下发成功" : reply->errorString());
    });
}

bool ModbusComm::isConnected() const
{
    return (m_client && m_client->state() == QModbusDevice::ConnectedState);
}

void ModbusComm::poll()
{
    if (!m_client || m_client->state() != QModbusDevice::ConnectedState) {
        return;
    }

    qDebug() << "ModbusComm::poll 线程 : " << QThread::currentThread();
    // qDebug() << "hello";

    // 上位机的核心动作：定时读取下位机的保持寄存器。
    // 这里读取 0~3 共 4 个寄存器，对应 3 路温度和 1 路压力。
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, 0, 4);
    QModbusReply *reply = m_client->sendReadRequest(request, m_slaveId);    //向下位机发送
    if (!reply) {
        //emit errorOccurred(m_client->errorString());
        qDebug() << "reply error";
        handleCommError(m_client->errorString());
        return;
    }


    //请求未完成
    if (!reply->isFinished())
    {
        //绑定回复完成的信号
        connect(reply, &QModbusReply::finished, this, [this, reply]() {
            //通过事件循环去延迟删除
            reply->deleteLater();
            //如果没有错误
            if (reply->error() != QModbusDevice::NoError) {
                handleCommError(reply->errorString());
                return;
            }
            m_timeoutCount = 0;
            //得到普通回复
            const QModbusDataUnit unit = reply->result();
            QVector<quint16> values;
            for (uint i = 0; i < unit.valueCount(); ++i) {
                values.append(unit.value(i));
            }


            emit onlineChanged(true);

            // 通信层不直接解释业务含义，只把原始寄存器交给数据处理模块。
            emit dataReady(m_processor.fromRegisters(values));
        });
    }
    else
    {
        qDebug() << "请求未完成!";
    }
}

void ModbusComm::reconnect()
{
    // 自动重连只对真实 TCP/RTU 有意义，内置模拟不需要重连。
    if (m_mode == CommMode::None || m_mode == CommMode::Simulation) {
        m_reconnectTimer->stop();
        return;
    }

    if (m_reconnectCount >= m_config.maxReconnectCount) {
        m_reconnectTimer->stop();
        emit statusChanged("重连次数已达到上限");
        return;
    }
    qDebug() << "尝试断线重连";
    qDebug() << "ModbusComm::reconnect 线程 : " << QThread::currentThread();

    ++m_reconnectCount;
    qDebug() << QString("正在自动重连，第 %1 次").arg(m_reconnectCount);
    emit statusChanged(QString("正在自动重连，第 %1 次").arg(m_reconnectCount));

    if (m_mode == CommMode::Tcp) {
        connectTcp(m_tcpIp, m_tcpPort, m_slaveId, m_config);
    } else if (m_mode == CommMode::Rtu) {
        connectRtu(m_rtuPortName, m_rtuBaudRate, m_rtuDataBits, m_rtuStopBits, m_rtuParity, m_slaveId, m_config);
    }
}

void ModbusComm::setupClient(QModbusClient *client)
{

    qDebug() << "ModbusComm::setupClient 线程 : " << QThread::currentThread();

    // 每次新建 TCP/RTU 客户端时，都统一在这里连接状态信号和错误信号。
    // 这样 TCP 和 RTU 的状态处理、自动重连逻辑可以复用。
    m_client = client;
    //超时时间
    m_client->setTimeout(1000);
    //每一次 Modbus 读写请求，如果失败，自动重试 3 次，总共最多尝试 4 次。
    m_client->setNumberOfRetries(3);

    //设备状态改变时的 信号和槽
    connect(m_client, &QModbusClient::stateChanged, this, [=](){
        if (m_client->state() == QModbusDevice::ConnectedState)
        {
            m_timeoutCount = 0;
            m_reconnectCount = 0;
            m_reconnectTimer->stop();
            //设备状态
            emit onlineChanged(true);
            //通信状态
            emit statusChanged(m_mode == CommMode::Tcp ? "Modbus TCP 已连接" : "Modbus RTU 已连接");
            startPolling();
        }
        else if (m_client->state() == QModbusDevice::UnconnectedState)
        {
            //停止采集数据
            m_pollTimer->stop();
            qDebug() << (m_mode == CommMode::Tcp ? "Modbus TCP 已断开" : "Modbus RTU 已断开");
            //设备状态
            emit onlineChanged(false);
            //通信状态
            emit statusChanged("设备已断开，等待自动重连");
            if (!m_reconnectTimer->isActive()) {
                m_reconnectTimer->start(m_config.reconnectIntervalMs);
            }
        }
    });

    //出错的信号和槽
    connect(m_client, &QModbusClient::errorOccurred, this, [=](){
        qDebug () << m_client->errorString();
    });
}

void ModbusComm::startPolling()
{
    disconnect(m_pollTimer, nullptr, this, nullptr);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusComm::poll);
    m_pollTimer->start(m_config.sampleIntervalMs);
}

void ModbusComm::handleCommError(const QString &message)
{
    // 连续错误次数用于判断设备是否离线。
    // 偶发一次失败不马上断线，连续 3 次失败才认为通信异常。
    ++m_timeoutCount;
    emit errorOccurred(message);
    if (m_timeoutCount >= 3) {
        emit onlineChanged(false);
        emit statusChanged("通信连续超时，设备离线");

        if (m_client) {
            m_client->disconnectDevice();
        }

        if (!m_reconnectTimer->isActive() && m_mode != CommMode::None) {
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
        }
    }
}

void ModbusComm::clearClient()
{
    if (!m_client) {
        return;
    }

    // 旧客户端销毁前先断开和 ModbusComm 的信号槽，避免旧状态信号影响新连接。
    disconnect(m_client, nullptr, this, nullptr);

    // 如果设备还处于连接状态，先主动断开协议连接。
    if (m_client->state() != QModbusDevice::UnconnectedState) {
        m_client->disconnectDevice();
    }

    // deleteLater 让对象在所属线程事件循环中安全释放。
    m_client->deleteLater();
    m_client = nullptr;
}
