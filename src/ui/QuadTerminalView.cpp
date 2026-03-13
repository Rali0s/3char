#include "ui/QuadTerminalView.hpp"

#include "ui/TerminalTab.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <cmath>

QuadTerminalView::QuadTerminalView(const QList<SessionProfile>& profiles, const AppConfig& appConfig, QWidget* parent)
    : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setSpacing(4);
    grid->setContentsMargins(0, 0, 0, 0);

    if (profiles.isEmpty()) {
        auto* placeholder = new QLabel("No profiles selected", this);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("background:#1e1e1e;color:#888;border:1px solid #333;");
        grid->addWidget(placeholder, 0, 0);
        return;
    }

    const int count = profiles.size();
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));
    (void)rows;

    for (int i = 0; i < count; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        auto* tab = new TerminalTab(profiles[i], appConfig, this);
        grid->addWidget(tab, row, col);
    }
}
