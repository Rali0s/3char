#include "core/SessionStore.hpp"

SessionStore::SessionStore() = default;

const AppConfig& SessionStore::config() const {
    return m_config;
}

AppConfig& SessionStore::config() {
    return m_config;
}

void SessionStore::upsertProfile(const SessionProfile& profile) {
    for (auto& p : m_config.profiles) {
        if (p.id == profile.id) {
            p = profile;
            return;
        }
    }
    m_config.profiles.push_back(profile);
}

bool SessionStore::removeProfile(const QString& id) {
    for (int i = 0; i < m_config.profiles.size(); ++i) {
        if (m_config.profiles[i].id == id) {
            m_config.profiles.removeAt(i);
            return true;
        }
    }
    return false;
}

SessionProfile SessionStore::profileById(const QString& id) const {
    for (const auto& p : m_config.profiles) {
        if (p.id == id) {
            return p;
        }
    }
    return SessionProfile{};
}
