// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <fmt/format.h>

#include "video_core/nx2_perf_table.h"
#include "yuzu/debugger/nx2_perf_dialog.h"

namespace {

enum class FilterMode : int {
    All = 0,
    LiveSafe = 1,
    Modified = 2,
    CurrentlyOn = 3,
    CurrentlyOff = 4,
    DefaultOn = 5,
    DefaultOff = 6,
};

} // Anonymous namespace

Nx2PerfDialog::Nx2PerfDialog(QWidget* parent) : QWidget(parent, Qt::Dialog) {
    setObjectName(QStringLiteral("Nx2PerfDialog"));
    setWindowTitle(tr("NX2 Live Switches"));
    resize(680, 620);
    setMinimumSize(520, 380);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);

    BuildUi();
    PopulateSwitches();
    RefreshState();

    sync_timer = new QTimer(this);
    sync_timer->setInterval(250);
    connect(sync_timer, &QTimer::timeout, this, [this] {
        if (VideoCommon::Nx2Perf::CurrentOffMask() != last_mask) {
            RefreshState();
        }
    });
}

void Nx2PerfDialog::BuildUi() {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    // Top summary + Always on Top
    auto* header_layout = new QHBoxLayout();
    summary_label = new QLabel(this);
    QFont summary_font = summary_label->font();
    summary_font.setBold(true);
    summary_label->setFont(summary_font);
    header_layout->addWidget(summary_label, 1);

    always_on_top_check = new QCheckBox(tr("Always on Top"), this);
    connect(always_on_top_check, &QCheckBox::toggled, this, &Nx2PerfDialog::OnToggleAlwaysOnTop);
    header_layout->addWidget(always_on_top_check);
    root_layout->addLayout(header_layout);

    // Search + Filter row
    auto* filter_layout = new QHBoxLayout();
    search_edit = new QLineEdit(this);
    search_edit->setPlaceholderText(tr("Search switch name or #index (e.g. dlss, rebar, macro, 98)..."));
    search_edit->setClearButtonEnabled(true);
    connect(search_edit, &QLineEdit::textChanged, this, &Nx2PerfDialog::ApplyFilter);
    filter_layout->addWidget(search_edit, 1);

    filter_combo = new QComboBox(this);
    filter_combo->addItem(tr("All Switches"));
    filter_combo->addItem(tr("Live-Safe Only"));
    filter_combo->addItem(tr("Modified vs Startup"));
    filter_combo->addItem(tr("Currently ON"));
    filter_combo->addItem(tr("Currently OFF"));
    filter_combo->addItem(tr("Default ON (Playtest)"));
    filter_combo->addItem(tr("Default OFF (Experimental)"));
    connect(filter_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &Nx2PerfDialog::ApplyFilter);
    filter_layout->addWidget(filter_combo);
    root_layout->addLayout(filter_layout);

    // Quick preset / bulk buttons row
    auto* actions_layout = new QHBoxLayout();
    auto* reset_btn = new QPushButton(tr("Reset to Startup"), this);
    reset_btn->setToolTip(tr("Restore all switches to the state when Eglantine started"));
    connect(reset_btn, &QPushButton::clicked, this, &Nx2PerfDialog::OnResetStartup);
    actions_layout->addWidget(reset_btn);

    auto* default_btn = new QPushButton(tr("Default (Playtest)"), this);
    default_btn->setToolTip(tr("Enable all standard playtest switches; keep experimental/quarantined off"));
    connect(default_btn, &QPushButton::clicked, this, &Nx2PerfDialog::OnDefaultPlaytest);
    actions_layout->addWidget(default_btn);

    auto* safe_btn = new QPushButton(tr("Safe Mode"), this);
    safe_btn->setToolTip(tr("Turn off all experimental switches (same as EDEN_NX2_PERF_SAFE=1)"));
    connect(safe_btn, &QPushButton::clicked, this, &Nx2PerfDialog::OnSafeMode);
    actions_layout->addWidget(safe_btn);

    auto* vis_on_btn = new QPushButton(tr("Visible ON"), this);
    vis_on_btn->setToolTip(tr("Turn ON all switches currently shown in the filtered list (except quarantined)"));
    connect(vis_on_btn, &QPushButton::clicked, this, [this] { OnSetVisible(true); });
    actions_layout->addWidget(vis_on_btn);

    auto* vis_off_btn = new QPushButton(tr("Visible OFF"), this);
    vis_off_btn->setToolTip(tr("Turn OFF all switches currently shown in the filtered list"));
    connect(vis_off_btn, &QPushButton::clicked, this, [this] { OnSetVisible(false); });
    actions_layout->addWidget(vis_off_btn);

    copy_env_btn = new QPushButton(tr("Copy Env"), this);
    copy_env_btn->setToolTip(tr("Copy EDEN_NX2_PERF_ON / EDEN_NX2_PERF_OFF for current switches to clipboard"));
    connect(copy_env_btn, &QPushButton::clicked, this, &Nx2PerfDialog::OnCopyEnv);
    actions_layout->addWidget(copy_env_btn);

    root_layout->addLayout(actions_layout);

    // Switches table
    tree = new QTreeWidget(this);
    tree->setColumnCount(4);
    tree->setHeaderLabels({tr("Switch"), tr("#"), tr("Mode"), tr("Default / Diff")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(tree, &QTreeWidget::itemChanged, this, &Nx2PerfDialog::OnItemChanged);
    connect(tree, &QTreeWidget::itemClicked, this, [](QTreeWidgetItem* item, int column) {
        if (item && column != 0) {
            item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked
                                                                      : Qt::Checked);
        }
    });
    root_layout->addWidget(tree, 1);
}

void Nx2PerfDialog::PopulateSwitches() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    updating_ui = true;
    tree->clear();
    items.clear();
    items.reserve(Nx2Perf::opt_names.size());

    const Nx2Perf::OffMask default_off = Nx2Perf::DefaultOffMask() | Nx2Perf::always_off;

    for (size_t i = 0; i < Nx2Perf::opt_names.size(); ++i) {
        auto* item = new QTreeWidgetItem(tree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        item->setText(0, QString::fromUtf8(Nx2Perf::opt_names[i].data(),
                                           static_cast<qsizetype>(Nx2Perf::opt_names[i].size())));
        item->setText(1, QStringLiteral("#%1").arg(i));

        const bool is_quarantined = Nx2Perf::IsOff(Nx2Perf::always_off, i);
        const bool is_live_safe = Nx2Perf::IsOff(Nx2Perf::live_safe, i);
        const bool is_default_on = !Nx2Perf::IsOff(default_off, i);

        if (is_quarantined) {
            item->setText(2, tr("Quarantined"));
        } else if (is_live_safe) {
            item->setText(2, tr("Live-Safe"));
        } else {
            item->setText(2, tr("Direct"));
        }

        item->setText(3, is_default_on ? tr("Default: ON") : tr("Default: OFF"));
        items.push_back(item);
    }

    updating_ui = false;
}

void Nx2PerfDialog::RefreshState() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    // If future switches were added or count changed, rebuild items automatically.
    if (items.size() != Nx2Perf::opt_names.size()) {
        PopulateSwitches();
    }

    updating_ui = true;
    const Nx2Perf::OffMask current = Nx2Perf::CurrentOffMask();
    const Nx2Perf::OffMask startup = Nx2Perf::StartupOffMask();
    const Nx2Perf::OffMask default_off = Nx2Perf::DefaultOffMask() | Nx2Perf::always_off;
    last_mask = current;

    size_t on_count = 0;
    size_t live_safe_on = 0;
    size_t live_safe_total = 0;
    size_t modified_count = 0;

    for (size_t i = 0; i < items.size(); ++i) {
        QTreeWidgetItem* item = items[i];
        const bool is_on = !Nx2Perf::IsOff(current, i);
        const bool was_on = !Nx2Perf::IsOff(startup, i);
        const bool is_live_safe = Nx2Perf::IsOff(Nx2Perf::live_safe, i);
        const bool is_default_on = !Nx2Perf::IsOff(default_off, i);

        if (is_on) {
            ++on_count;
        }
        if (is_live_safe) {
            ++live_safe_total;
            if (is_on) {
                ++live_safe_on;
            }
        }
        const bool modified = (is_on != was_on);
        if (modified) {
            ++modified_count;
        }

        item->setCheckState(0, is_on ? Qt::Checked : Qt::Unchecked);

        QString status = is_default_on ? tr("Default: ON") : tr("Default: OFF");
        if (modified) {
            status += is_on ? tr("  [Flipped ON]") : tr("  [Flipped OFF]");
        }
        item->setText(3, status);

        QFont font = item->font(0);
        font.setBold(modified);
        item->setFont(0, font);
        item->setFont(3, font);
    }

    summary_label->setText(
        tr("ON: %1 / %2   |   Live-Safe ON: %3 / %4   |   Modified vs Startup: %5")
            .arg(on_count)
            .arg(items.size())
            .arg(live_safe_on)
            .arg(live_safe_total)
            .arg(modified_count));

    updating_ui = false;
    ApplyFilter();
    if (change_callback) {
        change_callback();
    }
}

