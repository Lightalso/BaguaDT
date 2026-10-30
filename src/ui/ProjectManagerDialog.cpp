#include "ProjectManagerDialog.h"
#include "ProjectFile.h"
#include <QColor>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

namespace bagua {
ProjectManagerDialog::ProjectManagerDialog(const QStringList &recentProjects, const QString &currentProject,
                                           QWidget *parent)
    : QDialog(parent), recent(recentProjects), currentPath(currentProject) {
    setObjectName(QStringLiteral("projectManagerDialog"));
    setWindowTitle(QStringLiteral("项目管理 — BaguaDT"));
    resize(980, 600);
    setMinimumSize(760, 480);
    if (!currentPath.isEmpty() &&
        std::none_of(recent.cbegin(), recent.cend(), [&](const QString &path) {
            return QFileInfo(path).absoluteFilePath().compare(QFileInfo(currentPath).absoluteFilePath(),
                                                             Qt::CaseInsensitive) == 0;
        }))
        recent.prepend(currentPath);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(8);
    auto *heading = new QLabel(QStringLiteral("项目管理"));
    heading->setStyleSheet(QStringLiteral("font-size:15pt;font-weight:600;color:#17324d;"));
    root->addWidget(heading);
    auto *subheading =
        new QLabel(QStringLiteral("项目文件使用 .bgp 后缀。选择最近项目查看场景与录制，或从当前页签新建项目。"));
    subheading->setStyleSheet(QStringLiteral("color:#607080;"));
    root->addWidget(subheading);

    auto *split = new QHBoxLayout;
    split->setSpacing(10);

    auto *listPanel = new QWidget;
    listPanel->setObjectName(QStringLiteral("projectListPanel"));
    auto *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    auto *recentLabel = new QLabel(QStringLiteral("最近项目"));
    recentLabel->setStyleSheet(QStringLiteral("font-weight:600;color:#33475b;"));
    listLayout->addWidget(recentLabel);
    list = new QListWidget;
    list->setObjectName(QStringLiteral("projectList"));
    list->setAlternatingRowColors(true);
    list->setMinimumWidth(270);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    listLayout->addWidget(list, 1);
    auto *recentButtons = new QHBoxLayout;
    recentButtons->setSpacing(6);
    auto *newButton = new QPushButton(QStringLiteral("新建"));
    newButton->setObjectName(QStringLiteral("projectNewButton"));
    newButton->setToolTip(QStringLiteral("把当前打开的所有场景页签保存为新的 .bgp 项目"));
    auto *browseButton = new QPushButton(QStringLiteral("浏览…"));
    browseButton->setObjectName(QStringLiteral("projectBrowseButton"));
    removeButton = new QPushButton(QStringLiteral("移出"));
    removeButton->setObjectName(QStringLiteral("projectRemoveButton"));
    removeButton->setToolTip(QStringLiteral("把选中项目移出最近列表；不会删除项目文件"));
    recentButtons->addWidget(newButton);
    recentButtons->addWidget(browseButton);
    recentButtons->addWidget(removeButton);
    listLayout->addLayout(recentButtons);
    split->addWidget(listPanel, 0);

    auto *detailsPanel = new QWidget;
    detailsPanel->setObjectName(QStringLiteral("projectDetailsPanel"));
    auto *detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(10, 0, 0, 0);
    detailsLayout->setSpacing(6);
    nameLabel = new QLabel(QStringLiteral("请选择一个项目"));
    nameLabel->setObjectName(QStringLiteral("projectNameLabel"));
    nameLabel->setStyleSheet(QStringLiteral("font-size:13pt;font-weight:600;color:#1b2a3a;"));
    nameLabel->setWordWrap(true);
    pathLabel = new QLabel;
    pathLabel->setObjectName(QStringLiteral("projectPathLabel"));
    pathLabel->setStyleSheet(QStringLiteral("color:#66788a;"));
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    summaryLabel = new QLabel;
    summaryLabel->setObjectName(QStringLiteral("projectSummaryLabel"));
    summaryLabel->setStyleSheet(QStringLiteral("color:#4d6072;"));
    summaryLabel->setWordWrap(true);
    detailsLayout->addWidget(nameLabel);
    detailsLayout->addWidget(pathLabel);
    detailsLayout->addWidget(summaryLabel);

    sceneTable = new QTableWidget;
    sceneTable->setObjectName(QStringLiteral("projectSceneTable"));
    sceneTable->setColumnCount(4);
    sceneTable->setHorizontalHeaderLabels(
        {QStringLiteral("场景"), QStringLiteral("模式"), QStringLiteral("录制"), QStringLiteral("状态")});
    sceneTable->verticalHeader()->setVisible(false);
    sceneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sceneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sceneTable->setAlternatingRowColors(true);
    sceneTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    sceneTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sceneTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    sceneTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    detailsLayout->addWidget(sceneTable, 1);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    openButton = new QPushButton(QStringLiteral("打开项目"));
    openButton->setObjectName(QStringLiteral("projectOpenButton"));
    openButton->setStyleSheet(QStringLiteral("QPushButton{background:#0f6cbd;color:white;border:1px solid #0b5a9e;"
                                             "border-radius:3px;padding:4px 16px;} QPushButton:hover{background:#1780da;}"));
    revealButton = new QPushButton(QStringLiteral("在资源管理器中显示"));
    revealButton->setObjectName(QStringLiteral("projectRevealButton"));
    actions->addWidget(openButton);
    actions->addWidget(revealButton);
    actions->addStretch();
    detailsLayout->addLayout(actions);
    auto *hint = new QLabel(
        QStringLiteral("项目清单保存场景和录制的相对路径；场景文件与 .data 资源目录始终随项目目录移动。"));
    hint->setStyleSheet(QStringLiteral("color:#7a8a99;"));
    hint->setWordWrap(true);
    detailsLayout->addWidget(hint);
    split->addWidget(detailsPanel, 1);
    root->addLayout(split, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (auto *close = buttons->button(QDialogButtonBox::Close); close)
        close->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
        auto *item = list->item(row);
        showDetails(item ? item->data(Qt::UserRole).toString() : QString{});
    });
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { openSelected(); });
    connect(openButton, &QPushButton::clicked, this, &ProjectManagerDialog::openSelected);
    connect(revealButton, &QPushButton::clicked, this, [this] {
        const QString path = selectedPath();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    connect(removeButton, &QPushButton::clicked, this, &ProjectManagerDialog::removeSelected);
    connect(browseButton, &QPushButton::clicked, this, &ProjectManagerDialog::browse);
    connect(newButton, &QPushButton::clicked, this, [this] {
        emit createProjectRequested();
        accept();
    });
    reload();
}

QString ProjectManagerDialog::selectedPath() const {
    auto *item = list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void ProjectManagerDialog::reload(const QString &preferred) {
    const QString previous = !preferred.isEmpty() ? preferred : selectedPath();
    list->clear();
    int selectRow = -1;
    for (int i = 0; i < recent.size(); ++i) {
        const QString path = QFileInfo(recent[i]).absoluteFilePath();
        const ProjectInfo info = readProjectInfo(path);
        QString text = info.valid ? info.name : QStringLiteral("⚠ ") + info.name;
        if (info.valid && (info.missingScenes > 0)) {
            text += QStringLiteral(" · %1 个场景缺失").arg(info.missingScenes);
        }
        if (!currentPath.isEmpty() &&
            QFileInfo(currentPath).absoluteFilePath().compare(path, Qt::CaseInsensitive) == 0)
            text += QStringLiteral("（当前）");
        auto *item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(info.valid ? path : path + QStringLiteral("\n") + info.error);
        if (!info.valid)
            item->setForeground(QColor("#b42318"));
        if ((!previous.isEmpty() && path.compare(QFileInfo(previous).absoluteFilePath(), Qt::CaseInsensitive) == 0) ||
            (previous.isEmpty() && !currentPath.isEmpty() &&
             path.compare(QFileInfo(currentPath).absoluteFilePath(), Qt::CaseInsensitive) == 0))
            selectRow = i;
    }
    if (list->count() == 0) {
        auto *empty = new QListWidgetItem(QStringLiteral("暂无最近项目。使用“新建项目”或“浏览”开始。"), list);
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(QColor("#7a8a99"));
        showDetails({});
        return;
    }
    if (selectRow < 0)
        selectRow = 0;
    list->setCurrentRow(selectRow);
}

void ProjectManagerDialog::showDetails(const QString &path) {
    const QString selected = path;
    if (selected.isEmpty()) {
        nameLabel->setText(QStringLiteral("请选择一个项目"));
        pathLabel->clear();
        summaryLabel->clear();
        sceneTable->setRowCount(0);
        openButton->setEnabled(false);
        revealButton->setEnabled(false);
        removeButton->setEnabled(false);
        return;
    }
    const ProjectInfo info = readProjectInfo(selected);
    nameLabel->setText(info.valid ? info.name : QStringLiteral("无法读取的项目"));
    pathLabel->setText(QDir::toNativeSeparators(info.path));
    if (!info.valid) {
        summaryLabel->setText(info.error);
        summaryLabel->setStyleSheet(QStringLiteral("color:#b42318;"));
        sceneTable->setRowCount(0);
        openButton->setEnabled(false);
        revealButton->setEnabled(true);
        removeButton->setEnabled(true);
        return;
    }
    summaryLabel->setStyleSheet(QStringLiteral("color:#4d6072;"));
    const int recordingCount =
        std::accumulate(info.scenes.cbegin(), info.scenes.cend(), 0,
                        [](int sum, const ProjectSceneInfo &scene) { return sum + scene.recordingCount; });
    QString summary = QStringLiteral("%1 个场景 · %2 个录制 · 修改于 %3")
                          .arg(info.scenes.size())
                          .arg(recordingCount)
                          .arg(info.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (info.missingScenes > 0)
        summary += QStringLiteral(" · ⚠ %1 个场景文件缺失").arg(info.missingScenes);
    summaryLabel->setText(summary);
    sceneTable->setRowCount(info.scenes.size());
    for (int row = 0; row < info.scenes.size(); ++row) {
        const auto &scene = info.scenes[row];
        auto *name = new QTableWidgetItem(scene.name);
        name->setToolTip(scene.sceneRelative.isEmpty() ? QStringLiteral("仅包含录制") : scene.sceneRelative);
        sceneTable->setItem(row, 0, name);
        sceneTable->setItem(row, 1,
                            new QTableWidgetItem(scene.mode == QStringLiteral("replay") ? QStringLiteral("回放")
                                                                                        : QStringLiteral("编辑")));
        sceneTable->setItem(row, 2,
                            new QTableWidgetItem(scene.recordingCount > 0 ? QString::number(scene.recordingCount)
                                                                          : QStringLiteral("—")));
        QString status = QStringLiteral("就绪");
        QColor color("#25704a");
        if (!scene.scenePath.isEmpty() && !scene.sceneExists) {
            status = QStringLiteral("缺少场景");
            color = QColor("#b42318");
        } else if (scene.missingRecordings > 0) {
            status = QStringLiteral("缺少 %1 个录制").arg(scene.missingRecordings);
            color = QColor("#b42318");
        }
        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setForeground(color);
        sceneTable->setItem(row, 3, statusItem);
    }
    openButton->setEnabled(true);
    revealButton->setEnabled(true);
    removeButton->setEnabled(true);
}

void ProjectManagerDialog::browse() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 BaguaDT 项目"), {},
                                                      QStringLiteral("BaguaDT 项目 (*.bgp);;旧版项目 "
                                                                     "(*.bagua.project.json);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    emit openProjectRequested(path);
    accept();
}

void ProjectManagerDialog::openSelected() {
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    emit openProjectRequested(path);
    accept();
}

void ProjectManagerDialog::removeSelected() {
    const int row = list->currentRow();
    if (row < 0)
        return;
    const QString path = list->item(row)->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    emit removeRecentRequested(path);
    recent.removeAll(path);
    reload();
}
} // namespace bagua
