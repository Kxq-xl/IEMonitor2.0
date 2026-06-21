#include "modbuscomm.h"


ModbusComm::ModbusComm(QObject *parent)
    : QObject{parent}
{
    connect(&m_pollTimer, &QTimer::timeout, this, &ModbusComm::poll);
}

ModbusComm::~ModbusComm()
{
    disconnectDevice();
}

void ModbusComm::connectTcp(const QString &ip, int port, int slaveId, int intervalMs)
{
    // 重新连接前先断开旧连接，避免多个客户端同时读同一个设备。
    disconnectDevice();
    m_slaveId = slaveId;
    // Modbus TCP 最关键的三个参数：IP、端口、从站地址。
    // IP/端口定位设备，从站地址定位设备内部的 Modbus 从站。
    m_client = new QModbusTcpClient();
    m_client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);
    m_client->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);

    //超时时间
    m_client->setTimeout(1000);
    //每一次 Modbus 读写请求，如果失败，自动重试 3 次，总共最多尝试 4 次。
    m_client->setNumberOfRetries(3);

    //设备状态改变时的 信号和槽
    connect(m_client, &QModbusClient::stateChanged, this, [=](){
        if (m_client->state() == QModbusDevice::ConnectedState)
        {
            qDebug() << "Modbus TCP 已连接";
            m_pollTimer.setInterval(intervalMs);
            m_pollTimer.start();
        }
        else if (m_client->state() == QModbusDevice::UnconnectedState)
        {
            qDebug() << "Modbus TCP 已断开";
            m_pollTimer.stop();
        }
    });

    //出错的信号和槽
    connect(m_client, &QModbusClient::errorOccurred, this, [=](){
        qDebug () << m_client->errorString();
    });

    //连接设备
    m_client->connectDevice();
}

void ModbusComm::disconnectDevice()
{
    // Qt 的 QObject 父子关系会管理内存，但这里主动 deleteLater，
    // 可以让“断开后重新连接”的生命周期更清楚。
    if (!m_client) {
        return;
    }

    if (m_client->state() != QModbusDevice::UnconnectedState) {
        m_client->disconnectDevice();
    }
    m_client->deleteLater();
    m_client = nullptr;
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

    // qDebug() << "hello";

    // 上位机的核心动作：定时读取下位机的保持寄存器。
    // 这里读取 0~3 共 4 个寄存器，对应 3 路温度和 1 路压力。
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, 0, 4);
    QModbusReply *reply = m_client->sendReadRequest(request, m_slaveId);    //向下位机发送
    if (!reply) {
        //emit errorOccurred(m_client->errorString());
        qDebug() << "reply error";
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
                //emit errorOccurred(reply->errorString());
                return;
            }
            //得到普通回复
            const QModbusDataUnit unit = reply->result();
            QVector<quint16> values;
            for (uint i = 0; i < unit.valueCount(); ++i) {
                values.append(unit.value(i));
            }
            emit dataReady(parseRegisters(values));
        });
    }
    else
    {
        qDebug() << "请求未完成!";
    }
}

SensorData ModbusComm::parseRegisters(const QVector<quint16> &values) const
{
    SensorData data;
    data.timestamp = QDateTime::currentDateTime();

    // 约定：寄存器原始值除以 10 得到工程量。
    // 例如 Modbus Slave 中 253 表示 25.3 摄氏度。
    if (values.size() >= 4) {
        data.temp1 = values.at(0) / 10.0;
        data.temp2 = values.at(1) / 10.0;
        data.temp3 = values.at(2) / 10.0;
        data.pressure = values.at(3) / 10.0;
    }
    return data;
}


