#include "ui/TerminalTab.hpp"

#include "session/PtySession.hpp"
#include "session/SessionBackend.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <algorithm>

class TerminalBridge : public QObject {
    Q_OBJECT
public:
    explicit TerminalBridge(QObject* parent = nullptr)
        : QObject(parent) {}

signals:
    void inputReceived(const QByteArray& bytes);
    void resizeReceived(int cols, int rows);
    void copySelectionRequested(const QString& text);
    void pasteRequested();

public slots:
    void sendInput(const QString& data) {
        emit inputReceived(data.toUtf8());
    }

    void reportResize(int cols, int rows) {
        emit resizeReceived(cols, rows);
    }

    void copySelection(const QString& text) {
        emit copySelectionRequested(text);
    }

    void requestPaste() {
        emit pasteRequested();
    }
};

TerminalTab::TerminalTab(const SessionProfile& profile, const AppConfig& appConfig, QWidget* parent)
    : QWidget(parent),
      m_profile(profile),
      m_appConfig(appConfig),
      m_terminal(new QWebEngineView(this)),
      m_channel(new QWebChannel(this)),
      m_bridge(new TerminalBridge(this)),
      m_copyButton(new QPushButton("Copy", this)),
      m_pasteButton(new QPushButton("Paste", this)),
      m_flushTimer(new QTimer(this)),
      m_backend(new PtySession(this)) {
    m_historyLineLimit = std::clamp(m_appConfig.terminal.standardHistoryLines, 1000, 10000);
    m_flushTimer->setInterval(16);
    m_flushTimer->setSingleShot(false);

    auto* actions = new QHBoxLayout();
    actions->addWidget(m_copyButton);
    actions->addWidget(m_pasteButton);
    actions->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(actions);
    layout->addWidget(m_terminal, 1);
    m_terminal->setContextMenuPolicy(Qt::NoContextMenu);

    m_terminal->page()->setWebChannel(m_channel);
    m_channel->registerObject("bridge", m_bridge);

    const auto copyMode = m_appConfig.terminal.copyPasteMode.isEmpty()
                              ? QString("platform")
                              : m_appConfig.terminal.copyPasteMode;
    const auto rightClickAutoCopy = m_appConfig.terminal.rightClickAutoCopy;

    QString html = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'/>
  <link rel='stylesheet' href='qrc:///assets/xterm/xterm.css' />
  <style>
    html, body, #term { height: 100%; margin: 0; background: #111; }
    .xterm-viewport { overflow-y: auto !important; }
    #fallback {
      display: none;
      margin: 0;
      box-sizing: border-box;
      height: 100%;
      overflow: auto;
      padding: 8px;
      color: #ddd;
      font: 12px/1.35 monospace;
      white-space: pre-wrap;
    }
  </style>
</head>
<body>
  <div id='term'></div>
  <pre id='fallback'></pre>
  <script>
    let term = null;
    let fitAddon = null;
    let bridge = null;
    let bridgeBound = false;
    let fallbackBuffer = '';

    const fallback = document.getElementById('fallback');
    const appendFallback = function(text) {
      if (!fallback) return;
      fallback.style.display = 'block';
      fallbackBuffer += text;
      if (fallbackBuffer.length > 200000) {
        fallbackBuffer = fallbackBuffer.slice(fallbackBuffer.length - 200000);
      }
      fallback.textContent = fallbackBuffer;
      fallback.scrollTop = fallback.scrollHeight;
    };

    window.appendTerminalData = function(base64Payload) {
      let text = '';
      try {
        text = atob(base64Payload);
      } catch (e) {
        return;
      }
      if (term && typeof term.write === 'function') {
        term.write(text);
        return;
      }
      appendFallback(text);
    };

    window.getSelectionText = function() {
      if (term && typeof term.getSelection === 'function') {
        return term.getSelection();
      }
      return '';
    };
  </script>
  <script src='qrc:///qtwebchannel/qwebchannel.js'></script>
  <script src='qrc:///assets/xterm/xterm.js'></script>
  <script src='qrc:///assets/xterm/xterm-addon-fit.js'></script>
  <script>
    const copyMode = "__COPY_MODE__";
    const rightClickAutoCopy = __RIGHT_CLICK_AUTO_COPY__;

    const bindBridgeToTerminal = function() {
      if (!bridge || !term || bridgeBound) {
        return;
      }
      bridgeBound = true;
      term.onData(function(data) { bridge.sendInput(data); });
      bridge.reportResize(term.cols, term.rows);
    };

    const startTerminal = function() {
      if (typeof Terminal !== 'function' || !window.FitAddon || typeof FitAddon.FitAddon !== 'function') {
        appendFallback("[3TTY] xterm resources unavailable. Showing output-only fallback.\n");
        return;
      }

      term = new Terminal({cursorBlink: true, scrollback: __SCROLLBACK__, allowProposedApi: true});
      fitAddon = new FitAddon.FitAddon();
      term.loadAddon(fitAddon);
      term.open(document.getElementById('term'));
      fitAddon.fit();
      bindBridgeToTerminal();

      term.attachCustomKeyEventHandler(function(ev) {
        if (!bridge || ev.type !== 'keydown') {
          return true;
        }
        const key = (ev.key || '').toLowerCase();
        const ctrl = !!ev.ctrlKey;
        const shift = !!ev.shiftKey;
        const alt = !!ev.altKey;
        const meta = !!ev.metaKey;

        const doCopy = function() {
          bridge.copySelection(term.getSelection());
          return false;
        };
        const doPaste = function() {
          bridge.requestPaste();
          return false;
        };

        if (copyMode === 'ctrl_shift') {
          if (ctrl && shift && !alt && !meta && key === 'c') return doCopy();
          if (ctrl && shift && !alt && !meta && key === 'v') return doPaste();
          return true;
        }
        if (copyMode === 'ctrl_alt') {
          if (ctrl && alt && !shift && !meta && key === 'c') return doCopy();
          if (ctrl && alt && !shift && !meta && key === 'v') return doPaste();
          return true;
        }

        // platform default:
        // - macOS: Cmd+C / Cmd+V
        // - Linux/Windows: Ctrl+Shift+C / Ctrl+Shift+V
        if (meta && !ctrl && !alt && !shift && key === 'c') return doCopy();
        if (meta && !ctrl && !alt && !shift && key === 'v') return doPaste();
        if (ctrl && shift && !alt && !meta && key === 'c') return doCopy();
        if (ctrl && shift && !alt && !meta && key === 'v') return doPaste();
        return true;
      });

      if (rightClickAutoCopy && term.element) {
        term.element.addEventListener('contextmenu', function(ev) {
          ev.preventDefault();
          if (!bridge) return;
          const selected = term.getSelection();
          if (selected && selected.length > 0) {
            bridge.copySelection(selected);
          }
        });
      }

      window.addEventListener('resize', function() {
        if (fitAddon) {
          fitAddon.fit();
        }
        if (bridge && term) {
          bridge.reportResize(term.cols, term.rows);
        }
      });
    };

    if (window.qt && qt.webChannelTransport && window.QWebChannel) {
      new QWebChannel(qt.webChannelTransport, function(channel) {
        bridge = channel.objects.bridge;
        bindBridgeToTerminal();
      });
    } else {
      appendFallback("[3TTY] WebChannel unavailable.\n");
    }

    startTerminal();
  </script>
