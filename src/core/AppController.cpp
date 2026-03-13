#include "core/AppController.hpp"

#include <QDir>
#include <QStandardPaths>

bool AppController::initialize(const QString& masterPassword, QString* error) {
    m_guestMode = false;
    bool ok = false;
    m_sessionStore.config() = m_configStore.load(masterPassword, &ok, error);
    if (!ok) {
        return false;
    }
    ensureDefaultProfiles();
    m_masterPassword = masterPassword;
    return true;
}

bool AppController::initializeGuest() {
    m_guestMode = true;
    m_masterPassword.clear();
    m_sessionStore.config() = AppConfig{};
    ensureDefaultProfiles();
    return true;
}

bool AppController::save(QString* error) {
    if (m_guestMode) {
        if (error) {
            error->clear();
        }
        return true;
    }
    return m_configStore.save(m_sessionStore.config(), m_masterPassword, error);
}

bool AppController::isGuestMode() const {
    return m_guestMode;
}

SessionStore& AppController::sessions() {
    return m_sessionStore;
}

const SessionStore& AppController::sessions() const {
    return m_sessionStore;
}

QString AppController::notepadPath() const {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + "/notepad.txt";
}

QString AppController::masterPassword() const {
    return m_masterPassword;
}

void AppController::ensureDefaultProfiles() {
    if (!m_sessionStore.config().profiles.isEmpty()) {
        return;
    }

    SessionProfile local;
    local.id = "local-default";
    local.name = "Local Shell";
    local.type = "local";
#ifdef Q_OS_WIN
    local.shellCommand = "powershell.exe";
#else
    local.shellCommand = qEnvironmentVariable("SHELL", "/bin/bash");
#endif
    m_sessionStore.upsertProfile(local);

#ifdef Q_OS_WIN
    SessionProfile cmd;
    cmd.id = "local-cmd";
    cmd.name = "Command Prompt";
    cmd.type = "local";
    cmd.shellCommand = "cmd.exe";
    m_sessionStore.upsertProfile(cmd);

    SessionProfile msys;
    msys.id = "local-msys2";
    msys.name = "MSYS2 Bash";
    msys.type = "local";
    msys.shellCommand = "C:/msys64/usr/bin/bash.exe";
    m_sessionStore.upsertProfile(msys);
#endif
}
