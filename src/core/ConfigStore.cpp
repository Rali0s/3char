#include "core/ConfigStore.hpp"

#include "core/Crypto.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

ConfigStore::ConfigStore() {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    m_path = base + "/config.enc";
}

bool ConfigStore::exists() const {
    return QFile::exists(m_path);
}

bool ConfigStore::save(const AppConfig& config, const QString& masterPassword, QString* error) const {
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("Failed to open %1 for write").arg(m_path);
        return false;
    }
    const auto cipher = Crypto::encrypt(ModelJson::toBytes(config), masterPassword);
    if (file.write(cipher) < 0) {
        if (error) *error = "Failed to write encrypted config";
        return false;
    }
    return true;
}

AppConfig ConfigStore::load(const QString& masterPassword, bool* ok, QString* error) const {
    if (ok) *ok = false;

    QFile file(m_path);
    if (!file.exists()) {
        if (ok) *ok = true;
        return AppConfig{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Failed to open %1 for read").arg(m_path);
        return AppConfig{};
    }

    bool decrypted = false;
    auto plain = Crypto::decrypt(file.readAll(), masterPassword, &decrypted);
    if (!decrypted) {
        if (error) *error = "Invalid master password or corrupted config";
        return AppConfig{};
    }

    if (ok) *ok = true;
    return ModelJson::fromBytes(plain);
}

bool ConfigStore::exportPlainJson(const AppConfig& config, const QString& path, QString* error) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("Failed to open %1 for export").arg(path);
        return false;
    }
    if (file.write(ModelJson::toBytes(config)) < 0) {
        if (error) *error = "Failed to write export JSON";
        return false;
    }
    return true;
}

AppConfig ConfigStore::importPlainJson(const QString& path, bool* ok, QString* error) const {
    if (ok) *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Failed to open %1 for import").arg(path);
        return AppConfig{};
    }
    if (ok) *ok = true;
    return ModelJson::fromBytes(file.readAll());
}

QString ConfigStore::configPath() const {
    return m_path;
}
