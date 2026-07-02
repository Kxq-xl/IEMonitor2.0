#include "databasemanager.h"


DatabaseManager::DatabaseManager(QObject *parent)
    : QObject{parent}
{}

bool DatabaseManager::open()
{
    qDebug() << "DatabaseManager::open 线程 : " << QThread::currentThread();

    // 数据库放在 exe 所在目录。
    // Qt Creator 用不同构建目录运行时，db 文件也会跟着出现在对应运行目录。
    const QString dbPath = QCoreApplication::applicationDirPath() + QDir::separator() + "industrial_monitor.db";

    // 获取默认数据库连接
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    // 设置数据库文件路径
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    const bool ok = createTables();
    emit databaseReady(ok, ok ? "数据库已打开" : m_lastError);
    return ok;
}

bool DatabaseManager::insertSensorData(const SensorData &data)
{

    qDebug() << "DatabaseManager::insertSensorData 线程 : " << QThread::currentThread();

    // 保存运行数据，供“历史数据”页面按时间查询。
    QSqlQuery query(m_db);
    query.prepare("insert into t_sensor_data(timestamp,temp1,temp2,temp3,pressure) values(?,?,?,?,?)");
    query.addBindValue(data.timestamp.toString(Qt::ISODate));
    query.addBindValue(data.temp1);
    query.addBindValue(data.temp2);
    query.addBindValue(data.temp3);
    query.addBindValue(data.pressure);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QList<QVariantMap> DatabaseManager::queryHistory(const QDateTime &start, const QDateTime &end)
{
    qDebug() << "DatabaseManager::queryHistory 线程 : " << QThread::currentThread();

    // 查询指定时间范围内的历史采样数据。
    // QVariantMap 让 UI 层不用关心 SQL 字段顺序，按名字取值即可。
    QList<QVariantMap> rows;
    QSqlQuery query(m_db);
    query.prepare("select timestamp,temp1,temp2,temp3,pressure from t_sensor_data "
                  "where timestamp between ? and ? order by timestamp desc limit 500");
    query.addBindValue(start.toString(Qt::ISODate));
    query.addBindValue(end.toString(Qt::ISODate));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return rows;
    }

    while (query.next()) {
        QVariantMap row;
        row["timestamp"] = query.value(0);
        row["temp1"] = query.value(1);
        row["temp2"] = query.value(2);
        row["temp3"] = query.value(3);
        row["pressure"] = query.value(4);
        rows.append(row);
    }
    return rows;
}

QList<QVariantMap> DatabaseManager::queryAlarms()
{
    qDebug() << "DatabaseManager::queryAlarms 线程 : " << QThread::currentThread();

    // QVariantMap 让 UI 层不用关心 SQL 字段顺序，按名字取值即可。
    QList<QVariantMap> rows;
    QSqlQuery query(m_db);
    query.prepare("select timestamp,channel,alarm_type,value,threshold from t_alarm_record order by id desc limit 200");
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return rows;
    }

    while (query.next()) {
        QVariantMap row;
        row["timestamp"] = query.value(0);
        row["channel"] = query.value(1);
        row["alarm_type"] = query.value(2);
        row["value"] = query.value(3);
        row["threshold"] = query.value(4);
        rows.append(row);
    }
    return rows;
}

bool DatabaseManager::insertAlarmRecord(const QString &channel, const QString &type, double value, double threshold)
{
    qDebug() << "DatabaseManager::insertAlarmRecord 线程 : " << QThread::currentThread();

    // 保存报警事件，真实工业软件通常会长期保留报警记录。
    QSqlQuery query(m_db);
    query.prepare("insert into t_alarm_record(timestamp,channel,alarm_type,value,threshold) values(?,?,?,?,?)");
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(channel);
    query.addBindValue(type);
    query.addBindValue(value);
    query.addBindValue(threshold);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

void DatabaseManager::insertSensorDataAsync(const SensorData &data)
{
    // Async 函数本身不创建线程；它依赖 MainWindow 用 QueuedConnection
    // 投递到数据库线程执行。
    if (!insertSensorData(data)) {
        emit databaseError(m_lastError);
    }
}

void DatabaseManager::insertAlarmRecordAsync(const QString &channel, const QString &type, double value, double threshold)
{
    if (!insertAlarmRecord(channel, type, value, threshold)) {
        emit databaseError(m_lastError);
    }
}

void DatabaseManager::queryHistoryAsync(const QDateTime &start, const QDateTime &end)
{
    // 查询完成后通过信号把结果发回 UI 线程。
    emit historyReady(queryHistory(start, end));
}

void DatabaseManager::queryAlarmsAsync()
{
    // 查询完成后通过信号把结果发回 UI 线程。
    emit alarmsReady(queryAlarms());
}

bool DatabaseManager::createTables()
{
    // 第一次运行时自动建表；表已存在时不会重复创建。
    QSqlQuery query(m_db);
    // 传感器历史数据表
    const QString sensorSql =
        "create table if not exists t_sensor_data("
        "id integer primary key autoincrement,"
        "timestamp text not null,"
        "temp1 real,"
        "temp2 real,"
        "temp3 real,"
        "pressure real)";

    // 报警记录表
    const QString alarmSql =
        "create table if not exists t_alarm_record("
        "id integer primary key autoincrement,"
        "timestamp text not null,"
        "channel text,"
        "alarm_type text,"
        "value real,"
        "threshold real)";

    // 执行历史记录表创建
    if (!query.exec(sensorSql)) {
        m_lastError = query.lastError().text();
        return false;
    }
    // 执行报警表创建
    if (!query.exec(alarmSql)) {
        m_lastError = query.lastError().text();
        return false;
    }

    qDebug() << "打开数据库成功!";
    return true;
}
