#include "session/PtySession.hpp"

#include <QProcess>
#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

#ifndef Q_OS_WIN
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace {
QByteArrayList toArgv(const QStringList& parts) {
    QByteArrayList out;
    for (const auto& p : parts) {
        out.push_back(p.toUtf8());
    }
    return out;
}
}

PtySession::PtySession(QObject* parent)
    : SessionBackend(parent) {}

PtySession::~PtySession() {
    stop();
}

void PtySession::start(const SessionProfile& profile, const AppConfig& appConfig) {
#ifndef Q_OS_WIN
    startUnixPty(profile, appConfig);
#else
    Q_UNUSED(profile)
    Q_UNUSED(appConfig)
    emit errorRaised("PTY backend on Windows is not implemented in this build");
#endif
}

#ifndef Q_OS_WIN
void PtySession::startUnixPty(const SessionProfile& profile, const AppConfig& appConfig) {
    if (m_masterFd >= 0) {
        stop();
    }

    struct winsize ws {};
    ws.ws_row = 40;
    ws.ws_col = 120;

    m_childPid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (m_childPid < 0) {
        emit errorRaised("forkpty failed");
        return;
    }

    if (m_childPid == 0) {
        QStringList parts;
        if (profile.type == "ssh") {
            parts << "ssh";
            if (profile.port > 0) {
                parts << "-p" << QString::number(profile.port);
            }
            if (!profile.proxyRef.isEmpty()) {
                const auto pcmd = buildProxyCommand(profile, appConfig);
                if (!pcmd.isEmpty()) {
                    parts << "-o" << QString("ProxyCommand=%1").arg(pcmd);
                }
            }
            if (profile.authMode == "key" && !profile.keyPath.isEmpty()) {
                parts << "-i" << profile.keyPath;
            }
            const auto target = profile.username.isEmpty() ? profile.host : QString("%1@%2").arg(profile.username, profile.host);
            parts << target;
        } else {
            QString shellCommand = profile.shellCommand;
            if (shellCommand.isEmpty()) {
                shellCommand = qEnvironmentVariable("SHELL", "/bin/bash");
            }
            parts = QProcess::splitCommand(shellCommand);
            if (parts.isEmpty()) {
                parts << "/bin/bash";
            }
        }

        auto argvUtf8 = toArgv(parts);
        std::vector<char*> argv;
        argv.reserve(static_cast<size_t>(argvUtf8.size()) + 1);
        for (auto& b : argvUtf8) {
            argv.push_back(b.data());
        }
        argv.push_back(nullptr);

        ::execvp(argvUtf8.first().constData(), argv.data());
        _exit(127);
    }

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
        char buf[8192];
        const auto n = ::read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
            emit outputReady(QByteArray(buf, n));
            return;
        }

        int status = 0;
        (void)::waitpid(m_childPid, &status, WNOHANG);
        emit finished();
    });
}
#endif

QString PtySession::buildProxyCommand(const SessionProfile& profile, const AppConfig& appConfig) const {
    ProxyProfile proxy;
    bool found = false;
    for (const auto& p : appConfig.proxies) {
        if (p.id == profile.proxyRef) {
            proxy = p;
            found = true;
            break;
        }
    }
    if (!found) {
        return {};
    }

    const auto secret = appConfig.secrets.value(proxy.secretRef);
    if (proxy.type == "socks5") {
        if (!proxy.username.isEmpty() || !secret.isEmpty()) {
            return QString("connect-proxy -5 -S %1:%2 -p %3:%4 %%h %%p")
                .arg(proxy.host).arg(proxy.port).arg(proxy.username, secret);
        }
        return QString("nc -X 5 -x %1:%2 %%h %%p").arg(proxy.host).arg(proxy.port);
    }
    if (proxy.type == "http_connect") {
        if (!proxy.username.isEmpty() || !secret.isEmpty()) {
            return QString("connect-proxy -H %1:%2 -p %3:%4 %%h %%p")
                .arg(proxy.host).arg(proxy.port).arg(proxy.username, secret);
        }
        return QString("nc -X connect -x %1:%2 %%h %%p").arg(proxy.host).arg(proxy.port);
    }
    return {};
}

void PtySession::sendInput(const QByteArray& bytes) {
#ifndef Q_OS_WIN
    if (m_masterFd >= 0) {
        (void)::write(m_masterFd, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
#else
    Q_UNUSED(bytes)
#endif
}

void PtySession::resize(int cols, int rows) {
#ifndef Q_OS_WIN
    if (m_masterFd < 0) return;
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    (void)::ioctl(m_masterFd, TIOCSWINSZ, &ws);
#else
    Q_UNUSED(cols)
    Q_UNUSED(rows)
#endif
}

void PtySession::stop() {
#ifndef Q_OS_WIN
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    if (m_childPid > 0) {
        const pid_t pid = m_childPid;
        m_childPid = -1;
        (void)::kill(-pid, SIGHUP);
        (void)::kill(pid, SIGHUP);

        // Reap asynchronously so tab close never blocks the UI thread.
        std::thread([pid]() {
            auto waitForExit = [pid](int timeoutMs) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
                while (std::chrono::steady_clock::now() < deadline) {
                    int status = 0;
                    const pid_t r = ::waitpid(pid, &status, WNOHANG);
                    if (r == pid || (r < 0 && errno == ECHILD)) {
                        return true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                return false;
            };

            if (!waitForExit(200)) {
                (void)::kill(-pid, SIGTERM);
                (void)::kill(pid, SIGTERM);
            }
            if (!waitForExit(600)) {
                (void)::kill(-pid, SIGKILL);
                (void)::kill(pid, SIGKILL);
            }

            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    }
#endif
}
