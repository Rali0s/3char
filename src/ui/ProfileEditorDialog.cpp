#include "ui/ProfileEditorDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

ProfileEditorDialog::ProfileEditorDialog(QWidget* parent)
    : QDialog(parent),
      m_id(new QLineEdit(this)),
      m_name(new QLineEdit(this)),
      m_groupPath(new QLineEdit(this)),
      m_tags(new QLineEdit(this)),
      m_type(new QComboBox(this)),
      m_shellCommand(new QLineEdit(this)),
      m_host(new QLineEdit(this)),
      m_port(new QLineEdit(this)),
      m_username(new QLineEdit(this)),
      m_proxyRef(new QLineEdit(this)),
      m_authMode(new QComboBox(this)),
      m_keyPath(new QLineEdit(this)),
      m_notes(new QPlainTextEdit(this)) {
    setWindowTitle("Session Profile");
    m_type->addItems({"local", "ssh"});
    m_authMode->addItems({"password", "key"});
    m_port->setText("22");

    auto* form = new QFormLayout();
    form->addRow("id", m_id);
    form->addRow("name", m_name);
    form->addRow("groupPath", m_groupPath);
    form->addRow("tags (comma)", m_tags);
    form->addRow("type", m_type);
    form->addRow("shellCommand", m_shellCommand);
    form->addRow("host", m_host);
    form->addRow("port", m_port);
    form->addRow("username", m_username);
    form->addRow("proxyRef", m_proxyRef);
    form->addRow("authMode", m_authMode);
    form->addRow("keyPath", m_keyPath);
    form->addRow("notes", m_notes);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ProfileEditorDialog::setProfile(const SessionProfile& profile) {
    m_id->setText(profile.id);
    m_name->setText(profile.name);
    m_groupPath->setText(profile.groupPath);
    m_tags->setText(profile.tags.join(","));
    m_type->setCurrentText(profile.type);
    m_shellCommand->setText(profile.shellCommand);
    m_host->setText(profile.host);
    m_port->setText(QString::number(profile.port));
    m_username->setText(profile.username);
    m_proxyRef->setText(profile.proxyRef);
    m_authMode->setCurrentText(profile.authMode);
    m_keyPath->setText(profile.keyPath);
    m_notes->setPlainText(profile.notes);
}

SessionProfile ProfileEditorDialog::profile() const {
    SessionProfile p;
    p.id = m_id->text();
    p.name = m_name->text();
    p.groupPath = m_groupPath->text();
    p.tags = m_tags->text().split(',', Qt::SkipEmptyParts);
    p.type = m_type->currentText();
    p.shellCommand = m_shellCommand->text();
    p.host = m_host->text();
    p.port = m_port->text().toInt();
    p.username = m_username->text();
    p.proxyRef = m_proxyRef->text();
    p.authMode = m_authMode->currentText();
    p.keyPath = m_keyPath->text();
    p.notes = m_notes->toPlainText();
    return p;
}
