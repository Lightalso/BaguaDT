#pragma once
#include <QDialog>
#include <QStringList>

class QLabel;
class QListWidget;
class QTableWidget;
class QPushButton;

namespace bagua {
// Dedicated project browser. It deliberately only reads .bgp manifests; the
// MainWindow performs the actual scene staging and undo-stack creation.
class ProjectManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectManagerDialog(const QStringList &recentProjects, const QString &currentProject,
                                  QWidget *parent = nullptr);

signals:
    void openProjectRequested(const QString &path);
    void createProjectRequested();
    void removeRecentRequested(const QString &path);

private:
    void reload(const QString &preferred = {});
    void showDetails(const QString &path);
    void browse();
    void openSelected();
    void removeSelected();
    QString selectedPath() const;

    QStringList recent;
    QString currentPath;
    QListWidget *list = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *pathLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QTableWidget *sceneTable = nullptr;
    QPushButton *openButton = nullptr;
    QPushButton *revealButton = nullptr;
    QPushButton *removeButton = nullptr;
};
} // namespace bagua
