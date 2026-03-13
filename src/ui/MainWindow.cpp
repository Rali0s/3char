#include "ui/MainWindow.hpp"

#include "net/OllamaClient.hpp"
#include "ui/ProfileEditorDialog.hpp"
#include "ui/ProxyEditorDialog.hpp"
#include "ui/QuadTerminalView.hpp"
#include "ui/SecretsEditorDialog.hpp"
#include "ui/TerminalTab.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMap>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <thread>

#ifdef HAS_LIBSSH
#include <libssh/libssh.h>
#endif

namespace {
#ifdef HAS_LIBSSH
QString quoteForBash(const QString& input) {
    QString out = input;
    out.replace("'", "'\"'\"'");
    return QString("'%1'").arg(out);
}

bool verifyKnownHost(ssh_session session, QString* message) {
    const int state = ssh_session_is_known_server(session);
    if (state == SSH_KNOWN_HOSTS_OK) {
        return true;
    }
    if (state == SSH_KNOWN_HOSTS_NOT_FOUND || state == SSH_KNOWN_HOSTS_UNKNOWN) {
        if (ssh_session_update_known_hosts(session) == SSH_OK) {
            if (message) *message = "Host key was unknown and has been trusted.";
            return true;
        }
        if (message) *message = "Failed to update known_hosts.";
        return false;
    }
    if (message) *message = "Host key check failed (changed/revoked).";
    return false;
}

struct NativeSshResult {
    bool ok = false;
    int exitCode = -1;
    QString error;
    QString hostMessage;
};

NativeSshResult runNativeSshScript(const SessionProfile& profile,
                                   const AppConfig& cfg,
                                   const QString& proxyCommand,
                                   const QString& script,
                                   const QString& logPath) {
    NativeSshResult result;
    ssh_session s = ssh_new();
    if (!s) {
        result.error = "Failed to allocate ssh session";
        return result;
    }

    const auto host = profile.host.toStdString();
    const int port = profile.port > 0 ? profile.port : 22;
    const auto user = profile.username.toStdString();
    ssh_options_set(s, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    if (!profile.username.isEmpty()) {
        ssh_options_set(s, SSH_OPTIONS_USER, user.c_str());
    }
    if (!proxyCommand.isEmpty()) {
        const auto pc = proxyCommand.toStdString();
        ssh_options_set(s, SSH_OPTIONS_PROXYCOMMAND, pc.c_str());
    }

    if (ssh_connect(s) != SSH_OK) {
        result.error = QString("SSH connect failed: %1").arg(ssh_get_error(s));
        ssh_free(s);
        return result;
    }

    QString hostMsg;
    if (!verifyKnownHost(s, &hostMsg)) {
        result.error = QString("SSH host key check failed: %1").arg(hostMsg);
        ssh_disconnect(s);
        ssh_free(s);
        return result;
    }
    result.hostMessage = hostMsg;

    int rc = SSH_AUTH_DENIED;
    if (profile.authMode == "password") {
        const auto pw = cfg.secrets.value(profile.id + ".password");
        rc = ssh_userauth_password(s, nullptr, pw.toStdString().c_str());
    } else {
        rc = ssh_userauth_publickey_auto(
            s,
            nullptr,
            profile.keyPath.isEmpty() ? nullptr : profile.keyPath.toStdString().c_str());
    }

    if (rc != SSH_AUTH_SUCCESS) {
        result.error = QString("SSH auth failed: %1").arg(ssh_get_error(s));
        ssh_disconnect(s);
        ssh_free(s);
        return result;
    }

    ssh_channel ch = ssh_channel_new(s);
    if (!ch || ssh_channel_open_session(ch) != SSH_OK) {
        result.error = QString("SSH channel open failed: %1").arg(ssh_get_error(s));
        if (ch) ssh_channel_free(ch);
        ssh_disconnect(s);
        ssh_free(s);
        return result;
    }

    QFile logFile(logPath);
    if (!logFile.open(QIODevice::WriteOnly)) {
        result.error = "Cannot open workflow log file";
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        ssh_disconnect(s);
        ssh_free(s);
        return result;
    }

    const auto cmd = QString("bash -lc %1").arg(quoteForBash(script));
    if (ssh_channel_request_exec(ch, cmd.toStdString().c_str()) != SSH_OK) {
        result.error = QString("SSH exec failed: %1").arg(ssh_get_error(s));
        logFile.close();
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        ssh_disconnect(s);
        ssh_free(s);
        return result;
    }

    char buffer[4096];
    while (true) {
        const int nOut = ssh_channel_read(ch, buffer, sizeof(buffer), 0);
        if (nOut > 0) {
            logFile.write(buffer, nOut);
        }
        const int nErr = ssh_channel_read(ch, buffer, sizeof(buffer), 1);
        if (nErr > 0) {
            logFile.write(buffer, nErr);
        }
        if ((nOut == 0 && nErr == 0 && ssh_channel_is_eof(ch)) || (nOut < 0 || nErr < 0)) {
            break;
        }
    }
    logFile.flush();
    logFile.close();

    result.exitCode = ssh_channel_get_exit_status(ch);
    result.ok = true;

    ssh_channel_send_eof(ch);
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    ssh_disconnect(s);
    ssh_free(s);
    return result;
}
#endif
} // namespace

MainWindow::MainWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent),
      m_controller(controller),
      m_profileTree(nullptr),
      m_filter(nullptr),
      m_mainSplitter(nullptr),
      m_terminalTabs(nullptr),
      m_utilityTabs(nullptr),
      m_cheatsheet(nullptr),
      m_proxyConfigs(nullptr),
      m_scriptsJson(nullptr),
      m_workflowsJson(nullptr),
      m_schedulerLog(nullptr),
      m_runWorkflowName(nullptr),
      m_notepad(nullptr),
      m_ollamaPrompt(nullptr),
      m_ollamaOutput(nullptr),
      m_ollamaTarget(nullptr),
      m_schedulerTimer(new QTimer(this)),
      m_ollamaClient(new OllamaClient(this)) {
    setupUi();
    refreshProfileTree();
    loadNotepad();
}

MainWindow::~MainWindow() {
    saveNotepad();
}

