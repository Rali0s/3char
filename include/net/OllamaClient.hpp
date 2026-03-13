#pragma once

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject* parent = nullptr);

    void generate(const QUrl& endpoint, const QString& model, const QString& prompt);

signals:
    void generated(const QString& output);
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_net;
};
