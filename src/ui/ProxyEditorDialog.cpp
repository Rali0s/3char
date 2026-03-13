#include "ui/ProxyEditorDialog.hpp"

#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QVBoxLayout>

ProxyEditorDialog::ProxyEditorDialog(const QList<ProxyProfile>& proxies, QWidget* parent)
    : QDialog(parent),
      m_editor(new QPlainTextEdit(this)) {
    setWindowTitle("Proxy Profiles (JSON)");

    QJsonArray arr;
    for (const auto& p : proxies) {
        arr.append(p.toJson());
    }
    m_editor->setPlainText(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented)));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_editor);
    layout->addWidget(buttons);
}

QList<ProxyProfile> ProxyEditorDialog::proxies(bool* ok, QString* error) const {
    if (ok) *ok = false;
    const auto doc = QJsonDocument::fromJson(m_editor->toPlainText().toUtf8());
    if (!doc.isArray()) {
        if (error) *error = "Proxy JSON must be an array";
        return {};
    }

    QList<ProxyProfile> out;
    for (const auto& it : doc.array()) {
        if (!it.isObject()) continue;
        out.push_back(ProxyProfile::fromJson(it.toObject()));
    }
    if (ok) *ok = true;
    return out;
}
