#pragma once

#include <QByteArray>
#include <QString>

class Crypto {
public:
    static QByteArray encrypt(const QByteArray& plaintext, const QString& password);
    static QByteArray decrypt(const QByteArray& envelope, const QString& password, bool* ok = nullptr);

private:
    static QByteArray deriveKey(const QString& password, const QByteArray& salt);
    static QByteArray xorStream(const QByteArray& input, const QByteArray& key, const QByteArray& iv);
};