void MainWindow::setupUi() {
    setWindowTitle("3TTY");
    resize(1400, 900);

    m_mainSplitter = new QSplitter(this);

    auto* leftPane = new QWidget(m_mainSplitter);
    leftPane->setMinimumWidth(120);
    leftPane->setMaximumWidth(240);
    auto* leftLayout = new QVBoxLayout(leftPane);
    m_filter = new QLineEdit(leftPane);
    m_filter->setPlaceholderText("Filter by name/tag/group");

    auto* addBtn = new QPushButton("Add", leftPane);
    auto* editBtn = new QPushButton("Edit", leftPane);
    auto* delBtn = new QPushButton("Delete", leftPane);
    auto* openBtn = new QPushButton("Open", leftPane);
    auto* openQuadBtn = new QPushButton("MultiMode", leftPane);
    addBtn->setText("+");
    editBtn->setText("#_");
    delBtn->setText("-");
    openQuadBtn->setText("X^N");

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addBtn);
    buttonRow->addWidget(editBtn);
    buttonRow->addWidget(delBtn);
    buttonRow->addWidget(openBtn);
    buttonRow->addWidget(openQuadBtn);

    m_profileTree = new QTreeWidget(leftPane);
    m_profileTree->setHeaderLabels({"Sessions"});
    m_profileTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    leftLayout->addWidget(new QLabel("Server List", leftPane));
    leftLayout->addWidget(m_filter);
    leftLayout->addLayout(buttonRow);
    leftLayout->addWidget(m_profileTree, 1);

    auto* rightPane = new QWidget(m_mainSplitter);
    auto* rightLayout = new QVBoxLayout(rightPane);

    auto* quickRow = new QHBoxLayout();
    auto* quickId = new QLineEdit(rightPane);
    quickId->setPlaceholderText("Quick Connect by profile id or name");
    auto* quickOpen = new QPushButton("Connect", rightPane);
    quickRow->addWidget(quickId);
    quickRow->addWidget(quickOpen);

    m_terminalTabs = new QTabWidget(rightPane);
    m_terminalTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_terminalTabs->tabBar(), &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* tabBar = m_terminalTabs->tabBar();
        const int tabIndex = tabBar->tabAt(pos);
        if (tabIndex < 0) {
            return;
        }
        QMenu menu(this);
        auto* closeAction = menu.addAction("Close");
        const auto* chosen = menu.exec(tabBar->mapToGlobal(pos));
        if (chosen == closeAction) {
            if (auto* w = m_terminalTabs->widget(tabIndex)) {
                m_terminalTabs->removeTab(tabIndex);
                w->deleteLater();
            }
        }
    });
    connect(m_terminalTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx < 0) {
            applyWorkspaceDensity(false);
            return;
        }
        auto* w = m_terminalTabs->widget(idx);
        const bool isMulti = w && w->property("multimode").toBool();
        applyWorkspaceDensity(isMulti);
    });

    m_utilityTabs = new QTabWidget(rightPane);
    auto* helpPage = new QTextEdit(m_utilityTabs);
    helpPage->setReadOnly(true);
    helpPage->setPlainText(
        "3TTY Help\n"
        "================\n"
        "\n"
        "Purpose\n"
        "-------\n"
        "3TTY is a SysAdmin terminal toolbox for local shell work, SSH profiles, proxy-aware\n"
        "connections, script scheduling, and operational notes in one desktop app.\n"
        "\n"
        "How To Use (Quick Start)\n"
        "------------------------\n"
        "1) Create or edit server profiles on the left (Add/Edit).\n"
        "2) Click Open to launch a terminal tab for that profile.\n"
        "3) Use Quick Connect with profile id OR profile name.\n"
        "4) Select 2+ profiles and click MultiMode for grid mode.\n"
        "5) Save scripts/workflows in Scripts/Scheduler.\n"
        "6) Scheduler executes enabled workflows at configured HH:MM and writes logs.\n"
        "\n"
        "Major Tabs\n"
        "----------\n"
        "- Help: this guide and app purpose.\n"
        "- Settings: history + right-click auto-copy + keyboard copy/paste mode.\n"
        "- Settings also includes Guest Mode startup bypass (no password, no storage).\n"
        "- Bash Cheatsheet: quick command references.\n"
        "- Tor/SSH Config: proxy-auth config snippets for torrc/ssh/sshd.\n"
        "- Scripts/Scheduler: script storage + daily workflow automation + logs.\n"
        "- Empty Notepad: persistent notes.\n"
        "- Local OLLAMA: local code/script generation panel.\n"
        "\n"
        "LLAMA AI Safety (LOCAL ONLY)\n"
        "----------------------------\n"
        "- 3TTY is configured to call a LOCAL OLLAMA endpoint by default:\n"
        "  http://127.0.0.1:11434/api/generate\n"
        "- 127.0.0.1 means loopback on your own machine only.\n"
        "- Prompts and responses stay local unless you manually change endpoint/model configuration.\n"
        "- There is no built-in cloud fallback for OLLAMA in this app.\n"
        "\n"
        "Workflow Example (Noon SSH)\n"
        "---------------------------\n"
        "Create script name NOONSSH, then workflow:\n"
        "name=Noon Foxtail, timeHHMM=12:00, profileQuery=foxtail, scriptName=NOONSSH, enabled=true.\n"
        "\n"
        "Logs\n"
        "----\n"
        "Scheduler logs are saved under AppData/workflow-logs (normal mode).\n"
    );

    auto* settingsPage = new QWidget(m_utilityTabs);
    auto* settingsLayout = new QVBoxLayout(settingsPage);
    auto* guestSessionLabel = new QLabel(
        m_controller->isGuestMode()
            ? "Guest Mode is ACTIVE: no profile/notepad storage will be written."
            : "Guest Mode is OFF.",
        settingsPage
    );
    auto* guestBypassStartupCheck = new QCheckBox("Bypass password on startup (Guest Mode: no storage)", settingsPage);
    auto* historyLinesSlider = new QSlider(Qt::Horizontal, settingsPage);
    historyLinesSlider->setRange(1000, 10000);
    historyLinesSlider->setSingleStep(100);
    historyLinesSlider->setPageStep(500);
    auto* historyLinesValue = new QLabel(settingsPage);
    auto* rightClickAutoCopyCheck = new QCheckBox("Right-click auto-copy selected text", settingsPage);
    auto* copyPasteModeCombo = new QComboBox(settingsPage);
    auto* saveSettingsBtn = new QPushButton("Save Settings", settingsPage);

    QSettings startupSettings;
    guestBypassStartupCheck->setChecked(startupSettings.value("startup/guestBypass", false).toBool());
    historyLinesSlider->setValue(std::clamp(m_controller->sessions().config().terminal.standardHistoryLines, 1000, 10000));
    historyLinesValue->setText(QString::number(historyLinesSlider->value()) + " lines");
    rightClickAutoCopyCheck->setChecked(m_controller->sessions().config().terminal.rightClickAutoCopy);
    copyPasteModeCombo->addItem("Platform Default (macOS Cmd+C/V, Linux/Win Ctrl+Shift+C/V)", "platform");
    copyPasteModeCombo->addItem("Linux Style Ctrl+Shift+C / Ctrl+Shift+V", "ctrl_shift");
    copyPasteModeCombo->addItem("Ctrl+Alt+C / Ctrl+Alt+V", "ctrl_alt");
    {
        const auto mode = m_controller->sessions().config().terminal.copyPasteMode;
        const int idx = copyPasteModeCombo->findData(mode);
        copyPasteModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    settingsLayout->addWidget(new QLabel("Startup/Auth", settingsPage));
    settingsLayout->addWidget(guestSessionLabel);
    settingsLayout->addWidget(guestBypassStartupCheck);
    settingsLayout->addWidget(new QLabel("CLI History", settingsPage));
    settingsLayout->addWidget(new QLabel("History lines (up to 10,000)", settingsPage));
    settingsLayout->addWidget(historyLinesSlider);
    settingsLayout->addWidget(historyLinesValue);
    settingsLayout->addWidget(rightClickAutoCopyCheck);
    settingsLayout->addWidget(new QLabel("Keyboard copy/paste mode", settingsPage));
    settingsLayout->addWidget(copyPasteModeCombo);
    settingsLayout->addWidget(new QLabel("Note: history changes apply to newly opened terminal tabs.", settingsPage));
    settingsLayout->addWidget(saveSettingsBtn);
    settingsLayout->addStretch();

    m_cheatsheet = new QTextEdit(m_utilityTabs);
    m_cheatsheet->setReadOnly(true);
    m_cheatsheet->setPlainText(
        "Linux Bash / Grep / Regex Cheatsheet\n"
        "====================================\n"
        "\n"
        "Core Shell\n"
        "- ls -la\n"
        "- cd <dir>\n"
        "- pwd\n"
        "- tail -f file.log\n"
        "- chmod +x script.sh\n"
        "- ssh user@host -p 22\n"
        "\n"
        "CRONJob Syntax\n"
        "- m h dom mon dow command\n"
        "- Example: 0 2 * * * /usr/local/bin/backup.sh\n"
        "\n"
        "grep Essentials\n"
        "- grep 'pattern' file\n"
        "- grep -i 'pattern' file      # case-insensitive\n"
        "- grep -v 'pattern' file      # invert match\n"
        "- grep -n 'pattern' file      # show line numbers\n"
        "- grep -w 'word' file         # whole-word only\n"
        "- grep -c 'pattern' file      # count matches\n"
        "- grep -r 'pattern' path      # recursive search\n"
        "- grep -E 'a|b|c' file        # extended regex\n"
        "- grep -A 2 -B 2 'pattern' file  # context lines\n"
        "\n"
        "Regex Quickstart (Linux/Ruby/Grep)\n"
        "- Character classes: [abc] [A-Z] [0-9]\n"
        "- Shorthand classes: \\\\d \\\\w \\\\s and negatives \\\\D \\\\W \\\\S\n"
        "- Anchors: ^ start, $ end, \\\\b word-boundary\n"
        "- Quantifiers: * + ? {m,n} and lazy forms *? +? ??\n"
        "- Groups / alternation: (foo|bar)\n"
        "- Lookarounds: (?=...) (?!...) (?<=...) (?<!...)\n"
        "\n"
        "Ruby Grep Style\n"
        "- ruby -ne 'puts $_ if /error/i' app.log\n"
        "- ruby -ne 'print if /^(INFO|WARN|ERROR)/' app.log\n"
        "\n"
        "Regex Linux Examples\n"
        "- grep -E '^(INFO|WARN|ERROR)' app.log\n"
        "- grep -E '[0-9]{4}-[0-9]{2}-[0-9]{2}' app.log\n"
        "- rg -n '(fatal|panic|exception)' .\n"
        "- sed -E 's/[0-9]{4}-[0-9]{2}-[0-9]{2}/DATE/g' file\n"
        "\n"
        "Sources\n"
        "- https://www.rexegg.com/regex-quickstart.php\n"
        "- https://ryanstutorials.net/linuxtutorial/cheatsheetgrep.php\n"
    );

    m_proxyConfigs = new QTextEdit(m_utilityTabs);
    m_proxyConfigs->setReadOnly(true);
    m_proxyConfigs->setPlainText(
        "Proxy Auth Config Snippets\n"
        "==========================\n"
        "\n"
        "1) torrc (Tor using upstream authenticated SOCKS5 proxy)\n"
        "--------------------------------------------------------\n"
        "Socks5Proxy 127.0.0.1:1080\n"
        "Socks5ProxyUsername proxy_user\n"
        "Socks5ProxyPassword proxy_password\n"
        "\n"
        "# Optional HTTP CONNECT upstream\n"
        "# HTTPSProxy 127.0.0.1:8080\n"
        "# HTTPSProxyAuthenticator proxy_user:proxy_password\n"
        "\n"
        "2) SSH client config (~/.ssh/config) via authenticated proxy\n"
        "------------------------------------------------------------\n"
        "Host my-target\n"
        "  HostName example.internal\n"
        "  User ubuntu\n"
        "  Port 22\n"
        "  ProxyCommand connect-proxy -S 127.0.0.1:1080 -p proxy_user:proxy_password %h %p\n"
        "\n"
        "# HTTP CONNECT proxy variant\n"
        "# ProxyCommand connect-proxy -H 127.0.0.1:8080 -p proxy_user:proxy_password %h %p\n"
        "\n"
        "3) sshd_config (server hardening for proxy/tunnel usage)\n"
        "--------------------------------------------------------\n"
        "PubkeyAuthentication yes\n"
        "PasswordAuthentication no\n"
        "KbdInteractiveAuthentication no\n"
        "AuthenticationMethods publickey\n"
        "AllowTcpForwarding local\n"
        "PermitTunnel no\n"
        "X11Forwarding no\n"
        "PermitRootLogin no\n"
        "AllowUsers tunneluser\n"
        "\n"
        "# If you explicitly need reverse tunnels, change:\n"
        "# AllowTcpForwarding yes\n"
        "# GatewayPorts clientspecified\n"
        "\n"
        "4) Restart commands\n"
        "-------------------\n"
        "tor:   sudo systemctl restart tor\n"
        "sshd:  sudo systemctl restart sshd\n"
        "macOS sshd: sudo launchctl kickstart -k system/com.openssh.sshd\n"
    );

    auto* scriptSchedulerPanel = new QWidget(m_utilityTabs);
    auto* scriptSchedulerLayout = new QVBoxLayout(scriptSchedulerPanel);

    m_scriptsJson = new QPlainTextEdit(scriptSchedulerPanel);
    m_scriptsJson->setPlaceholderText("{\\n  \"NOONSSH\": \"echo hello\"\\n}");

    m_workflowsJson = new QPlainTextEdit(scriptSchedulerPanel);
    m_workflowsJson->setPlaceholderText(
        "[\\n"
        "  {\\n"
        "    \"name\": \"Noon Foxtail\",\\n"
        "    \"timeHHMM\": \"12:00\",\\n"
        "    \"profileQuery\": \"foxtail\",\\n"
        "    \"scriptName\": \"NOONSSH\",\\n"
        "    \"enabled\": true\\n"
        "  }\\n"
        "]"
    );

    m_runWorkflowName = new QLineEdit(scriptSchedulerPanel);
    m_runWorkflowName->setPlaceholderText("Workflow name to run now (optional)");

    auto* saveScriptsBtn = new QPushButton("Save Scripts JSON", scriptSchedulerPanel);
    auto* saveWorkflowsBtn = new QPushButton("Save Workflows JSON", scriptSchedulerPanel);
    auto* runNowBtn = new QPushButton("Run Workflow Now", scriptSchedulerPanel);

    auto* schedulerButtons = new QHBoxLayout();
    schedulerButtons->addWidget(saveScriptsBtn);
    schedulerButtons->addWidget(saveWorkflowsBtn);
    schedulerButtons->addWidget(runNowBtn);

    m_schedulerLog = new QTextEdit(scriptSchedulerPanel);
    m_schedulerLog->setReadOnly(true);

    scriptSchedulerLayout->addWidget(new QLabel("Scripts JSON (name -> script body)", scriptSchedulerPanel));
    scriptSchedulerLayout->addWidget(m_scriptsJson, 2);
    scriptSchedulerLayout->addWidget(new QLabel("Workflows JSON (daily HH:MM scheduler)", scriptSchedulerPanel));
    scriptSchedulerLayout->addWidget(m_workflowsJson, 2);
    scriptSchedulerLayout->addWidget(m_runWorkflowName);
    scriptSchedulerLayout->addLayout(schedulerButtons);
    scriptSchedulerLayout->addWidget(new QLabel("Scheduler Log", scriptSchedulerPanel));
    scriptSchedulerLayout->addWidget(m_schedulerLog, 1);

    m_notepad = new QPlainTextEdit(m_utilityTabs);

    auto* ollamaPanel = new QWidget(m_utilityTabs);
    auto* ollamaLayout = new QVBoxLayout(ollamaPanel);
    m_ollamaTarget = new QComboBox(ollamaPanel);
    m_ollamaTarget->addItems({"bash", "batch", "powershell", ".net"});
    m_ollamaPrompt = new QPlainTextEdit(ollamaPanel);
    m_ollamaPrompt->setPlaceholderText("Describe what you want generated...");
    m_ollamaOutput = new QPlainTextEdit(ollamaPanel);

    auto* genBtn = new QPushButton("Generate", ollamaPanel);
    auto* copyBtn = new QPushButton("Copy", ollamaPanel);
    auto* insertBtn = new QPushButton("Insert into active tab", ollamaPanel);
    auto* ollamaThinkingTimer = new QTimer(this);
    ollamaThinkingTimer->setInterval(450);
    ollamaThinkingTimer->setProperty("tick", 0);
    auto* row = new QHBoxLayout();
    row->addWidget(genBtn);
    row->addWidget(copyBtn);
    row->addWidget(insertBtn);

    ollamaLayout->addWidget(new QLabel("Target", ollamaPanel));
    ollamaLayout->addWidget(m_ollamaTarget);
    ollamaLayout->addWidget(new QLabel("Prompt", ollamaPanel));
    ollamaLayout->addWidget(m_ollamaPrompt);
    ollamaLayout->addWidget(new QLabel("Generated", ollamaPanel));
    ollamaLayout->addWidget(m_ollamaOutput);
    ollamaLayout->addLayout(row);

    m_utilityTabs->addTab(helpPage, "Help");
    m_utilityTabs->addTab(settingsPage, "Settings");
    m_utilityTabs->addTab(m_cheatsheet, "Bash Cheatsheet");
    m_utilityTabs->addTab(m_proxyConfigs, "Tor/SSH Config");
    m_utilityTabs->addTab(scriptSchedulerPanel, "Scripts/Scheduler");
    m_utilityTabs->addTab(m_notepad, "Empty Notepad");
    m_utilityTabs->addTab(ollamaPanel, "Local OLLAMA");

    rightLayout->addLayout(quickRow);
    rightLayout->addWidget(m_terminalTabs, 2);
    rightLayout->addWidget(m_utilityTabs, 1);

    m_mainSplitter->addWidget(leftPane);
    m_mainSplitter->addWidget(rightPane);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setSizes({180, 1220});

    setCentralWidget(m_mainSplitter);

    auto* fileMenu = menuBar()->addMenu("File");
    auto* importAction = fileMenu->addAction("Import JSON...");
    auto* exportAction = fileMenu->addAction("Export JSON...");
    auto* saveAction = fileMenu->addAction("Save");
    auto* proxyAction = fileMenu->addAction("Manage Proxies...");
    auto* secretsAction = fileMenu->addAction("Manage Secrets...");
    auto* helpMenu = menuBar()->addMenu("Help");
    auto* helpGuideAction = helpMenu->addAction("Help Guide");
    auto* aboutAction = helpMenu->addAction("About 3TTY");

    connect(saveAction, &QAction::triggered, this, [this]() {
        QString err;
        if (!m_controller->save(&err)) {
            QMessageBox::critical(this, "Save failed", err);
        }
    });
    connect(proxyAction, &QAction::triggered, this, [this]() {
        ProxyEditorDialog dlg(m_controller->sessions().config().proxies, this);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        bool ok = false;
        QString err;
        const auto proxies = dlg.proxies(&ok, &err);
        if (!ok) {
            QMessageBox::critical(this, "Invalid proxy JSON", err);
            return;
        }
        m_controller->sessions().config().proxies = proxies;
    });
    connect(secretsAction, &QAction::triggered, this, [this]() {
        SecretsEditorDialog dlg(m_controller->sessions().config().secrets, this);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        bool ok = false;
        QString err;
        const auto secrets = dlg.secrets(&ok, &err);
        if (!ok) {
            QMessageBox::critical(this, "Invalid secrets JSON", err);
            return;
        }
        m_controller->sessions().config().secrets = secrets;
    });
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About 3Char-TTY // 3TTY",
            "3Char-TTY // 3TTY\n"
            "For SysAdmin Tooling\n"
            "Cross-platform terminal, SSH workflow scheduler, and scripting console.\n\n"
            "By: 3xC\n"
            "Contact: 3xcypher@pm.me\n"
            "Etc."
        );
    });
    connect(helpGuideAction, &QAction::triggered, this, [this, helpPage]() {
        m_utilityTabs->setCurrentWidget(helpPage);
    });
    connect(historyLinesSlider, &QSlider::valueChanged, this, [historyLinesValue](int value) {
        historyLinesValue->setText(QString::number(value) + " lines");
    });
    connect(saveSettingsBtn, &QPushButton::clicked, this, [this, guestBypassStartupCheck, rightClickAutoCopyCheck, copyPasteModeCombo, historyLinesSlider]() {
        QSettings startupSettings;
        startupSettings.setValue("startup/guestBypass", guestBypassStartupCheck->isChecked());

        auto& terminalSettings = m_controller->sessions().config().terminal;
        terminalSettings.expandedHistoryEnabled = false;
        terminalSettings.rightClickAutoCopy = rightClickAutoCopyCheck->isChecked();
        terminalSettings.copyPasteMode = copyPasteModeCombo->currentData().toString();
        terminalSettings.standardHistoryLines = std::clamp(historyLinesSlider->value(), 1000, 10000);
        QString err;
        if (!m_controller->save(&err)) {
            QMessageBox::critical(this, "Save failed", err);
            return;
        }
        QMessageBox::information(
            this,
            "Settings Saved",
            QString("History lines set to %1. Applies to newly opened terminal tabs.")
                .arg(terminalSettings.standardHistoryLines)
        );
    });

    connect(importAction, &QAction::triggered, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, "Import JSON", QString(), "JSON (*.json)");
        if (path.isEmpty()) return;

        bool ok = false;
        QString err;
        ConfigStore cs;
        auto imported = cs.importPlainJson(path, &ok, &err);
        if (!ok) {
            QMessageBox::critical(this, "Import failed", err);
            return;
        }
        m_controller->sessions().config() = imported;
        refreshProfileTree(m_filter->text());
    });

    connect(exportAction, &QAction::triggered, this, [this]() {
        const auto path = QFileDialog::getSaveFileName(this, "Export JSON", "profiles.json", "JSON (*.json)");
        if (path.isEmpty()) return;

        QString err;
        ConfigStore cs;
        if (!cs.exportPlainJson(m_controller->sessions().config(), path, &err)) {
            QMessageBox::critical(this, "Export failed", err);
        }
    });

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        refreshProfileTree(text);
    });

    connect(openBtn, &QPushButton::clicked, this, [this]() {
        const auto items = m_profileTree->selectedItems();
        if (items.isEmpty()) return;
        const auto id = items.first()->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) openProfile(id);
    });
    connect(openQuadBtn, &QPushButton::clicked, this, [this]() {
        openMultiModeFromSelectedProfiles();
    });

    connect(quickOpen, &QPushButton::clicked, this, [this, quickId]() {
        const auto id = quickId->text().trimmed();
        if (!id.isEmpty()) openProfile(id);
    });

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        ProfileEditorDialog dlg(this);
        SessionProfile p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.type = "local";
        p.authMode = "password";
        dlg.setProfile(p);
        if (dlg.exec() == QDialog::Accepted) {
            m_controller->sessions().upsertProfile(dlg.profile());
            refreshProfileTree(m_filter->text());
        }
    });

    connect(editBtn, &QPushButton::clicked, this, [this]() {
        const auto items = m_profileTree->selectedItems();
        if (items.isEmpty()) return;
        const auto id = items.first()->data(0, Qt::UserRole).toString();
        auto p = m_controller->sessions().profileById(id);
        if (p.id.isEmpty()) return;

        ProfileEditorDialog dlg(this);
        dlg.setProfile(p);
        if (dlg.exec() == QDialog::Accepted) {
            m_controller->sessions().upsertProfile(dlg.profile());
            refreshProfileTree(m_filter->text());
        }
    });

    connect(delBtn, &QPushButton::clicked, this, [this]() {
        const auto items = m_profileTree->selectedItems();
        if (items.isEmpty()) return;
        const auto id = items.first()->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return;
        m_controller->sessions().removeProfile(id);
        refreshProfileTree(m_filter->text());
    });

    connect(ollamaThinkingTimer, &QTimer::timeout, this, [this, ollamaThinkingTimer]() {
        const int tick = (ollamaThinkingTimer->property("tick").toInt() + 1) % 3;
        ollamaThinkingTimer->setProperty("tick", tick);
        m_ollamaOutput->setPlainText(QString("LLAMA is thinking%1").arg(QString(".").repeated(tick + 1)));
    });

    connect(genBtn, &QPushButton::clicked, this, [this, genBtn, ollamaThinkingTimer]() {
        QString prompt = m_ollamaPrompt->toPlainText().trimmed();
        if (prompt.isEmpty()) {
            m_ollamaOutput->setPlainText("Enter a prompt to generate output.");
            return;
        }
        prompt = QString("Generate %1 code only. Task: %2").arg(m_ollamaTarget->currentText(), prompt);
        const auto& cfg = m_controller->sessions().config().ollama;
        genBtn->setEnabled(false);
        ollamaThinkingTimer->setProperty("tick", 0);
        m_ollamaOutput->setPlainText("LLAMA is thinking.");
        ollamaThinkingTimer->start();
        m_ollamaClient->generate(QUrl(cfg.endpoint), cfg.model, prompt);
    });

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_ollamaOutput->toPlainText());
    });

    connect(insertBtn, &QPushButton::clicked, this, [this]() {
        auto* tab = qobject_cast<TerminalTab*>(m_terminalTabs->currentWidget());
        if (!tab) return;
        QApplication::clipboard()->setText(m_ollamaOutput->toPlainText());
    });

    connect(m_ollamaClient, &OllamaClient::generated, this, [this, genBtn, ollamaThinkingTimer](const QString& out) {
        ollamaThinkingTimer->stop();
        genBtn->setEnabled(true);
        m_ollamaOutput->setPlainText(out);
    });

    connect(m_ollamaClient, &OllamaClient::failed, this, [this, genBtn, ollamaThinkingTimer](const QString& err) {
        ollamaThinkingTimer->stop();
        genBtn->setEnabled(true);
        m_ollamaOutput->setPlainText(QString("Generation failed: %1").arg(err));
        QMessageBox::warning(this, "OLLAMA", err);
    });

    connect(saveScriptsBtn, &QPushButton::clicked, this, [this]() {
        loadScriptSchedulerFromEditors();
        refreshScriptSchedulerEditors();
    });
    connect(saveWorkflowsBtn, &QPushButton::clicked, this, [this]() {
        loadScriptSchedulerFromEditors();
        refreshScriptSchedulerEditors();
    });
    connect(runNowBtn, &QPushButton::clicked, this, [this]() {
        loadScriptSchedulerFromEditors();
        const auto requested = m_runWorkflowName->text().trimmed();
        if (!requested.isEmpty()) {
            runWorkflowByName(requested, true);
            return;
        }
        if (!m_controller->sessions().config().workflows.isEmpty()) {
            runWorkflowIndex(0, true);
        }
    });

    if (!m_controller->sessions().config().scripts.contains("NOONSSH")) {
        m_controller->sessions().config().scripts["NOONSSH"] = "echo \"NOONSSH start\"\\nhostname\\ndate\\necho \"NOONSSH done\"";
    }
    bool hasNoonWorkflow = false;
    for (const auto& w : m_controller->sessions().config().workflows) {
        if (w.name.compare("Noon Foxtail", Qt::CaseInsensitive) == 0) {
            hasNoonWorkflow = true;
            break;
        }
    }
    if (!hasNoonWorkflow) {
        ScheduledWorkflow wf;
        wf.name = "Noon Foxtail";
        wf.timeHHMM = "12:00";
        wf.profileQuery = "foxtail";
        wf.scriptName = "NOONSSH";
        wf.enabled = true;
        m_controller->sessions().config().workflows.push_back(wf);
    }
    refreshScriptSchedulerEditors();

    connect(m_schedulerTimer, &QTimer::timeout, this, [this]() {
        schedulerTick();
    });
    m_schedulerTimer->start(30000);
    schedulerTick();

