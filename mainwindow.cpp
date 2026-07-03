#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QSerialPortInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //初始化 UI 界面
    initUI();
    //通信和数据库 多线程
    initWorkers();
    //加载上一次的配置文件
    loadSettings();
    m_saveTimer = new QTimer(this);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        if (m_deviceOnline && m_lastData.timestamp.isValid() && m_lastData.valid){
            QMetaObject::invokeMethod(m_db, "insertSensorDataAsync", Qt::QueuedConnection,
                                      Q_ARG(SensorData, m_lastData));
        }
    });
    qDebug() << ui->saveIntervalSpin->value() * 1000 ;
    m_saveTimer->start(ui->saveIntervalSpin->value() * 1000);

    applyAlarmLimits();


    // 通信线程 -> UI 线程：状态、在线、错误、写入结果。
    // 通信状态
    connect(m_comm, &ModbusComm::statusChanged, this, [this](const QString &status) {
        ui->statusLabel->setText(status);
        statusBar()->showMessage(status);
    });
    // 设备状态
    connect(m_comm, &ModbusComm::onlineChanged, this, [this](bool online) {
        m_deviceOnline = online;
        ui->onlineLabel->setText(online ? "在线" : "离线");
        ui->onlineLabel->setStyleSheet(online ? "color:#16a34a;font-weight:700;" : "color:#dc2626;font-weight:700;");
    });
    // 错误
    connect(m_comm, &ModbusComm::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage("通信错误：" + message);
    });
    // 写入

    //将获取到的信息同步到ui上面
    connect(m_comm, &ModbusComm::dataReady, this, [this](const SensorData &data) {
        // 这就是上位机的数据主链路：
        // 采集到一包数据后，同时更新数值、曲线、报警逻辑。
        m_lastData = data;
        //1. 更新实时数
        updateSensorLabel("温度1", data.temp1, "℃", data.valid);
        updateSensorLabel("温度2", data.temp2, "℃", data.valid);
        updateSensorLabel("温度3", data.temp3, "℃", data.valid);
        updateSensorLabel("压力", data.pressure, "kPa", data.valid);

        //2. 更新图表
        appendChartPoint(data);

        //应用最新的低限，高限
        applyAlarmLimits();
        //3. 是否报警
        m_alarm.check(data);
        if (!data.valid) {
            statusBar()->showMessage("无效数据：" + data.invalidReason);
        }
    });

    //警报来的信号
    connect(&m_alarm, &AlarmManager::alarmTriggered, this, [this](const QString &channel, const QString &type, double value, double threshold) {
        // 报警发生时，界面变红，
        if (m_valueLabels.contains(channel)) {
            m_valueLabels[channel]->setStyleSheet("font-size:24px;font-weight:700;color:#dc2626;");
        }

        // 报警入库也投递到数据库线程。
        QMetaObject::invokeMethod(m_db, "insertAlarmRecordAsync", Qt::QueuedConnection,
                                  Q_ARG(QString, channel),
                                  Q_ARG(QString, type),
                                  Q_ARG(double, value),
                                  Q_ARG(double, threshold));

        //自动刷新报警界面
        refreshAlarmTable();
        statusBar()->showMessage(QString("报警：%1 %2，当前 %3，阈值 %4").arg(channel, type).arg(value).arg(threshold));
    });
    //恢复的信号
    connect(&m_alarm, &AlarmManager::alarmRecovered, this, [this](const QString &channel) {
        // 温度恢复正常后，界面还原
        if (m_valueLabels.contains(channel)) {
            m_valueLabels[channel]->setStyleSheet("font-size:24px;font-weight:600;color:#1f2937;");
        }
        statusBar()->showMessage(channel + " 已恢复正常");
    });


    //存储周期变化后，立即重启保存定时器。
    connect(ui->saveIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_saveTimer) {
            m_saveTimer->start(value * 1000);
        }
    });


}

MainWindow::~MainWindow()
{
    delete ui;
    // 退出前保存当前界面配置，方便下次启动恢复。
    saveSettings();
    // 先让通信线程里的 ModbusComm 主动断开，避免线程退出时还有连接或定时器在跑。
    if (m_comm) {
        QMetaObject::invokeMethod(m_comm, "disconnectDevice", Qt::BlockingQueuedConnection);
    }

    // 关闭通信线程和数据库线程。
    // wait() 确保线程真正结束后再销毁 MainWindow。
    if (m_commThread) {
        m_commThread->quit();
        m_commThread->wait();
    }

    if (m_dbThread) {
        m_dbThread->quit();
        m_dbThread->wait();
    }
}

