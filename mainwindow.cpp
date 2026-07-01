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

    //打开数据库
    if (!m_db.open())
    {
        QMessageBox::warning(this, "数据库初始化失败", m_db.lastError());
        //return;
    }


    m_saveTimer = new QTimer();
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        if (m_comm.isConnected()) {
            // 工业软件通常不会把每个采样点都入库，否则数据库增长太快。
            // 这里每 2 秒保存一次，用来体验历史追溯功能。
            m_db.insertSensorData(m_lastData);
        }
    });
    m_saveTimer->start(2000);

    applyAlarmLimits();

    // 通信状态
    connect(&m_comm, &ModbusComm::statusChanged, this, [this](const QString &status) {
        ui->statusLabel->setText(status);
        statusBar()->showMessage(status);
    });
    // 设备状态
    connect(&m_comm, &ModbusComm::onlineChanged, this, [this](bool online) {
        ui->onlineLabel->setText(online ? "在线" : "离线");
        ui->onlineLabel->setStyleSheet(online ? "color:#16a34a;font-weight:700;" : "color:#dc2626;font-weight:700;");
    });

    //将获取到的信息同步到ui上面
    connect(&m_comm, &ModbusComm::dataReady, this, [this](const SensorData &data) {
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
        //同时写入数据库，方便后续追溯。
        m_db.insertAlarmRecord(channel, type, value, threshold);
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
}

MainWindow::~MainWindow()
{
    delete ui;
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
    // 点击连接时先保存当前界面配置，再把连接任务投递给通信线程。
    //saveSettings();
    applyAlarmLimits();
    m_sampleIndex = 0;
    //RuntimeConfig config = runtimeConfig();

    qDebug () << "点击了连接按钮";


    if (ui->tcpMode->isChecked()) {
        // TCP 模式：IP + 端口 + 从站地址。
        // 点击“连接 Modbus TCP”后，软件开始从外部 Modbus Slave/PLC 采集数据。
        qDebug() << "选择了 TCP 模式连接";
        m_comm.connectTcp(ui->ipLine->text(), ui->spinPort->value(), ui->spinSlaveAddr->value(), ui->spinInterval->value());
    } else if (ui->rtuMode->isChecked()) {
        // RTU 模式：串口参数 + 从站地址。
        qDebug() << "选择了 RTU 模式连接";
        m_comm.connectRtu(ui->serialPortCombo->currentText()
                          ,ui->baudCombo->currentText().toInt()
                          ,(QSerialPort::DataBits)ui->dataBitsCombo->currentText().toInt()
                          ,(QSerialPort::StopBits)ui->stopBitsCombo->currentText().toInt()
                          ,(QSerialPort::Parity)ui->parityCombo->currentText().toInt()
                          ,ui->spinSlaveAddr->value(), ui->spinInterval->value());

    } else {
        // 模拟模式：不用外部设备，由通信线程定时生成假数据。
    }

    //setConnectedUi(true);
}


void MainWindow::on_disCon_clicked()
{
    qDebug () << "点击了关闭按钮";
    m_comm.disconnectDevice();
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
    QList<QVariantMap> rows = m_db.queryAlarms();

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


//点击了查询按钮
void MainWindow::on_queryButton_clicked()
{
    qDebug() << "点击了查询按钮";
    QList<QVariantMap> rows = m_db.queryHistory(ui->startEdit->dateTime(), ui->endEdit->dateTime());

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

