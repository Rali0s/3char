#include "core/ConfigStore.hpp"
#include "core/Crypto.hpp"
#include "data/Models.hpp"

#include <QCoreApplication>
#include <QFile>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QByteArray plain = "hello";
    const auto enc = Crypto::encrypt(plain, "pass");
    bool ok = false;
    const auto dec = Crypto::decrypt(enc, "pass", &ok);
    if (!ok || dec != plain) {
        std::cerr << "crypto round-trip failed\n";
        return 1;
    }

    bool wrongOk = true;
    (void)Crypto::decrypt(enc, "wrong", &wrongOk);
    if (wrongOk) {
        std::cerr << "crypto wrong-password check failed\n";
        return 1;
    }

    AppConfig cfg;
    SessionProfile p;
    p.id = "id1";
    p.name = "Local";
    p.type = "local";
    cfg.profiles.push_back(p);

    const auto bytes = ModelJson::toBytes(cfg);
    const auto parsed = ModelJson::fromBytes(bytes);
    if (parsed.profiles.size() != 1 || parsed.profiles.first().id != "id1") {
        std::cerr << "model json round-trip failed\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
