#include "core/AppController.hpp"
#include "core/ConfigStore.hpp"
#include "ui/MainWindow.hpp"
#include "ui/MasterPasswordDialog.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QSettings>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("3Char");
    QCoreApplication::setApplicationName("3TTY");

    QSettings startupSettings;
    const bool guestBypassEnabled = startupSettings.value("startup/guestBypass", false).toBool();

    AppController controller;
    if (guestBypassEnabled) {
        (void)controller.initializeGuest();
    } else {
        ConfigStore configStore;
        MasterPasswordDialog pwdDialog(configStore.exists());
        if (pwdDialog.exec() != QDialog::Accepted) {
            return 0;
        }
        if (pwdDialog.guestModeRequested()) {
            (void)controller.initializeGuest();
        } else {
            if (pwdDialog.password().isEmpty()) {
                return 0;
            }
            QString err;
            if (!controller.initialize(pwdDialog.password(), &err)) {
                QMessageBox::critical(nullptr, "Unlock failed", err);
                return 1;
            }
        }
    }

    MainWindow w(&controller);
    w.show();

    return app.exec();
}
