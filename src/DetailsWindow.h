#pragma once
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStandardPaths>
#include <QTreeWidget>

struct HistoryEntry {
	QString path;
	QString error;
	QString srcHash;
	QString destHash;
};

class DetailsWindow : public QObject {
	Q_OBJECT
      public:
	explicit DetailsWindow(QTreeWidget *treeWidget, QObject *parent = nullptr);

	void setSourceDest(const QString &source, const QString &dest);
	void addHistoryEntry(const QString &timestamp, const QString &mode,
		const QList<HistoryEntry> &entries, bool saveToFile = true);
	void loadHistory();
	void populateErrorTree(QTreeWidget *tree, const QList<HistoryEntry> &entries);

      public slots:
	void clearHistory();
	void onCustomContextMenu(const QPoint &pos);

      private:
	QString m_sourceFolder;
	QString m_destFolder;
	QTreeWidget *m_treeWidget;

	QString getHistoryPath() const;
	void addPathToTree(QTreeWidget *tree, QTreeWidgetItem *parent, const QString &fullPath, const QString &error, const QString &srcHash, const QString &destHash);
	void saveHistoryEntry(const QString &timestamp, const QString &mode, const QList<HistoryEntry> &entries);
};
