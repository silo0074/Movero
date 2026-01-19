#pragma once
#include <QObject>
#include <QTreeWidget>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QHeaderView>

struct HistoryEntry {
    QString path;
    QString error;
};

class DetailsWindow : public QObject {
    Q_OBJECT
public:
    explicit DetailsWindow(QTreeWidget* treeWidget, QObject *parent = nullptr);

    void setSourceDest(const QString &source, const QString &dest);
    void addHistoryEntry(const QString &timestamp, const QString &mode, 
                        const QList<HistoryEntry> &entries, bool saveToFile = true);
    void loadHistory();

public slots:
    void clearHistory();
    void onCustomContextMenu(const QPoint &pos);

private:
    QString m_sourceFolder;
    QString m_destFolder;
    QTreeWidget *m_treeWidget;

    QString getHistoryPath() const;
    void addPathToTree(QTreeWidgetItem *parent, const QString &fullPath, const QString &error);
    void saveHistoryEntry(const QString &timestamp, const QString &mode, const QList<HistoryEntry> &entries);
};
