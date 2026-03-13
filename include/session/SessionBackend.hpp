#pragma once

#include "data/Models.hpp"

#include <QObject>

class SessionBackend : public QObject {
    Q_OBJECT
public:
    explicit SessionBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~SessionBackend() override = default;

    virtual void start(const SessionProfile& profile, const AppConfig& appConfig) = 0;
    virtual void sendInput(const QByteArray& bytes) = 0;
    virtual void resize(int cols, int rows) = 0;
    virtual void stop() = 0;

signals:
    void outputReady(const QByteArray& bytes);
    void errorRaised(const QString& text);
    void finished();
};