#ifdef Q_OS_WIN
    if (m_controller->sessions().config().profiles.isEmpty()) {
        SessionProfile msys;
        msys.id = "windows-msys2-default";
        msys.name = "MSYS2 Bash";
        msys.type = "local";
        msys.shellCommand = "C:/msys64/usr/bin/bash.exe";
        m_controller->sessions().upsertProfile(msys);
        refreshProfileTree();
    }
#endif
}

void MainWindow::refreshProfileTree(const QString& filter) {
    m_profileTree->clear();

    QMap<QString, QTreeWidgetItem*> groups;
    const auto f = filter.trimmed().toLower();

    for (const auto& p : m_controller->sessions().config().profiles) {
        const auto haystack = QString("%1 %2 %3").arg(p.name, p.groupPath, p.tags.join(" ")).toLower();
        if (!f.isEmpty() && !haystack.contains(f)) {
            continue;
        }

        const auto group = p.groupPath.isEmpty() ? "Ungrouped" : p.groupPath;
        if (!groups.contains(group)) {
            auto* item = new QTreeWidgetItem({group});
            groups[group] = item;
            m_profileTree->addTopLevelItem(item);
        }

        auto* leaf = new QTreeWidgetItem({p.name.isEmpty() ? p.id : p.name});
        leaf->setData(0, Qt::UserRole, p.id);
        leaf->setToolTip(0, QString("%1\n%2").arg(p.id, p.tags.join(", ")));
        groups[group]->addChild(leaf);
    }

    m_profileTree->expandAll();
}

