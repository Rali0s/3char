#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct ProxyProfile {
    QString id;
    QString type; // socks5|http_connect
    QString host;
    int port = 0;
    QString username;
    QString secretRef;

    QJsonObject toJson() const;
    static ProxyProfile fromJson(const QJsonObject& obj);
};

struct SessionProfile {
    QString id;
    QString name;
    QString groupPath;
    QStringList tags;
    QString type; // local|ssh
    QString shellCommand;
    QString host;
    int port = 22;
    QString username;
    QString proxyRef;
    QString authMode; // password|key
    QString keyPath;
    QString notes;

    QJsonObject toJson() const;
    static SessionProfile fromJson(const QJsonObject& obj);
};

struct OllamaConfig {
    QString model = "llama3.1";
    QString endpoint = "http://127.0.0.1:11434/api/generate";

    QJsonObject toJson() const;
    static OllamaConfig fromJson(const QJsonObject& obj);
};

struct TerminalSettings {
    int standardHistoryLines = 1000;
    bool expandedHistoryEnabled = false;
    bool rightClickAutoCopy = true;
    QString copyPasteMode = "platform"; // platform|ctrl_shift|ctrl_alt

    QJsonObject toJson() const;
    static TerminalSettings fromJson(const QJsonObject& obj);
};

struct ScheduledWorkflow {
    QString name;
    QString timeHHMM; // 24h format, e.g. 12:00
    QString profileQuery; // id or name
    QString scriptName;
    bool enabled = true;
    QString lastRunDate; // yyyy-MM-dd

    QJsonObject toJson() const;
    static ScheduledWorkflow fromJson(const QJsonObject& obj);
};

struct AppConfig {
    int version = 1;
    QList<SessionProfile> profiles;
    QList<ProxyProfile> proxies;
    QMap<QString, QString> secrets;
    QMap<QString, QString> scripts;
    QList<ScheduledWorkflow> workflows;
    TerminalSettings terminal;
    QString uiState;
    OllamaConfig ollama;

    QJsonObject toJson() const;
    static AppConfig fromJson(const QJsonObject& obj);
};

namespace ModelJson {
QByteArray toBytes(const AppConfig& config);
AppConfig fromBytes(const QByteArray& bytes);
}