void MainWindow::initUI()
{
    this->setWindowTitle("笑脸工业设备数据监控上位机1.0");
    resize(1180, 760);
    ui->tabWidget->setTabText(0, "实时监控");
    ui->tabWidget->setTabText(1, "历史数据");
    ui->tabWidget->setTabText(2, "报警记录");

    // 初始化实时监控的连接参数、实时数据、阈值、曲线。
    initConnectionPage();

    // 初始化历史记录的页面
    initHistoryPage();

    // 初始化报警界面
    initAlarmPage();

}

void MainWindow::initChart()
{
    // 曲线的意义是看趋势，不只是看当前值。
    // 比如温度是否持续上升、压力是否周期波动，都比单个数字更直观。
    // 1. 先创建图表顶层容器.
    m_chart = new QChart();

    // 2. 创建4条折线序列，绑定父对象this自动回收内存
    m_temp1Series = new QLineSeries(this);
    m_temp2Series = new QLineSeries(this);
    m_temp3Series = new QLineSeries(this);
    m_pressureSeries = new QLineSeries(this);

    // 设置每条曲线在图例上显示的名称
    m_temp1Series->setName("温度1");
    m_temp2Series->setName("温度2");
    m_temp3Series->setName("温度3");
    m_pressureSeries->setName("压力");

    // 3. 将4条曲线全部添加到图表容器中
    m_chart->addSeries(m_temp1Series);
    m_chart->addSeries(m_temp2Series);
    m_chart->addSeries(m_temp3Series);
    m_chart->addSeries(m_pressureSeries);

    // 设置图表顶部大标题
    m_chart->setTitle("实时趋势曲线");

    // 4. 手动创建X、Y数值坐标轴（多曲线共用同一套坐标轴）
    m_axisX = new QValueAxis(this);
    m_axisY = new QValueAxis(this);

    // X轴范围0~120，文字标签：采样点
    m_axisX->setRange(0, 120);
    m_axisX->setTitleText("采样点");

    // Y轴范围0~80，文字标签：工程量
    m_axisY->setRange(0, 80);
    m_axisY->setTitleText("工程量");

    // 把X轴添加到图表底部，Y轴添加到图表左侧
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    //循环将所有曲线绑定到X、Y坐标轴，曲线才能跟随坐标轴缩放
    QList<QLineSeries*> allSeries = {m_temp1Series, m_temp2Series, m_temp3Series, m_pressureSeries};
    for (auto *series : allSeries)
    {
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
    }

    // 将创建好的图表交给界面上的绘图控件显示
    ui->graphicsView->setChart(m_chart);
    // 开启抗锯齿，线条顺滑无锯齿
    ui->graphicsView->setRenderHint(QPainter::Antialiasing);

}