void Nx2PerfDialog::ApplyFilter() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    const QString query = search_edit->text().trimmed();
    const auto mode = static_cast<FilterMode>(filter_combo->currentIndex());
    const Nx2Perf::OffMask current = Nx2Perf::CurrentOffMask();
    const Nx2Perf::OffMask startup = Nx2Perf::StartupOffMask();
    const Nx2Perf::OffMask default_off = Nx2Perf::DefaultOffMask() | Nx2Perf::always_off;

    for (size_t i = 0; i < items.size(); ++i) {
        QTreeWidgetItem* item = items[i];
        bool match_text = true;
        if (!query.isEmpty()) {
            match_text = item->text(0).contains(query, Qt::CaseInsensitive) ||
                         item->text(1).contains(query, Qt::CaseInsensitive);
        }

        bool match_mode = true;
        const bool is_on = !Nx2Perf::IsOff(current, i);
        const bool was_on = !Nx2Perf::IsOff(startup, i);
        switch (mode) {
        case FilterMode::All:
            match_mode = true;
            break;
        case FilterMode::LiveSafe:
            match_mode = Nx2Perf::IsOff(Nx2Perf::live_safe, i);
            break;
        case FilterMode::Modified:
            match_mode = (is_on != was_on);
            break;
        case FilterMode::CurrentlyOn:
            match_mode = is_on;
            break;
        case FilterMode::CurrentlyOff:
            match_mode = !is_on;
            break;
        case FilterMode::DefaultOn:
            match_mode = !Nx2Perf::IsOff(default_off, i);
            break;
        case FilterMode::DefaultOff:
            match_mode = Nx2Perf::IsOff(default_off, i);
            break;
        }

        item->setHidden(!(match_text && match_mode));
    }
}

