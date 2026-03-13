#pragma once

#include <QDialog>
#include <QMap>

class QPlainTextEdit;

class SecretsEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SecretsEditorDialog(const QMap<QString, QString>& secrets, QWidget* parent = nullptr);

    QMap<QString, QString> secrets(bool* ok, QString* error) const;

private:
    QPlainTextEdit* m_editor;
};