void MainWindow::initConnectionPage()
{
    // 初始化 tcp 连接 ip 端口 从站地址 采集周期
    ui->ipLine->setText("127.0.0.1");
    ui->spinPort->setMaximum(9999);
    ui->spinPort->setValue(502);
    ui->spinSlaveAddr->setValue(1);
    ui->spinInterval->setMaximum(99999);
    ui->spinInterval->setSuffix(" ms");
    ui->spinInterval->setValue(500);

    // 初始化 RTU 连接的参数
    // 串口
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        ui->serialPortCombo->addItem(info.portName());
    }
    //波特率，数据位，停止位，校验
    ui->baudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    ui->dataBitsCombo->addItems({"8", "7"});
    ui->stopBitsCombo->addItems({"1", "2"});
    ui->parityCombo->addItems({"None", "Even", "Odd"});

    // 存储周期
    ui->saveIntervalSpin->setRange(1, 3600);
    ui->saveIntervalSpin->setValue(10);
    ui->saveIntervalSpin->setSuffix(" s");

    //重连间隔
    ui->reconnectIntervalSpin->setRange(1000, 60000);
    ui->reconnectIntervalSpin->setValue(3000);
    ui->reconnectIntervalSpin->setSuffix(" ms");

    //最大重连
    ui->maxReconnectSpin->setRange(0, 100);
    ui->maxReconnectSpin->setValue(5);

    //报警延时
    ui->alarmDelaySpin->setRange(0, 60000);
    ui->alarmDelaySpin->setValue(1000);
    ui->alarmDelaySpin->setSuffix(" ms");

    //创建温度映射
    m_valueLabels.insert("温度1", ui->temp1);
    m_valueLabels.insert("温度2", ui->temp2);
    m_valueLabels.insert("温度3", ui->temp3);
    m_valueLabels.insert("压力", ui->pressure);

    //将上限下限解除
    ui->temp1_lowRange->setRange(-INFINITY, INFINITY);
    ui->temp1_highRange->setRange(-INFINITY, INFINITY);
    ui->temp2_lowRange->setRange(-INFINITY, INFINITY);
    ui->temp2_highRange->setRange(-INFINITY, INFINITY);
    ui->temp3_lowRange->setRange(-INFINITY, INFINITY);
    ui->temp3_highRange->setRange(-INFINITY, INFINITY);
    ui->pressure_lowRange->setRange(-INFINITY, INFINITY);
    ui->pressure_highRange->setRange(-INFINITY, INFINITY);

    //设置 温度 label 的字体大小
    ui->temp1->setMinimumHeight(46);
    ui->temp1->setStyleSheet("font-size:24px;font-weight:600;color:#1f2937;");
    ui->temp2->setMinimumHeight(46);
    ui->temp2->setStyleSheet("font-size:24px;font-weight:600;color:#1f2937;");
    ui->temp3->setMinimumHeight(46);
    ui->temp3->setStyleSheet("font-size:24px;font-weight:600;color:#1f2937;");
    ui->pressure->setMinimumHeight(46);
    ui->pressure->setStyleSheet("font-size:24px;font-weight:600;color:#1f2937;");

    // 初始化阈值
    ui->temp1_lowRange->setValue(0.0);
    ui->temp1_highRange->setValue(60.0);
    ui->temp2_lowRange->setValue(0.0);
    ui->temp2_highRange->setValue(60.0);
    ui->temp3_lowRange->setValue(0.0);
    ui->temp3_highRange->setValue(60.0);
    ui->pressure_lowRange->setValue(0.0);
    ui->pressure_highRange->setValue(12.0);


    // 初始化折线图
    initChart();
}

