#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 初始化整个主界面 Tab 容器。
    void initUI();

    // 初始化“实时监控”页面：连接参数、实时数据、阈值、曲线。
    void initConnectionPage();

private slots:
    void on_conTcp_clicked();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
