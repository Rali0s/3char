#pragma once

#include "data/Models.hpp"

#include <QWidget>

class QuadTerminalView : public QWidget {
    Q_OBJECT
public:
    explicit QuadTerminalView(const QList<SessionProfile>& profiles, const AppConfig& appConfig, QWidget* parent = nullptr);
};
