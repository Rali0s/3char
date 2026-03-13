#pragma once

#include "session/SessionBackend.hpp"

#include <QProcess>

class LocalSession : public SessionBackend {
    Q_OBJECT
public:
    explicit LocalSession(QObject* parent = nullptr);

    void start(const SessionProfile& profile, const AppConfig& appConfig) override;
    void sendInput(const QByteArray& bytes) override;
    void stop() override;

private:
    QProcess m_process;
};
