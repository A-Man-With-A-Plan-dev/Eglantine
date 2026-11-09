// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>
#include <QWidget>
#include "video_core/nx2_perf.h"

class QAction;
class QCheckBox;
class QComboBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class Nx2PerfDialog : public QWidget {
public:
    explicit Nx2PerfDialog(QWidget* parent = nullptr);

    /// Returns a QAction that can be used to toggle visibility of this dialog.
    QAction* toggleViewAction();

    /// Refreshes the UI state from VideoCommon::Nx2Perf::CurrentOffMask().
    void RefreshState();

    void SetChangeCallback(std::function<void()> callback) {
        change_callback = std::move(callback);
    }

protected:
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private:
    void BuildUi();
    void PopulateSwitches();
    void ApplyFilter();
    void OnItemChanged(QTreeWidgetItem* item, int column);
    void OnResetStartup();
    void OnDefaultPlaytest();
    void OnSafeMode();
    void OnSetVisible(bool enable);
    void OnCopyEnv();
    void OnToggleAlwaysOnTop(bool on_top);

    QAction* toggle_view_action = nullptr;
    QLabel* summary_label = nullptr;
    QCheckBox* always_on_top_check = nullptr;
    QLineEdit* search_edit = nullptr;
    QComboBox* filter_combo = nullptr;
    QTreeWidget* tree = nullptr;
    QPushButton* copy_env_btn = nullptr;
    QTimer* sync_timer = nullptr;

    std::vector<QTreeWidgetItem*> items;
    VideoCommon::Nx2Perf::OffMask last_mask{};
    std::function<void()> change_callback;
    bool updating_ui = false;
};
