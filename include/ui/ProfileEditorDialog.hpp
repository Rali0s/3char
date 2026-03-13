#pragma once

#include "data/Models.hpp"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

class ProfileEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileEditorDialog(QWidget* parent = nullptr);

    void setProfile(const SessionProfile& profile);
    SessionProfile profile() const;

private:
    QLineEdit* m_id;
    QLineEdit* m_name;
    QLineEdit* m_groupPath;
    QLineEdit* m_tags;
    QComboBox* m_type;
    QLineEdit* m_shellCommand;
    QLineEdit* m_host;
    QLineEdit* m_port;
    QLineEdit* m_username;
    QLineEdit* m_proxyRef;
    QComboBox* m_authMode;
    QLineEdit* m_keyPath;
    QPlainTextEdit* m_notes;
};