void MainWindow::initHistoryPage()
{
    //默认从前一个小时
    ui->startEdit->setDateTime(QDateTime::currentDateTime().addSecs(-3600));
    ui->startEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    ui->endEdit->setDateTime(QDateTime::currentDateTime());
    ui->endEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");

    ui->historyTable->setColumnCount(5);
    ui->historyTable->setHorizontalHeaderLabels({"时间", "温度1", "温度2", "温度3", "压力"});
    //将列全部铺满
    ui->historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::initAlarmPage()
{
    ui->alarmTable->setColumnCount(5);
    ui->alarmTable->setHorizontalHeaderLabels({"时间", "通道", "类型", "数值", "阈值"});
    //将列全部铺满
    ui->alarmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::initWorkers()
{
    // 通信对象放到独立线程：
    // Modbus 连接、定时采集、超时重连都在通信线程执行，避免阻塞 UI。
    m_commThread = new QThread(this);
    m_comm = new ModbusComm();
    m_comm->moveToThread(m_commThread);
    connect(m_commThread, &QThread::finished, m_comm, &QObject::deleteLater);
    m_commThread->start();

    // 数据库对象放到独立线程：
    // 历史写入和查询都通过 queued signal/slot 进入数据库线程。
    m_dbThread = new QThread(this);
    m_db = new DatabaseManager();
    m_db->moveToThread(m_dbThread);
    connect(m_dbThread, &QThread::finished, m_db, &QObject::deleteLater);
    m_dbThread->start();
    if (!QMetaObject::invokeMethod(m_db, "open", Qt::QueuedConnection))
    {
        QMessageBox::warning(this, "数据库初始化失败", m_db->lastError());
        return;
    }
}

RuntimeConfig MainWindow::runtimeConfig() const
{
    RuntimeConfig config;
    config.sampleIntervalMs = ui->spinInterval->value();
    config.saveIntervalSec = ui->saveIntervalSpin->value();
    config.reconnectIntervalMs = ui->reconnectIntervalSpin->value();
    config.maxReconnectCount = ui->maxReconnectSpin->value();
    config.alarmDelayMs = ui->alarmDelaySpin->value();
    return config;
}

void MainWindow::updateSensorLabel(const QString &name, double value, const QString &unit, bool valid)
{
    if (!m_valueLabels.contains(name)) {
        return;
    }

    m_valueLabels[name]->setText(valid ? QString::number(value, 'f', 1) + " " + unit : "无效");
}

void MainWindow::appendChartPoint(const SensorData &data)
{
    //暂停 或者 数据无效，不往图表中追加数据
    if (m_chartPaused || !data.valid){
        return;
    }

    ++m_sampleIndex;
    qDebug() << "更新图表";

    m_temp1Series->append(m_sampleIndex, data.temp1);
    m_temp2Series->append(m_sampleIndex, data.temp2);
    m_temp3Series->append(m_sampleIndex, data.temp3);
    m_pressureSeries->append(m_sampleIndex, data.pressure);

    const int keepPoints = 120;
    // 只保留最近 120 个点，避免软件运行很久后曲线数据无限增长。
    for (auto *series : {m_temp1Series, m_temp2Series, m_temp3Series, m_pressureSeries}) {
        while (series->count() > keepPoints) {
            series->remove(0);
        }
    }
    if (m_sampleIndex > keepPoints) {
        m_axisX->setRange(m_sampleIndex - keepPoints, m_sampleIndex);
    }
}

void MainWindow::on_conTcp_clicked()
{

    qDebug() << "on_conTcp_clicked 线程 : " << QThread::currentThread();

    // 点击连接时先保存当前界面配置，再把连接任务投递给通信线程。
    saveSettings();

    applyAlarmLimits();
    m_sampleIndex = 0;
    RuntimeConfig config = runtimeConfig();

    qDebug () << "点击了连接按钮";
    if (ui->tcpMode->isChecked()) {
        // TCP 模式：IP + 端口 + 从站地址。
        // 点击“连接 Modbus TCP”后，软件开始从外部 Modbus Slave/PLC 采集数据。
        qDebug() << "选择了 TCP 模式连接";
        QMetaObject::invokeMethod(m_comm, [=]() {
            m_comm->connectTcp(ui->ipLine->text(), ui->spinPort->value(), ui->spinSlaveAddr->value(), config);
        }, Qt::QueuedConnection);
        //m_comm->connectTcp(ui->ipLine->text(), ui->spinPort->value(), ui->spinSlaveAddr->value(), config); 单线程
        // QMetaObject::invokeMethod(m_comm, "connectTcp", Qt::QueuedConnection,
        //                           Q_ARG(QString, ui->ipLine->text()),
        //                           Q_ARG(int, ui->spinPort->value()),
        //                           Q_ARG(int, ui->spinSlaveAddr->value()),
        //                           Q_ARG(RuntimeConfig, config));
    } else if (ui->rtuMode->isChecked()) {
        // RTU 模式：串口参数 + 从站地址。
        qDebug() << "选择了 RTU 模式连接";
        QMetaObject::invokeMethod(m_comm, "connectRtu", Qt::QueuedConnection,
                                  Q_ARG(QString, ui->serialPortCombo->currentText()),
                                  Q_ARG(int, ui->baudCombo->currentText().toInt()),
                                  Q_ARG(QSerialPort::DataBits, (QSerialPort::DataBits)ui->stopBitsCombo->currentText().toInt()),
                                  Q_ARG(QSerialPort::StopBits, (QSerialPort::StopBits)ui->stopBitsCombo->currentText().toInt()),
                                  Q_ARG(QSerialPort::Parity, (QSerialPort::Parity)ui->parityCombo->currentText().toInt()),
                                  Q_ARG(int, ui->spinSlaveAddr->value()),
                                  Q_ARG(RuntimeConfig, config));
    } else {
        // 模拟模式：不用外部设备，由通信线程定时生成假数据。
    }

    //setConnectedUi(true);
}


void MainWindow::on_disCon_clicked()
{
    qDebug () << "点击了关闭按钮";
    QMetaObject::invokeMethod(m_comm, "disconnectDevice", Qt::QueuedConnection);
    //setConnectedUi(false);
}


void MainWindow::on_pushButton_clicked()
{
    qDebug() << "点击了模拟测试按钮，对于没有模拟从站设备，以及没有下位机的情况。。。。";
    //模拟数据。
}




QTableWidgetItem* MainWindow::makeCell(const QString& text)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void MainWindow::refreshAlarmTable()
{

    QMetaObject::invokeMethod(m_db, "queryAlarmsAsync", Qt::QueuedConnection);

    connect(m_db, &DatabaseManager::alarmsReady, this, [=](const QList<QVariantMap> &rows){
        // 先清空旧表格内容
        ui->alarmTable->clearContents();
        // 设置表格总行数
        ui->alarmTable->setRowCount(rows.size());

        for (int row = 0; row < rows.size(); ++row) {
            const QVariantMap item = rows.at(row);
            ui->alarmTable->setItem(row, 0, makeCell(item["timestamp"].toString()));
            ui->alarmTable->setItem(row, 1, makeCell(item["channel"].toString()));
            ui->alarmTable->setItem(row, 2, makeCell(item["alarm_type"].toString()));
            ui->alarmTable->setItem(row, 3, makeCell(QString::number(item["value"].toDouble(), 'f', 1)));
            ui->alarmTable->setItem(row, 4, makeCell(QString::number(item["threshold"].toDouble(), 'f', 1)));
        }
    });
}

void MainWindow::applyAlarmLimits()
{
    //设置报警时延
    m_alarm.setDelayMs(ui->alarmDelaySpin->value());
    // 阈值直接从界面读取，用户改完后下一次采样就会生效。
    m_alarm.setLimit("温度1", ui->temp1_lowRange->value(), ui->temp1_highRange->value());
    m_alarm.setLimit("温度2", ui->temp2_lowRange->value(),ui->temp2_highRange->value());
    m_alarm.setLimit("温度3", ui->temp3_lowRange->value(), ui->temp3_highRange->value());
    m_alarm.setLimit("压力", ui->pressure_lowRange->value(), ui->pressure_highRange->value());
}

void MainWindow::loadSettings()
{
    // 使用 ini 文件保存配置，便于拷贝、查看和随程序一起交付。
    const QString iniPath = QCoreApplication::applicationDirPath() + "/industrial_monitor.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    settings.beginGroup("ModbusTCP");
    ui->ipLine->setText(settings.value("IP", "127.0.0.1").toString());
    ui->spinPort->setValue(settings.value("Port", 502).toInt());
    ui->spinSlaveAddr->setValue(settings.value("SlaveID", 1).toInt());
    ui->spinInterval->setValue(settings.value("SampleIntervalMs", 100).toInt());
    ui->saveIntervalSpin->setValue(settings.value("SaveIntervalSec", 10).toInt());
    ui->reconnectIntervalSpin->setValue(settings.value("ReconnectIntervalMs", 3000).toInt());
    settings.endGroup();

    settings.beginGroup("ModbusRTU");
    const QString savedPort = settings.value("ComPort", ui->serialPortCombo->currentText()).toString();
    if (!savedPort.isEmpty()) {
        const int index = ui->serialPortCombo->findText(savedPort);
        if (index >= 0) {
            ui->serialPortCombo->setCurrentIndex(index);
        } else {
            ui->serialPortCombo->addItem(savedPort);
            ui->serialPortCombo->setCurrentText(savedPort);
        }
    }
    ui->baudCombo->setCurrentText(settings.value("BaudRate", "9600").toString());
    ui->dataBitsCombo->setCurrentText(settings.value("DataBits", "8").toString());
    ui->stopBitsCombo->setCurrentText(settings.value("StopBits", "1").toString());
    ui->parityCombo->setCurrentText(settings.value("Parity", "None").toString());
    settings.endGroup();

    settings.beginGroup("WindowUI");
    resize(settings.value("Width", width()).toInt(), settings.value("Height", height()).toInt());
    settings.endGroup();

    settings.beginGroup("AlarmSetting");
    ui->maxReconnectSpin->setValue(settings.value("MaxRetryCount", 5).toInt());
    ui->alarmDelaySpin->setValue(settings.value("AlarmDelayMs", 1000).toInt());

    // 加载三个温度 以及 压力的 高低阈值
    ui->temp1_lowRange->setValue(settings.value("温度1Low",  ui->temp1_lowRange->value()).toDouble());
    ui->temp1_highRange->setValue(settings.value("温度1High",  ui->temp1_highRange->value()).toDouble());
    ui->temp2_lowRange->setValue(settings.value("温度2Low",  ui->temp2_lowRange->value()).toDouble());
    ui->temp2_highRange->setValue(settings.value("温度2High",  ui->temp2_highRange->value()).toDouble());
    ui->temp3_lowRange->setValue(settings.value("温度3Low",  ui->temp3_lowRange->value()).toDouble());
    ui->temp3_highRange->setValue(settings.value("温度3High",  ui->temp3_highRange->value()).toDouble());
    ui->pressure_lowRange->setValue(settings.value("压力Low",  ui->pressure_lowRange->value()).toDouble());
    ui->pressure_highRange->setValue(settings.value("压力High", ui->pressure_highRange->value()).toDouble());

    settings.endGroup();
}

void MainWindow::saveSettings()
{

    // 关闭程序或点击连接时保存配置到 ini 文件，下一次启动直接恢复。
    const QString iniPath = QCoreApplication::applicationDirPath() + "/industrial_monitor.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    settings.beginGroup("ModbusTCP");
    settings.setValue("IP", ui->ipLine->text());
    settings.setValue("Port", ui->spinPort->value());
    settings.setValue("SlaveID", ui->spinSlaveAddr->value());
    settings.setValue("SampleIntervalMs", ui->spinInterval->value());
    settings.setValue("SaveIntervalSec", ui->saveIntervalSpin->value());
    settings.setValue("ReconnectIntervalMs", ui->reconnectIntervalSpin->value());
    settings.endGroup();

    settings.beginGroup("ModbusRTU");
    settings.setValue("ComPort", ui->serialPortCombo->currentText());
    settings.setValue("BaudRate", ui->baudCombo->currentText());
    settings.setValue("DataBits", ui->dataBitsCombo->currentText());
    settings.setValue("StopBits", ui->stopBitsCombo->currentText());
    settings.setValue("Parity", ui->parityCombo->currentText());
    settings.endGroup();

    settings.beginGroup("WindowUI");
    settings.setValue("Width", width());
    settings.setValue("Height", height());
    settings.endGroup();

    settings.beginGroup("AlarmSetting");
    settings.setValue("EnableAlarm", 1);
    settings.setValue("MaxRetryCount", ui->maxReconnectSpin->value());
    settings.setValue("AlarmDelayMs", ui->alarmDelaySpin->value());


    // 保存三个温度 以及 压力的 高低阈值
    settings.setValue("温度1Low", ui->temp1_lowRange->value());
    settings.setValue("温度1High", ui->temp1_highRange->value());
    settings.setValue("温度2Low", ui->temp2_lowRange->value());
    settings.setValue("温度2High", ui->temp2_highRange->value());
    settings.setValue("温度3Low", ui->temp3_lowRange->value());
    settings.setValue("温度3High", ui->temp3_highRange->value());
    settings.setValue("压力Low", ui->pressure_lowRange->value());
    settings.setValue("压力High", ui->pressure_highRange->value());
    settings.endGroup();
}


//点击了查询按钮
void MainWindow::on_queryButton_clicked()
{
    qDebug() << "点击了查询按钮";
    QMetaObject::invokeMethod(m_db, "queryHistoryAsync", Qt::QueuedConnection,
                              Q_ARG(QDateTime, ui->startEdit->dateTime()),
                              Q_ARG(QDateTime, ui->endEdit->dateTime()));
    connect(m_db, &DatabaseManager::historyReady, this, [=](const QList<QVariantMap> &rows){
        // 封装生成居中只读单元格 匿名函数
        // auto makeCell = [](const QString& text)->QTableWidgetItem*{
        //     QTableWidgetItem *item = new QTableWidgetItem(text);
        //     item->setTextAlignment(Qt::AlignCenter);
        //     item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        //     return item;
        // };
        // 先清空旧表格内容
        ui->historyTable->clearContents();
        // 设置表格总行数
        ui->historyTable->setRowCount(rows.size());

        for (int row = 0; row < rows.size(); ++row) {
            const QVariantMap item = rows.at(row);
            ui->historyTable->setItem(row, 0, makeCell(item["timestamp"].toString()));
            ui->historyTable->setItem(row, 1, makeCell(QString::number(item["temp1"].toDouble(), 'f', 1)));
            ui->historyTable->setItem(row, 2, makeCell(QString::number(item["temp2"].toDouble(), 'f', 1)));
            ui->historyTable->setItem(row, 3, makeCell(QString::number(item["temp3"].toDouble(), 'f', 1)));
            ui->historyTable->setItem(row, 4, makeCell(QString::number(item["pressure"].toDouble(), 'f', 1)));
        }

    });

}


void MainWindow::on_refreshButton_clicked()
{
    qDebug() << "点击了刷新报警记按钮";
    refreshAlarmTable();
}

void MainWindow::on_pauseChartButton_clicked()
{
    qDebug() << "点击了图标暂停按钮";
    // 曲线暂停按钮：只影响曲线追加，不影响采集和数据库保存。
    m_chartPaused = !m_chartPaused;
    ui->pauseChartButton->setText(m_chartPaused ? "继续曲线" : "暂停曲线");
}

