#include "ui/SecretsEditorDialog.hpp"

#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QVBoxLayout>

SecretsEditorDialog::SecretsEditorDialog(const QMap<QString, QString>& secrets, QWidget* parent)
    : QDialog(parent),
      m_editor(new QPlainTextEdit(this)) {
    setWindowTitle("Secrets (JSON key/value)");

    QJsonObject obj;
    for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
        obj.insert(it.key(), it.value());
    }
    m_editor->setPlainText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_editor);
    layout->addWidget(buttons);
}

QMap<QString, QString> SecretsEditorDialog::secrets(bool* ok, QString* error) const {
    if (ok) *ok = false;
    const auto doc = QJsonDocument::fromJson(m_editor->toPlainText().toUtf8());
    if (!doc.isObject()) {
        if (error) *error = "Secrets JSON must be an object";
        return {};
    }

    QMap<QString, QString> out;
    for (auto it = doc.object().begin(); it != doc.object().end(); ++it) {
        out[it.key()] = it.value().toString();
    }
    if (ok) *ok = true;
    return out;
}
