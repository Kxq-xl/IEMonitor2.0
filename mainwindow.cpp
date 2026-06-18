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

        ui->temp1->setText(QString::number(data.temp1) + " ℃");
        ui->temp2->setText(QString::number(data.temp2) + " ℃");
        ui->temp3->setText(QString::number(data.temp3) + " ℃");
        ui->pressure->setText(QString::number(data.pressure) + " ℃");

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

void MainWindow::on_conTcp_clicked()
{
    qDebug () << "点击了 TCP 连接按钮";
    // 点击“连接 Modbus TCP”后，软件开始从外部 Modbus Slave/PLC 采集数据。
    m_comm.connectTcp(ui->ipLine->text(), ui->spinPort->value(), ui->spinSlaveAddr->value(), ui->spinInterval->value());

}


void MainWindow::on_disCon_clicked()
{
    qDebug () << "点击了关闭按钮";
    m_comm.disconnectDevice();
}

