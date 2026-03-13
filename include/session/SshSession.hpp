#pragma once

#include "session/SessionBackend.hpp"

#include <QProcess>
#include <atomic>
#include <thread>

class SshSession : public SessionBackend {
    Q_OBJECT
public:
    explicit SshSession(QObject* parent = nullptr);
    ~SshSession() override;

    void start(const SessionProfile& profile, const AppConfig& appConfig) override;
    void sendInput(const QByteArray& bytes) override;
    void stop() override;

private:
#ifdef HAS_LIBSSH
    void runLibSsh(SessionProfile profile, AppConfig appConfig);
    std::thread m_worker;
    std::atomic<bool> m_stop{false};
    void* m_sshSession = nullptr;
    void* m_channel = nullptr;
#else
    QProcess m_fallbackSsh;
#endif
};
