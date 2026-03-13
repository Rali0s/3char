#pragma once

#include "core/AppController.hpp"

#include <QMainWindow>

class QLineEdit;
class QTabWidget;
class QTreeWidget;
class QTextEdit;
class QPlainTextEdit;
class QComboBox;
class QTimer;
class QSplitter;
class OllamaClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void refreshProfileTree(const QString& filter = {});
    void openProfile(const QString& profileQuery);
    void openMultiModeFromSelectedProfiles();
    void applyWorkspaceDensity(bool multiMode);
    QList<QString> selectedProfileIds() const;
    SessionProfile resolveProfile(const QString& profileQuery, QString* error = nullptr) const;
    QString shellSingleQuote(const QString& input) const;
    QString proxyCommandForProfile(const SessionProfile& profile) const;
    QString workflowLogDir() const;
    void refreshScriptSchedulerEditors();
    void loadScriptSchedulerFromEditors();
    void schedulerTick();
    void runWorkflowByName(const QString& workflowName, bool manual);
    void runWorkflowIndex(int index, bool manual);
    void appendSchedulerLog(const QString& line);
    void loadNotepad();
    void saveNotepad();

    AppController* m_controller;
    QTreeWidget* m_profileTree;
    QLineEdit* m_filter;
    QSplitter* m_mainSplitter;
    QTabWidget* m_terminalTabs;
    QTabWidget* m_utilityTabs;

    QTextEdit* m_cheatsheet;
    QTextEdit* m_proxyConfigs;
    QPlainTextEdit* m_scriptsJson;
    QPlainTextEdit* m_workflowsJson;
    QTextEdit* m_schedulerLog;
    QLineEdit* m_runWorkflowName;
    QPlainTextEdit* m_notepad;
    QPlainTextEdit* m_ollamaPrompt;
    QPlainTextEdit* m_ollamaOutput;
    QComboBox* m_ollamaTarget;
    QTimer* m_schedulerTimer;

    OllamaClient* m_ollamaClient;
};
