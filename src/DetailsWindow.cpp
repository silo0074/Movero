#include <QStyle>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include "DetailsWindow.h"
#include "LogHelper.h"

DetailsWindow::DetailsWindow(QTreeWidget* treeWidget, QObject *parent) 
    : QObject(parent), m_treeWidget(treeWidget) 
{
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // Set the header to resize based on the actual content width
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    // Ensure the scrollbar policy allows horizontal scrolling
    m_treeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // This ensures the tree doesn't "stretch" the last column to the edge, 
    // allowing it to grow beyond the window border
    m_treeWidget->header()->setStretchLastSection(false);
    
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &DetailsWindow::onCustomContextMenu);
}

void DetailsWindow::setSourceDest(const QString &source, const QString &dest) {
    m_sourceFolder = source;
    m_destFolder = dest;
}

void DetailsWindow::clearHistory() {
    if (m_treeWidget) m_treeWidget->clear();
    const QString path = getHistoryPath();
    LOG(LogLevel::DEBUG) << "Removing history file: " << path;
    QFile::remove(path);
}

void DetailsWindow::addHistoryEntry(const QString &timestamp, const QString &mode, 
                        const QList<HistoryEntry> &entries, bool saveToFile)
{
    if (!m_treeWidget) return;

    // Create the Top-Level "Job" (e.g., "2024-05-01 10:00 - Copy") 
    QTreeWidgetItem *jobItem = new QTreeWidgetItem(m_treeWidget);
    jobItem->setText(0, timestamp + " - " + mode);
    jobItem->setFont(0, QFont("Arial", 10, QFont::Bold));
    
    QTreeWidgetItem *sourceRoot = new QTreeWidgetItem(jobItem);
    sourceRoot->setText(0, "Source: " + m_sourceFolder);
    sourceRoot->setFont(0, QFont("Arial", 10, QFont::Bold));
    sourceRoot->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

    QTreeWidgetItem *destRoot = new QTreeWidgetItem(jobItem);
    destRoot->setText(0, "Destination: " + m_destFolder);
    destRoot->setFont(0, QFont("Arial", 10, QFont::Bold));
    destRoot->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

    // Add File items
    for (const auto &entry : entries) {
        addPathToTree(jobItem, entry.path, entry.error);
    }
    
    jobItem->setExpanded(true);
    
    // Save to JSON
    if(saveToFile) saveHistoryEntry(timestamp, mode, entries);
}

QString DetailsWindow::getHistoryPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/history.json";
}

void DetailsWindow::addPathToTree(QTreeWidgetItem *parent, const QString &fullPath, const QString &error) {
    if (!m_treeWidget) return;

    QDir directory(m_sourceFolder);
    QString relativePath = directory.relativeFilePath(fullPath);
    QString normalized = QDir::cleanPath(relativePath);
    QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
    
    QTreeWidgetItem *current = parent;
    for (int i = 0; i < parts.size(); ++i) {
        bool found = false;
        for (int j = 0; j < current->childCount(); ++j) {
            if (current->child(j)->text(0) == parts[i]) {
                current = current->child(j);
                found = true;
                break;
            }
        }
        if (!found) {
            current = new QTreeWidgetItem(current);
            current->setText(0, parts[i]);
            if (i == parts.size() - 1) 
                current->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_FileIcon));
            else 
                current->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));
        }
        
        // If it's the last part (the file) and has an error
        if (i == parts.size() - 1 && !error.isEmpty()) {
            current->setForeground(0, Qt::red);
            // Add error as a child item so it appears "below"
            QTreeWidgetItem *errItem = new QTreeWidgetItem(current);
            errItem->setText(0, "Error: " + error);
            errItem->setForeground(0, Qt::red);
            errItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_MessageBoxWarning));
            current->setExpanded(true);
        }
    }
}

void DetailsWindow::saveHistoryEntry(const QString &timestamp, const QString &mode, const QList<HistoryEntry> &entries) {
    QFile file(getHistoryPath());
    QJsonArray array;
    
    if (file.open(QIODevice::ReadOnly)) {
        array = QJsonDocument::fromJson(file.readAll()).array();
        file.close();
    }

    QJsonObject jobObj;
    jobObj["time"] = timestamp;
    jobObj["mode"] = mode;
    jobObj["sourceRoot"] = m_sourceFolder;
    jobObj["destRoot"] = m_destFolder;
    
    QJsonArray filesArray;
    for(const auto& e : entries) {
        QJsonObject fObj;
        fObj["path"] = e.path;
        fObj["error"] = e.error;
        filesArray.append(fObj);
    }
    jobObj["entries"] = filesArray;
    array.append(jobObj);

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
    }
}

void DetailsWindow::loadHistory() {
    QFile file(getHistoryPath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &val : array) {
        QJsonObject job = val.toObject();
        QList<HistoryEntry> entries;
        QJsonArray files = job["entries"].toArray();
        for (const QJsonValue &fVal : files) {
            QJsonObject fObj = fVal.toObject();
            entries.append({fObj["path"].toString(), fObj["error"].toString()});
        }

        // Convert temporary QString to a const reference for setSourceDest
        const QString sourceRoot = job["sourceRoot"].toString();
        const QString destRoot = job["destRoot"].toString();
        setSourceDest(sourceRoot, destRoot);
        addHistoryEntry(job["time"].toString(), job["mode"].toString(), entries, false);
    }
}

void DetailsWindow::onCustomContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (!item) return;

    QMenu menu(m_treeWidget);
    QAction *copyAction = menu.addAction("Copy text");
    connect(copyAction, &QAction::triggered, [item]() {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(item->text(0));
    });
    menu.exec(m_treeWidget->mapToGlobal(pos));
}
