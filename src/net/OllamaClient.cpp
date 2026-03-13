#include "net/OllamaClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

OllamaClient::OllamaClient(QObject* parent)
    : QObject(parent),
      m_net(new QNetworkAccessManager(this)) {}

void OllamaClient::generate(const QUrl& endpoint, const QString& model, const QString& prompt) {
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body{
        {"model", model},
        {"prompt", prompt},
        {"stream", false},
    };

    auto* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit failed("Invalid OLLAMA response format");
            return;
        }
        emit generated(doc.object().value("response").toString());
    });
}
