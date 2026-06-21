#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>


// databasemanager.h / databasemanager.cpp
// 数据持久化模块：负责把实时采集数据、报警事件写入 SQLite，
// 也负责给“历史数据”和“报警记录”页面提供查询结果。

class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);


    // 打开程序运行目录下的 industrial_monitor.db，并自动建表。
    bool open();



    // 查询指定时间范围内的历史采样数据。
    QList<QVariantMap> queryHistory(const QDateTime &start, const QDateTime &end);

    // 返回最近一次数据库错误，方便界面提示。
    QString lastError() const;
signals:


private:
    // 创建运行数据表和报警记录表。
    bool createTables();

    QSqlDatabase m_db;
    QString m_lastError;
};

#endif // DATABASEMANAGER_H
