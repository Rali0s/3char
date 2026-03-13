#pragma once

#include "core/ConfigStore.hpp"
#include "core/SessionStore.hpp"

#include <QString>

class AppController {
public:
    bool initialize(const QString& masterPassword, QString* error = nullptr);
    bool initializeGuest();
    bool save(QString* error = nullptr);
    bool isGuestMode() const;

    SessionStore& sessions();
    const SessionStore& sessions() const;

    QString notepadPath() const;
    QString masterPassword() const;

private:
    void ensureDefaultProfiles();

    ConfigStore m_configStore;
    SessionStore m_sessionStore;
    QString m_masterPassword;
    bool m_guestMode = false;
};
