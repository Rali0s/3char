#pragma once

#include "data/Models.hpp"

#include <QDialog>

class QPlainTextEdit;

class ProxyEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProxyEditorDialog(const QList<ProxyProfile>& proxies, QWidget* parent = nullptr);

    QList<ProxyProfile> proxies(bool* ok, QString* error) const;

private:
    QPlainTextEdit* m_editor;
};
