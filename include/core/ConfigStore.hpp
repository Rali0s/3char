#pragma once

#include "data/Models.hpp"

#include <QString>

class ConfigStore {
public:
    ConfigStore();

    bool exists() const;
    bool save(const AppConfig& config, const QString& masterPassword, QString* error = nullptr) const;
    AppConfig load(const QString& masterPassword, bool* ok, QString* error = nullptr) const;

    bool exportPlainJson(const AppConfig& config, const QString& path, QString* error = nullptr) const;
    AppConfig importPlainJson(const QString& path, bool* ok, QString* error = nullptr) const;

    QString configPath() const;

private:
    QString m_path;
};
