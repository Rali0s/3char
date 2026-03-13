#pragma once

#include "data/Models.hpp"

class SessionStore {
public:
    SessionStore();

    const AppConfig& config() const;
    AppConfig& config();

    void upsertProfile(const SessionProfile& profile);
    bool removeProfile(const QString& id);
    SessionProfile profileById(const QString& id) const;

private:
    AppConfig m_config;
};
