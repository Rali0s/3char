#include "session/SshSession.hpp"

#include <QMetaObject>

#ifdef HAS_LIBSSH
#include <libssh/libssh.h>
#endif

namespace {
bool verifyKnownHost(ssh_session session, QString* message) {
    int state = ssh_session_is_known_server(session);
    if (state == SSH_KNOWN_HOSTS_OK) {
        return true;
    }
    if (state == SSH_KNOWN_HOSTS_NOT_FOUND || state == SSH_KNOWN_HOSTS_UNKNOWN) {
        if (ssh_session_update_known_hosts(session) == SSH_OK) {
            if (message) {
                *message = "Host key was unknown and has been trusted for future sessions.";
            }
            return true;
        }
        if (message) {
            *message = "Host key verification failed while updating known hosts.";
        }
        return false;
    }
    if (message) {
        *message = "Host key changed or verification failed.";
    }
    return false;
}

QString buildProxyCommand(const SessionProfile& profile, const AppConfig& appConfig) {
    if (profile.proxyRef.isEmpty()) {
        return {};
    }
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
}

SshSession::SshSession(QObject* parent)
    : SessionBackend(parent) {
#ifndef HAS_LIBSSH
    connect(&m_fallbackSsh, &QProcess::readyReadStandardOutput, this, [this]() {
        emit outputReady(QString::fromLocal8Bit(m_fallbackSsh.readAllStandardOutput()));
    });
    connect(&m_fallbackSsh, &QProcess::readyReadStandardError, this, [this]() {
        emit outputReady(QString::fromLocal8Bit(m_fallbackSsh.readAllStandardError()));
    });
#endif
}

SshSession::~SshSession() {
    stop();
}

void SshSession::start(const SessionProfile& profile, const AppConfig& appConfig) {
#ifdef HAS_LIBSSH
    m_stop = false;
    m_worker = std::thread([this, profile, appConfig]() { runLibSsh(profile, appConfig); });
#else
    QStringList args;
    const auto target = QString("%1@%2").arg(profile.username, profile.host);
    const auto proxyCommand = buildProxyCommand(profile, appConfig);
    if (!proxyCommand.isEmpty()) {
        args << "-o" << QString("ProxyCommand=%1").arg(proxyCommand);
    }
    args << "-p" << QString::number(profile.port > 0 ? profile.port : 22) << target;
    m_fallbackSsh.start("ssh", args);
    emit outputReady("[ssh] libssh unavailable, using system ssh fallback\n");
#endif
}

void SshSession::sendInput(const QByteArray& bytes) {
#ifdef HAS_LIBSSH
    if (m_channel) {
        ssh_channel_write(static_cast<ssh_channel>(m_channel), bytes.constData(), bytes.size());
    }
#else
    m_fallbackSsh.write(bytes);
#endif
}

void SshSession::stop() {
#ifdef HAS_LIBSSH
    m_stop = true;
    if (m_channel) {
        ssh_channel_close(static_cast<ssh_channel>(m_channel));
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
#else
    if (m_fallbackSsh.state() != QProcess::NotRunning) {
        m_fallbackSsh.terminate();
    }
#endif
}

#ifdef HAS_LIBSSH
void SshSession::runLibSsh(SessionProfile profile, AppConfig appConfig) {
    ssh_session s = ssh_new();
    if (!s) {
        QMetaObject::invokeMethod(this, [this]() { emit errorRaised("Failed to allocate ssh session"); }, Qt::QueuedConnection);
        return;
    }

    ssh_options_set(s, SSH_OPTIONS_HOST, profile.host.toStdString().c_str());
    const int port = profile.port > 0 ? profile.port : 22;
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    const auto user = profile.username.toStdString();
    ssh_options_set(s, SSH_OPTIONS_USER, user.c_str());
    const auto proxyCommand = buildProxyCommand(profile, appConfig);
    if (!proxyCommand.isEmpty()) {
        const auto pc = proxyCommand.toStdString();
        ssh_options_set(s, SSH_OPTIONS_PROXYCOMMAND, pc.c_str());
    }

    if (ssh_connect(s) != SSH_OK) {
        const QString msg = QString("SSH connect failed: %1").arg(ssh_get_error(s));
        ssh_free(s);
        QMetaObject::invokeMethod(this, [this, msg]() { emit errorRaised(msg); }, Qt::QueuedConnection);
        return;
    }
    QString hostMsg;
    if (!verifyKnownHost(s, &hostMsg)) {
        const QString msg = QString("SSH host key check failed: %1").arg(hostMsg);
        ssh_disconnect(s);
        ssh_free(s);
        QMetaObject::invokeMethod(this, [this, msg]() { emit errorRaised(msg); }, Qt::QueuedConnection);
        return;
    }
    if (!hostMsg.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, hostMsg]() { emit outputReady("[ssh] " + hostMsg + "\n"); }, Qt::QueuedConnection);
    }

    int rc = SSH_AUTH_DENIED;
    if (profile.authMode == "password") {
        const auto pw = appConfig.secrets.value(profile.id + ".password");
        rc = ssh_userauth_password(s, nullptr, pw.toStdString().c_str());
    } else {
        rc = ssh_userauth_publickey_auto(s, nullptr, profile.keyPath.isEmpty() ? nullptr : profile.keyPath.toStdString().c_str());
    }
    if (rc != SSH_AUTH_SUCCESS) {
        const QString msg = QString("SSH auth failed: %1").arg(ssh_get_error(s));
        ssh_disconnect(s);
        ssh_free(s);
        QMetaObject::invokeMethod(this, [this, msg]() { emit errorRaised(msg); }, Qt::QueuedConnection);
        return;
    }

    ssh_channel ch = ssh_channel_new(s);
    if (!ch || ssh_channel_open_session(ch) != SSH_OK || ssh_channel_request_pty(ch) != SSH_OK || ssh_channel_request_shell(ch) != SSH_OK) {
        const QString msg = QString("SSH channel setup failed: %1").arg(ssh_get_error(s));
        if (ch) ssh_channel_free(ch);
        ssh_disconnect(s);
        ssh_free(s);
        QMetaObject::invokeMethod(this, [this, msg]() { emit errorRaised(msg); }, Qt::QueuedConnection);
        return;
    }

    m_sshSession = s;
    m_channel = ch;
    QMetaObject::invokeMethod(this, [this]() { emit outputReady("[ssh] connected\n"); }, Qt::QueuedConnection);

    char buffer[4096];
    while (!m_stop) {
        const int n = ssh_channel_read_timeout(ch, buffer, sizeof(buffer), 0, 100);
        if (n > 0) {
            const QString text = QString::fromUtf8(buffer, n);
            QMetaObject::invokeMethod(this, [this, text]() { emit outputReady(text); }, Qt::QueuedConnection);
        } else if (n == SSH_ERROR || ssh_channel_is_closed(ch)) {
            break;
        }
    }

    ssh_channel_close(ch);
    ssh_channel_free(ch);
    ssh_disconnect(s);
    ssh_free(s);
    m_channel = nullptr;
    m_sshSession = nullptr;

    QMetaObject::invokeMethod(this, [this]() { emit finished(); }, Qt::QueuedConnection);
}
#endif
