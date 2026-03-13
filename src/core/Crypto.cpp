#include "core/Crypto.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
QByteArray randomBytes(int len) {
    QByteArray out;
    out.resize(len);
    for (int i = 0; i < len; ++i) {
        out[i] = static_cast<char>(QRandomGenerator::global()->bounded(0, 256));
    }
    return out;
}
}

QByteArray Crypto::deriveKey(const QString& password, const QByteArray& salt) {
    QByteArray key = password.toUtf8() + salt;
    for (int i = 0; i < 200000; ++i) {
        key = QCryptographicHash::hash(key + password.toUtf8() + salt, QCryptographicHash::Sha256);
    }
    return key;
}

QByteArray Crypto::xorStream(const QByteArray& input, const QByteArray& key, const QByteArray& iv) {
    QByteArray out = input;
    QByteArray block = key + iv;
    int counter = 0;
    for (int i = 0; i < out.size(); ++i) {
        if (i % 32 == 0) {
            QByteArray ctr = QByteArray::number(counter++);
            block = QCryptographicHash::hash(key + iv + ctr, QCryptographicHash::Sha256);
        }
        out[i] = static_cast<char>(out[i] ^ block[i % 32]);
    }
    return out;
}

QByteArray Crypto::encrypt(const QByteArray& plaintext, const QString& password) {
    const QByteArray salt = randomBytes(16);
    const QByteArray iv = randomBytes(16);
    const QByteArray key = deriveKey(password, salt);
    const QByteArray cipher = xorStream(plaintext, key, iv);
    const QByteArray mac = QCryptographicHash::hash(key + iv + cipher, QCryptographicHash::Sha256);

    QJsonObject envelope{
        {"v", 1},
        {"salt", QString::fromLatin1(salt.toBase64())},
        {"iv", QString::fromLatin1(iv.toBase64())},
        {"cipher", QString::fromLatin1(cipher.toBase64())},
        {"mac", QString::fromLatin1(mac.toBase64())},
    };
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

QByteArray Crypto::decrypt(const QByteArray& envelope, const QString& password, bool* ok) {
    if (ok) {
        *ok = false;
    }
    const auto doc = QJsonDocument::fromJson(envelope);
    if (!doc.isObject()) {
        return {};
    }
    const auto obj = doc.object();
    const QByteArray salt = QByteArray::fromBase64(obj.value("salt").toString().toLatin1());
    const QByteArray iv = QByteArray::fromBase64(obj.value("iv").toString().toLatin1());
    const QByteArray cipher = QByteArray::fromBase64(obj.value("cipher").toString().toLatin1());
    const QByteArray mac = QByteArray::fromBase64(obj.value("mac").toString().toLatin1());

    const QByteArray key = deriveKey(password, salt);
    const QByteArray expected = QCryptographicHash::hash(key + iv + cipher, QCryptographicHash::Sha256);
    if (expected != mac) {
        return {};
    }

    if (ok) {
        *ok = true;
    }
    return xorStream(cipher, key, iv);
}