void Nx2PerfDialog::OnItemChanged(QTreeWidgetItem* item, int column) {
    if (updating_ui || !item || column != 0) {
        return;
    }
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
    if (index >= Nx2Perf::opt_names.size()) {
        return;
    }
    const bool enable = item->checkState(0) == Qt::Checked;
    Nx2Perf::Set(static_cast<Nx2Perf::Opt>(index), enable, Nx2Perf::opt_names[index]);
    RefreshState();
}

void Nx2PerfDialog::OnResetStartup() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    Nx2Perf::SetOffMask(Nx2Perf::StartupOffMask(), "gui-reset");
    RefreshState();
}

void Nx2PerfDialog::OnDefaultPlaytest() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    Nx2Perf::SetOffMask(Nx2Perf::always_off, "gui-default");
    RefreshState();
}

void Nx2PerfDialog::OnSafeMode() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    Nx2Perf::SetOffMask(Nx2Perf::StartupOffMask() | Nx2Perf::DefaultOffMask() | Nx2Perf::always_off,
                        "gui-safe");
    RefreshState();
}

void Nx2PerfDialog::OnSetVisible(bool enable) {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    Nx2Perf::OffMask mask = Nx2Perf::CurrentOffMask();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i]->isHidden()) {
            continue;
        }
        const auto opt = static_cast<Nx2Perf::Opt>(i);
        if (enable) {
            if (!Nx2Perf::IsOff(Nx2Perf::always_off, i)) {
                mask &= ~Nx2Perf::Bit(opt);
            }
        } else {
            mask |= Nx2Perf::Bit(opt);
        }
    }
    Nx2Perf::SetOffMask(mask, enable ? "gui-visible-on" : "gui-visible-off");
    RefreshState();
}

void Nx2PerfDialog::OnCopyEnv() {
    namespace Nx2Perf = VideoCommon::Nx2Perf;
    const Nx2Perf::OffMask current = Nx2Perf::CurrentOffMask();
    const Nx2Perf::OffMask baseline = Nx2Perf::always_off;

    std::string on_list;
    std::string off_list;
    for (size_t i = 0; i < Nx2Perf::opt_names.size(); ++i) {
        const bool is_on = !Nx2Perf::IsOff(current, i);
        const bool base_on = !Nx2Perf::IsOff(baseline, i);
        if (is_on && !base_on) {
            if (!on_list.empty()) {
                on_list += ',';
            }
            on_list += Nx2Perf::opt_names[i];
        } else if (!is_on && base_on) {
            if (!off_list.empty()) {
                off_list += ',';
            }
            off_list += Nx2Perf::opt_names[i];
        }
    }

    std::string env_text;
    if (!on_list.empty()) {
        env_text += fmt::format("EDEN_NX2_PERF_ON={}", on_list);
    }
    if (!off_list.empty()) {
        if (!env_text.empty()) {
            env_text += ' ';
        }
        env_text += fmt::format("EDEN_NX2_PERF_OFF={}", off_list);
    }
    if (env_text.empty()) {
        env_text = "# All switches match default playtest state";
    }

    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(QString::fromStdString(env_text));
    }
    copy_env_btn->setText(tr("Copied!"));
    QTimer::singleShot(1200, this, [this] { copy_env_btn->setText(tr("Copy Env")); });
}

void Nx2PerfDialog::OnToggleAlwaysOnTop(bool on_top) {
    const bool was_visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on_top);
    if (was_visible) {
        show();
    }
}

QAction* Nx2PerfDialog::toggleViewAction() {
    if (toggle_view_action == nullptr) {
        toggle_view_action = new QAction(tr("&NX2 Live Switches"), this);
        toggle_view_action->setCheckable(true);
        toggle_view_action->setChecked(isVisible());
        connect(toggle_view_action, &QAction::toggled, this, &Nx2PerfDialog::setVisible);
    }
    return toggle_view_action;
}

void Nx2PerfDialog::showEvent(QShowEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(true);
    }
    RefreshState();
    if (sync_timer) {
        sync_timer->start();
    }
    QWidget::showEvent(ev);
}

void Nx2PerfDialog::hideEvent(QHideEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(false);
    }
    if (sync_timer) {
        sync_timer->stop();
    }
    QWidget::hideEvent(ev);
}
