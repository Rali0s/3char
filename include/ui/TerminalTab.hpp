#pragma once

#include "data/Models.hpp"

#include <QByteArray>
#include <QtGlobal>
#include <QWidget>

class QPushButton;
class QWebEngineView;
class QWebChannel;
class QTimer;
class SessionBackend;
class TerminalBridge;

class TerminalTab : public QWidget {
    Q_OBJECT
public:
    explicit TerminalTab(const SessionProfile& profile, const AppConfig& appConfig, QWidget* parent = nullptr);
    ~TerminalTab() override;

    QString title() const;

private:
    void appendToTerminal(const QByteArray& bytes);
    void flushPendingOutput();

    SessionProfile m_profile;
    AppConfig m_appConfig;
    int m_historyLineLimit = 1000;
    bool m_pageLoaded = false;
    bool m_backendStarted = false;
    QByteArray m_pendingOutput;
    QWebEngineView* m_terminal;
    QWebChannel* m_channel;
    TerminalBridge* m_bridge;
    QPushButton* m_copyButton;
    QPushButton* m_pasteButton;
    QTimer* m_flushTimer;
    SessionBackend* m_backend;
};