QList<QString> MainWindow::selectedProfileIds() const {
    QList<QString> ids;
    const auto items = m_profileTree->selectedItems();
    for (const auto* item : items) {
        const auto id = item->data(0, Qt::UserRole).toString().trimmed();
        if (id.isEmpty()) {
            continue;
        }
        if (!ids.contains(id)) {
            ids.push_back(id);
        }
    }
    return ids;
}

void MainWindow::openMultiModeFromSelectedProfiles() {
    const auto ids = selectedProfileIds();
    if (ids.size() < 2) {
        QMessageBox::information(this, "MultiMode", "Select at least 2 profiles, then click MultiMode.");
        return;
    }

    QList<SessionProfile> profiles;
    for (const auto& id : ids) {
        auto p = m_controller->sessions().profileById(id);
        if (p.id.isEmpty()) {
            QMessageBox::warning(this, "MultiMode", QString("Profile not found: %1").arg(id));
            return;
        }
        profiles.push_back(p);
    }

    auto* multi = new QuadTerminalView(profiles, m_controller->sessions().config(), m_terminalTabs);
    multi->setProperty("multimode", true);
    QStringList names;
    for (const auto& p : profiles) {
        names << (p.name.isEmpty() ? p.id : p.name);
    }
    const auto label = QString("MultiMode (%1) | %2").arg(profiles.size()).arg(names.join(" | "));
    m_terminalTabs->addTab(multi, label);
    m_terminalTabs->setCurrentWidget(multi);
    applyWorkspaceDensity(true);
}

