#include "DetailsWindow.h"
#include "LogHelper.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QStyle>

DetailsWindow::DetailsWindow(QTreeWidget *treeWidget, QObject *parent)
	: QObject(parent), m_treeWidget(treeWidget) 
{
	m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	// m_treeWidget->setHeaderLabels({tr("File"), tr("Source Hash"), tr("Dest Hash")});

	// Ensure the scrollbar policy allows horizontal scrolling
	m_treeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

	// This ensures the tree doesn't "stretch" the last column to the edge,
	// allowing it to grow beyond the window border
	m_treeWidget->header()->setStretchLastSection(false);

	m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch); // File column stretches
	m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::Interactive); // Hashes are fixed/manual
	m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::Interactive);
	m_treeWidget->setColumnWidth(0, 200); // Set a reasonable default

	// Ensure the tree itself doesn't have a huge minimum size hint
	m_treeWidget->setMinimumWidth(100);

	// Set a maximum section size for the first column to prevent it from pushing the layout
	m_treeWidget->header()->setCascadingSectionResizes(true);

	// Set a minimum size for the first column
	// This prevents the window from being forced wide by content,
	// but forces scrollbars if the window is resized too small.
	m_treeWidget->header()->setMinimumSectionSize(100);
	m_treeWidget->header()->setDefaultSectionSize(150);

	connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &DetailsWindow::onCustomContextMenu);
}

void DetailsWindow::setSourceDest(const QString &source, const QString &dest) {
	m_sourceFolder = source;
	m_destFolder = dest;
}

void DetailsWindow::clearHistory() {
	if (m_treeWidget)
		m_treeWidget->clear();
	const QString path = getHistoryPath();
	const QFile file(path);

	if (file.exists()) {
		LOG(LogLevel::INFO) << "Removing history file: " << path;
		QFile::remove(path);
	} else {
		LOG(LogLevel::ERROR) << "History file not found: " << path;
	}
}

void DetailsWindow::addHistoryEntry(const QString &timestamp, const QString &mode,
									const QList<HistoryEntry> &entries, bool saveToFile) 
{
	if (!m_treeWidget)
		return;

	// Create the Top-Level "Job" (e.g., "2024-05-01 10:00 - Copy")
	QTreeWidgetItem *jobItem = new QTreeWidgetItem();
	jobItem->setText(0, timestamp + " - " + mode);
	jobItem->setFont(0, QFont("Arial", 10, QFont::Bold));

	QTreeWidgetItem *sourceRoot = new QTreeWidgetItem(jobItem);
	sourceRoot->setText(0, tr("Source: ") + m_sourceFolder);
	sourceRoot->setFont(0, QFont("Arial", 10, QFont::Bold));
	sourceRoot->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

	QTreeWidgetItem *destRoot = new QTreeWidgetItem(jobItem);
	destRoot->setText(0, tr("Destination: ") + m_destFolder);
	destRoot->setFont(0, QFont("Arial", 10, QFont::Bold));
	destRoot->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

	// Add File items
	for (const auto &entry : entries) {
		addPathToTree(m_treeWidget, jobItem, entry.path, entry.error, entry.srcHash, entry.destHash);
	}

	m_treeWidget->insertTopLevelItem(0, jobItem);
	// jobItem->setExpanded(true);

	// Save to JSON
	if (saveToFile)
		saveHistoryEntry(timestamp, mode, entries);
}

void DetailsWindow::populateErrorTree(QTreeWidget *tree, const QList<HistoryEntry> &entries) {
	if (!tree)
		return;

	tree->clear();
	// tree->setHeaderLabels({"File", "Source Hash", "Dest Hash"});
	tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // File column stretches
	tree->header()->setSectionResizeMode(1, QHeaderView::Interactive); // Hashes are fixed/manual
	tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
	tree->setColumnWidth(0, 200); // Set a reasonable default

	// Ensure the tree itself doesn't have a huge minimum size hint
	tree->setMinimumWidth(100);
	tree->setMinimumHeight(100);

	// Allow horizontal scrolling
	tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	tree->header()->setStretchLastSection(false);

	// Set a maximum section size for the first column to prevent it from pushing the layout
	m_treeWidget->header()->setCascadingSectionResizes(true);

	// Set a minimum size for the first column
	// This prevents the window from being forced wide by content,
	// but forces scrollbars if the window is resized too small.
	tree->header()->setMinimumSectionSize(100);
	tree->header()->setDefaultSectionSize(150);


	QTreeWidgetItem *root = new QTreeWidgetItem(tree);
	root->setText(0, "Errors");
	root->setFont(0, QFont("Arial", 10, QFont::Bold));
	root->setIcon(0, tree->style()->standardIcon(QStyle::SP_MessageBoxWarning));

	for (const auto &entry : entries) {
		if (!entry.error.isEmpty()) {
			addPathToTree(tree, root, entry.path, entry.error, entry.srcHash, entry.destHash);
		}
	}

	root->setExpanded(true);
	tree->expandAll();
}

