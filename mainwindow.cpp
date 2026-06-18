#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initUI();

    //将获取到的信息同步到ui上面
    connect(&m_comm, &ModbusComm::dataReady, this, [this](const SensorData &data) {
        // 这就是上位机的数据主链路：
        // 采集到一包数据后，同时更新数值、曲线、报警逻辑。

        // 测试数据是否获取成功
        // qDebug() << data.timestamp << " ";
        // qDebug() << data.temp1 << " ";
        // qDebug() << data.temp2 << " ";
        // qDebug() << data.temp3 << " ";
        // qDebug() << data.pressure << " ";

        //1. 更新实时数
        ui->temp1->setText(QString::number(data.temp1) + " ℃");
        ui->temp2->setText(QString::number(data.temp2) + " ℃");
        ui->temp3->setText(QString::number(data.temp3) + " ℃");
        ui->pressure->setText(QString::number(data.pressure) + " ℃");

        //2. 更新图表
        appendChartPoint(data);


        //3. 是否报警
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initUI()
{
    this->setWindowTitle("笑脸工业设备数据监控上位机");

    ui->tabWidget->setTabText(0, "实时监控");
    ui->tabWidget->setTabText(1, "历史数据");
    ui->tabWidget->setTabText(2, "报警记录");

    // 初始化实时监控的连接参数、实时数据、阈值、曲线。
    initConnectionPage();
    // 初始化折线图
    initChart();
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


    // 初始化阈值
    ui->temp1_LowRange->setValue(0.0);
    ui->temp1_highRange->setValue(60.0);
    ui->temp2_lowRange->setValue(0.0);
    ui->temp2_highRange->setValue(60.0);
    ui->temp3_lowRange->setValue(0.0);
    ui->temp3_highRange->setValue(60.0);
    ui->pressure_lowRange->setValue(0.0);
    ui->pressure_highRange->setValue(12.0);
}

void MainWindow::appendChartPoint(const SensorData &data)
{
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
    qDebug () << "点击了 TCP 连接按钮";
    // 点击“连接 Modbus TCP”后，软件开始从外部 Modbus Slave/PLC 采集数据。
    m_sampleIndex = 0;
    m_comm.connectTcp(ui->ipLine->text(), ui->spinPort->value(), ui->spinSlaveAddr->value(), ui->spinInterval->value());

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


