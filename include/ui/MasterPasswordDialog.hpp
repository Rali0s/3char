#pragma once

#include <QDialog>

class QLineEdit;
class QLabel;

class MasterPasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit MasterPasswordDialog(bool hasExistingConfig, QWidget* parent = nullptr);

    QString password() const;
    bool guestModeRequested() const;

private:
    bool m_guestModeRequested = false;
    QLineEdit* m_password;
    QLabel* m_title;
};