</body>
</html>
)HTML";
    html.replace("__SCROLLBACK__", QString::number(m_historyLineLimit));
    html.replace("__COPY_MODE__", copyMode);
    html.replace("__RIGHT_CLICK_AUTO_COPY__", rightClickAutoCopy ? "true" : "false");
    m_terminal->setHtml(html, QUrl("https://localhost/"));

    connect(m_flushTimer, &QTimer::timeout, this, &TerminalTab::flushPendingOutput);
    connect(m_terminal, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_pageLoaded = ok;
        if (!ok) {
            appendToTerminal("\r\n[error] Terminal renderer failed to load.\r\n");
        }
        if (!m_backendStarted) {
            m_backendStarted = true;
            m_backend->start(m_profile, m_appConfig);
        }
        if (!m_pendingOutput.isEmpty() && !m_flushTimer->isActive()) {
            m_flushTimer->start();
        }
        flushPendingOutput();
    });

    connect(m_bridge, &TerminalBridge::inputReceived, m_backend, &SessionBackend::sendInput);
    connect(m_bridge, &TerminalBridge::resizeReceived, m_backend, &SessionBackend::resize);
    connect(m_bridge, &TerminalBridge::copySelectionRequested, this, [](const QString& text) {
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
        }
    });
    connect(m_bridge, &TerminalBridge::pasteRequested, this, [this]() {
        auto text = QApplication::clipboard()->text();
        text.replace("\n", "\r");
        m_backend->sendInput(text.toUtf8());
    });

    connect(m_backend, &SessionBackend::outputReady, this, [this](const QByteArray& bytes) {
        appendToTerminal(bytes);
    });
    connect(m_backend, &SessionBackend::errorRaised, this, [this](const QString& text) {
        appendToTerminal(QString("\r\n[error] %1\r\n").arg(text).toUtf8());
    });

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        m_terminal->page()->runJavaScript("window.getSelectionText();", [this](const QVariant& value) {
            QApplication::clipboard()->setText(value.toString());
        });
    });

    connect(m_pasteButton, &QPushButton::clicked, this, [this]() {
        auto text = QApplication::clipboard()->text();
        text.replace("\n", "\r");
        m_backend->sendInput(text.toUtf8());
    });
}

TerminalTab::~TerminalTab() {
    if (m_backend) {
        m_backend->stop();
    }
}

QString TerminalTab::title() const {
    return m_profile.name.isEmpty() ? m_profile.id : m_profile.name;
}

void TerminalTab::appendToTerminal(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return;
    }

    constexpr int kMaxBufferedBytes = 2 * 1024 * 1024;
    m_pendingOutput.append(bytes);
    if (m_pendingOutput.size() > kMaxBufferedBytes) {
        m_pendingOutput.remove(0, m_pendingOutput.size() - kMaxBufferedBytes);
    }

    if (m_pageLoaded && !m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void TerminalTab::flushPendingOutput() {
    if (!m_pageLoaded || !m_terminal || !m_terminal->page()) {
        if (m_flushTimer->isActive()) {
            m_flushTimer->stop();
        }
        return;
    }

    if (m_pendingOutput.isEmpty()) {
        if (m_flushTimer->isActive()) {
            m_flushTimer->stop();
        }
        return;
    }

    constexpr int kChunkBytes = 32 * 1024;
    const int size = std::min(kChunkBytes, static_cast<int>(m_pendingOutput.size()));
    const QByteArray chunk = m_pendingOutput.left(size);
    m_pendingOutput.remove(0, size);

    const auto payload = QString::fromLatin1(chunk.toBase64());
    const auto script = QString(
        "if (typeof window.appendTerminalData === 'function') { window.appendTerminalData('%1'); }")
                            .arg(payload);
    m_terminal->page()->runJavaScript(script);

    if (m_pendingOutput.isEmpty()) {
        m_flushTimer->stop();
    }
}

#include "TerminalTab.moc"
