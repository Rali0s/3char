#pragma once

#include "session/SessionBackend.hpp"

#include <QSocketNotifier>

class PtySession : public SessionBackend {
    Q_OBJECT
public:
    explicit PtySession(QObject* parent = nullptr);
    ~PtySession() override;

    void start(const SessionProfile& profile, const AppConfig& appConfig) override;
    void sendInput(const QByteArray& bytes) override;
    void resize(int cols, int rows) override;
    void stop() override;

private:
    void startUnixPty(const SessionProfile& profile, const AppConfig& appConfig);
    QString buildProxyCommand(const SessionProfile& profile, const AppConfig& appConfig) const;

    int m_masterFd = -1;
    int m_childPid = -1;
    QSocketNotifier* m_notifier = nullptr;
};
