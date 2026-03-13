#include "ui/MasterPasswordDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

MasterPasswordDialog::MasterPasswordDialog(bool hasExistingConfig, QWidget* parent)
    : QDialog(parent),
      m_password(new QLineEdit(this)),
      m_title(new QLabel(this)) {
    setWindowTitle("Master Password");
    m_password->setEchoMode(QLineEdit::Password);
    m_title->setText(hasExistingConfig
                        ? "Enter master password to unlock profiles"
                        : "Create master password for encrypted config");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_guestModeRequested = false;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* guestButton = new QPushButton("Continue as Guest (No Password, No Storage)", this);
    connect(guestButton, &QPushButton::clicked, this, [this]() {
        m_guestModeRequested = true;
        accept();
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addWidget(m_password);
    layout->addWidget(guestButton);
    layout->addWidget(buttons);
}

QString MasterPasswordDialog::password() const {
    return m_password->text();
}

bool MasterPasswordDialog::guestModeRequested() const {
    return m_guestModeRequested;
}