void MainWindow::applyWorkspaceDensity(bool multiMode) {
    if (!m_mainSplitter || !m_utilityTabs) {
        return;
    }

    const auto sizes = m_mainSplitter->sizes();
    const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    if (total <= 0) {
        return;
    }

    if (multiMode) {
        const int left = std::max(120, total / 10);
        m_mainSplitter->setSizes({left, std::max(200, total - left)});
        m_utilityTabs->setMaximumHeight(std::max(100, height() / 8));
    } else {
        const int left = std::max(200, total / 4);
        m_mainSplitter->setSizes({left, std::max(300, total - left)});
        m_utilityTabs->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}


void MainWindow::openProfile(const QString& profileQuery) {
    QString err;
    SessionProfile p = resolveProfile(profileQuery, &err);
    if (p.id.isEmpty()) {
        QMessageBox::warning(this, "Profile lookup", err.isEmpty() ? "No profile found for that id or name." : err);
        return;
    }

    auto* tab = new TerminalTab(p, m_controller->sessions().config(), m_terminalTabs);
    tab->setProperty("multimode", false);
    m_terminalTabs->addTab(tab, tab->title());
    m_terminalTabs->setCurrentWidget(tab);
    applyWorkspaceDensity(false);
}

SessionProfile MainWindow::resolveProfile(const QString& profileQuery, QString* error) const {
    SessionProfile p = m_controller->sessions().profileById(profileQuery);
    if (!p.id.isEmpty()) {
        return p;
    }

    QList<SessionProfile> exactNameMatches;
    QList<SessionProfile> partialNameMatches;
    const auto query = profileQuery.trimmed();
    for (const auto& candidate : m_controller->sessions().config().profiles) {
        if (candidate.name.compare(query, Qt::CaseInsensitive) == 0) {
            exactNameMatches.push_back(candidate);
        } else if (candidate.name.contains(query, Qt::CaseInsensitive)) {
            partialNameMatches.push_back(candidate);
        }
    }

    if (exactNameMatches.size() == 1) {
        return exactNameMatches.first();
    }
    if (exactNameMatches.size() > 1) {
        if (error) *error = "Multiple profiles have this name. Use profile id.";
        return {};
    }
    if (partialNameMatches.size() == 1) {
        return partialNameMatches.first();
    }
    if (partialNameMatches.size() > 1) {
        if (error) *error = "Multiple profiles match this name. Use full name or profile id.";
        return {};
    }
    if (error) *error = "No profile found for that id or name.";
    return {};
}

QString MainWindow::shellSingleQuote(const QString& input) const {
    QString out = input;
    out.replace("'", "'\"'\"'");
    return QString("'%1'").arg(out);
}

QString MainWindow::proxyCommandForProfile(const SessionProfile& profile) const {
    if (profile.proxyRef.isEmpty()) {
        return {};
    }
    ProxyProfile proxy;
    bool found = false;
    for (const auto& p : m_controller->sessions().config().proxies) {
        if (p.id == profile.proxyRef) {
            proxy = p;
            found = true;
            break;
        }
    }
    if (!found) {
        return {};
    }
    const auto secret = m_controller->sessions().config().secrets.value(proxy.secretRef);
    if (proxy.type == "socks5") {
        if (!proxy.username.isEmpty() || !secret.isEmpty()) {
            return QString("connect-proxy -5 -S %1:%2 -p %3:%4 %%h %%p")
                .arg(proxy.host).arg(proxy.port).arg(proxy.username, secret);
        }
        return QString("nc -X 5 -x %1:%2 %%h %%p").arg(proxy.host).arg(proxy.port);
    }
    if (proxy.type == "http_connect") {
        if (!proxy.username.isEmpty() || !secret.isEmpty()) {
            return QString("connect-proxy -H %1:%2 -p %3:%4 %%h %%p")
                .arg(proxy.host).arg(proxy.port).arg(proxy.username, secret);
        }
        return QString("nc -X connect -x %1:%2 %%h %%p").arg(proxy.host).arg(proxy.port);
    }
    return {};
}

QString MainWindow::workflowLogDir() const {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/workflow-logs";
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::refreshScriptSchedulerEditors() {
    QJsonObject scriptsObj;
    for (auto it = m_controller->sessions().config().scripts.cbegin(); it != m_controller->sessions().config().scripts.cend(); ++it) {
        scriptsObj.insert(it.key(), it.value());
    }
    m_scriptsJson->setPlainText(QString::fromUtf8(QJsonDocument(scriptsObj).toJson(QJsonDocument::Indented)));

    QJsonArray workflowsArr;
    for (const auto& w : m_controller->sessions().config().workflows) {
        workflowsArr.append(w.toJson());
    }
    m_workflowsJson->setPlainText(QString::fromUtf8(QJsonDocument(workflowsArr).toJson(QJsonDocument::Indented)));
}

void MainWindow::loadScriptSchedulerFromEditors() {
    {
        const auto doc = QJsonDocument::fromJson(m_scriptsJson->toPlainText().toUtf8());
        if (doc.isObject()) {
            QMap<QString, QString> scripts;
            for (auto it = doc.object().begin(); it != doc.object().end(); ++it) {
                scripts[it.key()] = it.value().toString();
            }
            m_controller->sessions().config().scripts = scripts;
        } else {
            appendSchedulerLog("scripts JSON ignored: expected object");
        }
    }
    {
        const auto doc = QJsonDocument::fromJson(m_workflowsJson->toPlainText().toUtf8());
        if (doc.isArray()) {
            QList<ScheduledWorkflow> workflows;
            for (const auto& it : doc.array()) {
                if (it.isObject()) {
                    workflows.push_back(ScheduledWorkflow::fromJson(it.toObject()));
                }
            }
            m_controller->sessions().config().workflows = workflows;
        } else {
            appendSchedulerLog("workflows JSON ignored: expected array");
        }
    }
}

void MainWindow::appendSchedulerLog(const QString& line) {
    const auto ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_schedulerLog->append(QString("[%1] %2").arg(ts, line));
}

void MainWindow::runWorkflowByName(const QString& workflowName, bool manual) {
    for (int i = 0; i < m_controller->sessions().config().workflows.size(); ++i) {
        if (m_controller->sessions().config().workflows[i].name.compare(workflowName, Qt::CaseInsensitive) == 0) {
            runWorkflowIndex(i, manual);
            return;
        }
    }
    appendSchedulerLog(QString("workflow not found: %1").arg(workflowName));
}

void MainWindow::runWorkflowIndex(int index, bool manual) {
    if (index < 0 || index >= m_controller->sessions().config().workflows.size()) {
        return;
    }
    auto& w = m_controller->sessions().config().workflows[index];
    if (!w.enabled && !manual) {
        return;
    }

    QString resolveError;
    const auto profile = resolveProfile(w.profileQuery, &resolveError);
    if (profile.id.isEmpty()) {
        appendSchedulerLog(QString("%1 skipped: %2").arg(w.name, resolveError));
        return;
    }

    const auto script = m_controller->sessions().config().scripts.value(w.scriptName);
    if (script.isEmpty()) {
        appendSchedulerLog(QString("%1 skipped: script '%2' not found").arg(w.name, w.scriptName));
        return;
    }

    const auto workflowName = w.name;
    const auto ts = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const auto safeWorkflow = w.name.isEmpty() ? QString("workflow-%1").arg(index) : w.name;
    const auto logPath = QString("%1/%2-%3.log").arg(workflowLogDir(), safeWorkflow, ts);

    QString program;
    QStringList args;
    if (profile.type == "ssh") {
        const auto proxyCmd = proxyCommandForProfile(profile);
        const auto appConfigSnapshot = m_controller->sessions().config();
        const auto today = QDate::currentDate().toString(Qt::ISODate);

#ifdef HAS_LIBSSH
        if (profile.authMode == "password" && m_controller->sessions().config().secrets.value(profile.id + ".password").isEmpty()) {
            appendSchedulerLog(QString("%1 skipped: missing secret '%2.password'").arg(w.name, profile.id));
            return;
        }

        appendSchedulerLog(QString("%1 starting on profile '%2' using script '%3' (native libssh)")
                               .arg(workflowName, profile.name.isEmpty() ? profile.id : profile.name, w.scriptName));
        QPointer<MainWindow> self(this);
        std::thread([self, profile, appConfigSnapshot, proxyCmd, script, logPath, workflowName, index, today]() {
            const auto result = runNativeSshScript(profile, appConfigSnapshot, proxyCmd, script, logPath);
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self, [self, result, workflowName, logPath, index, today]() {
                if (!self) return;
                if (!result.hostMessage.isEmpty()) {
                    self->appendSchedulerLog(QString("%1 host key: %2").arg(workflowName, result.hostMessage));
                }
                if (!result.ok) {
                    self->appendSchedulerLog(QString("%1 failed: %2").arg(workflowName, result.error));
                    return;
                }
                self->appendSchedulerLog(QString("%1 finished with code %2. log: %3").arg(workflowName).arg(result.exitCode).arg(logPath));
                if (index >= 0 && index < self->m_controller->sessions().config().workflows.size()) {
                    self->m_controller->sessions().config().workflows[index].lastRunDate = today;
                }
                QString err;
                (void)self->m_controller->save(&err);
                self->refreshScriptSchedulerEditors();
            }, Qt::QueuedConnection);
        }).detach();
        return;
#else
        appendSchedulerLog(QString("%1 skipped: libssh not enabled in this build").arg(w.name));
        return;
#endif
    } else {
#ifdef Q_OS_WIN
        program = "powershell.exe";
        args << "-NoProfile" << "-Command" << script;
#else
        program = "/bin/bash";
        args << "-lc" << script;
#endif
    }

    auto* proc = new QProcess(this);
    auto* outFile = new QFile(logPath, proc);
    if (!outFile->open(QIODevice::WriteOnly)) {
        appendSchedulerLog(QString("%1 failed: cannot open log file").arg(w.name));
        proc->deleteLater();
        return;
    }

    appendSchedulerLog(QString("%1 starting on profile '%2' using script '%3'").arg(workflowName, profile.name.isEmpty() ? profile.id : profile.name, w.scriptName));
    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, outFile]() {
        outFile->write(proc->readAllStandardOutput());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [proc, outFile]() {
        outFile->write(proc->readAllStandardError());
    });
    auto completion = std::make_shared<bool>(false);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, outFile, proc, logPath, workflowName, index, completion](int exitCode, QProcess::ExitStatus) {
        if (*completion) {
            return;
        }
        *completion = true;
        outFile->flush();
        outFile->close();
        appendSchedulerLog(QString("%1 finished with code %2. log: %3").arg(workflowName).arg(exitCode).arg(logPath));
        if (index >= 0 && index < m_controller->sessions().config().workflows.size()) {
            m_controller->sessions().config().workflows[index].lastRunDate = QDate::currentDate().toString(Qt::ISODate);
        }
        QString err;
        (void)m_controller->save(&err);
        refreshScriptSchedulerEditors();
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, outFile, proc, workflowName, program, completion](QProcess::ProcessError error) {
        if (*completion) {
            return;
        }
        *completion = true;
        outFile->flush();
        outFile->close();
        appendSchedulerLog(
            QString("%1 failed to start '%2' (process error %3)")
                .arg(workflowName, program)
                .arg(static_cast<int>(error)));
        proc->deleteLater();
    });
    proc->start(program, args);
}

void MainWindow::schedulerTick() {
    const auto now = QTime::currentTime().toString("HH:mm");
    const auto today = QDate::currentDate().toString(Qt::ISODate);
    for (int i = 0; i < m_controller->sessions().config().workflows.size(); ++i) {
        const auto& w = m_controller->sessions().config().workflows[i];
        if (!w.enabled) {
            continue;
        }
        if (w.timeHHMM == now && w.lastRunDate != today) {
            runWorkflowIndex(i, false);
        }
    }
}

void MainWindow::loadNotepad() {
    if (m_controller->isGuestMode()) {
        m_notepad->setPlainText("Guest Mode: notepad persistence is disabled for this session.");
        return;
    }
    QFile file(m_controller->notepadPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_notepad->setPlainText(QString::fromUtf8(file.readAll()));
}

void MainWindow::saveNotepad() {
    if (m_controller->isGuestMode()) {
        return;
    }
    QFile file(m_controller->notepadPath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(m_notepad->toPlainText().toUtf8());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveNotepad();
    QString err;
    if (!m_controller->save(&err)) {
        QMessageBox::critical(this, "Save failed", err);
    }
    QMainWindow::closeEvent(event);
}
