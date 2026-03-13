#include "session/LocalSession.hpp"

#include <QOperatingSystemVersion>

LocalSession::LocalSession(QObject* parent)
    : SessionBackend(parent) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        emit outputReady(QString::fromLocal8Bit(m_process.readAllStandardOutput()));
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        emit outputReady(QString::fromLocal8Bit(m_process.readAllStandardError()));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        emit errorRaised(QString("Local process error: %1").arg(static_cast<int>(e)));
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this]() {
        emit finished();
    });
}

void LocalSession::start(const SessionProfile& profile, const AppConfig&) {
    QString shell = profile.shellCommand;
    QStringList args;
    if (shell.isEmpty()) {
#ifdef Q_OS_WIN
        shell = "powershell.exe";
#else
        shell = qEnvironmentVariable("SHELL", "/bin/bash");
#endif
    }

#ifdef Q_OS_WIN
    if (shell.contains(" ")) {
        m_process.start(shell, args);
    } else {
        m_process.start(shell, args);
    }
#else
    m_process.start(shell, args);
#endif

    emit outputReady(QString("[local] starting: %1\n").arg(shell));
}

void LocalSession::sendInput(const QByteArray& bytes) {
    m_process.write(bytes);
}

void LocalSession::stop() {
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(1000)) {
            m_process.kill();
        }
    }
}
