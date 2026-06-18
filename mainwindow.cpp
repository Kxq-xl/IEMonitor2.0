#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initUI();

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
    // 点击“连接 Modbus TCP”后，软件开始从外部 Modbus Slave/PLC 采集数据。

}