QString DetailsWindow::getHistoryPath() const {
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir().mkpath(dir);
	return dir + "/history.json";
}

void DetailsWindow::addPathToTree(QTreeWidget *tree, QTreeWidgetItem *parent, const QString &fullPath, const QString &error, const QString &srcHash, const QString &destHash) {
	if (!tree)
		return;

	QDir directory(m_destFolder);
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
				current->setIcon(0, tree->style()->standardIcon(QStyle::SP_FileIcon));
			else
				current->setIcon(0, tree->style()->standardIcon(QStyle::SP_DirIcon));
		}

		// If it's the last part (the file) and has an error
		if (i == parts.size() - 1 && !error.isEmpty()) {
			current->setForeground(0, Qt::red);
			current->setText(1, srcHash);
			current->setText(2, destHash);
			// Add error as a child item so it appears "below"
			QTreeWidgetItem *errItem = new QTreeWidgetItem(current);
			errItem->setText(0, "Error: " + error);
			errItem->setForeground(0, Qt::red);
			errItem->setIcon(0, tree->style()->standardIcon(QStyle::SP_MessageBoxWarning));
			current->setExpanded(true);
		} else if (i == parts.size() - 1) {
			current->setText(1, srcHash);
			current->setText(2, destHash);
		}
	}
}

void DetailsWindow::saveHistoryEntry(const QString &timestamp, const QString &mode, const QList<HistoryEntry> &entries) {
	QFile file(getHistoryPath());

	// Check if the file is over 5MB (5 * 1024 * 1024 bytes)
	const qint64 sizeLimit = 5 * 1024 * 1024;
	if (file.exists() && file.size() > sizeLimit) {
		LOG(LogLevel::WARNING) << "History file too large (" << file.size() << " bytes). Clearing...";
		clearHistory();
	}

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
	for (const auto &e : entries) {
		QJsonObject fObj;
		fObj["path"] = e.path;
		fObj["error"] = e.error;
		fObj["srcHash"] = e.srcHash;
		fObj["destHash"] = e.destHash;
		filesArray.append(fObj);
	}
	jobObj["entries"] = filesArray;
	array.prepend(jobObj);

	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(array).toJson());
	} else {
		LOG(LogLevel::ERROR) << "Failed to commit history file.";
	}
}

void DetailsWindow::loadHistory() {
	QFile file(getHistoryPath());
	if (!file.open(QIODevice::ReadOnly))
		return;

	QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
	// Iterate backwards so that when we insert at index 0, the newest items appear at the top.
	for (int i = array.size() - 1; i >= 0; --i) {
		QJsonObject job = array[i].toObject();
		QList<HistoryEntry> entries;
		QJsonArray files = job["entries"].toArray();
		for (const QJsonValue &fVal : files) {
			QJsonObject fObj = fVal.toObject();
			entries.append({fObj["path"].toString(),
				fObj["error"].toString(),
				fObj["srcHash"].toString(),
				fObj["destHash"].toString()});
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
	if (!item)
		return;

	int column = m_treeWidget->columnAt(pos.x());
	if (column < 0)
		return;

	QMenu menu(m_treeWidget);
	QAction *copyAction = menu.addAction(tr("Copy text"));
	connect(copyAction, &QAction::triggered, [item, &column]() {
		QClipboard *clipboard = QApplication::clipboard();
		clipboard->setText(item->text(column));
	});
	menu.exec(m_treeWidget->mapToGlobal(pos));
}
