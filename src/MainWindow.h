#pragma once

#include <QCloseEvent>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "CopyWorker.h"
#include "DetailsWindow.h"

enum class OperationMode {
	Copy,
	Move,
	Settings,
	PreviewUI
};

class SpeedGraph : public QWidget { // Inherits from QWidget
Q_OBJECT // Enables Qt meta-object features
	public :

	bool m_update_graph = true;
	std::vector<double> m_history;

	// Without explicit, these might be allowed:
	// SpeedGraph graph = someWidget;  // Implicit conversion
	// With explicit, only direct initialization is allowed
	// SpeedGraph graph(someWidget);
	explicit SpeedGraph(QWidget *parent = nullptr);
	void addSpeedPoint(double mbps);
	QString formatSpeed(double mbps);

	void setPaused(bool paused) {
		if (m_isPaused != paused) {
			m_isPaused = paused;
			update(); // Redraw with new colors
		}
	}

protected:
	// Override the paint event to custom draw the graph
	void paintEvent(QPaintEvent *event) override;

private:
	// std::vector<double> m_history;
	double m_maxSpeed;
	bool m_isPaused = false;
};

namespace Ui {
	class MainWindow;
}


class MainWindow : public QWidget {
	Q_OBJECT
public:
	explicit MainWindow(OperationMode mode, const std::vector<std::string> &sources, const std::string &dest, QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void onStatusChanged(CopyWorker::Status status);
	void onTotalProgress(int fileCount, int totalFiles);
	void onTogglePause();
	void onUpdateProgress(QString src, QString dest, int percent, int totalPercent, double curSpeed, double avgSpeed, long secondsLeft);
	void onError(CopyWorker::FileError err);
	void onFinished();
	void onConflictNeeded(QString src, QString dest, QString suggestedName);
	void onFileCompleted(QString path, QString srcHash, QString destHash, bool isTopLevel);

protected:
	void closeEvent(QCloseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	// void moveEvent(QMoveEvent *event) override;

	private : void logHistory(const QString &path, const QString &error = "", const QString &srcHash = "", const QString &destHash = "");
	void highlightFile(const QStringList &paths);
	void updateTaskbarProgress(int percent);
	void onToggleDetails();
	void updateProgressUi();
	void generateTestData();

	// UI Main window size
	int m_expandedHeight;
	int m_collapsedHeight;

	QPoint m_relativeOffset;
	bool m_isOffsetInitialized = false;
	Ui::MainWindow *ui;
	DetailsWindow *m_detailsWindow = nullptr;
	QStringList m_topLevelItems;
	QList<HistoryEntry> m_jobHistory;
	QSet<QString> m_loggedFiles;
	CopyWorker *m_worker;
	SpeedGraph *m_graph;
	CopyWorker::Status m_status_code;
	QString m_status_string;
	QString m_currentFile;
	QString m_currentDest;
	QString m_sourceFolder;
	QString m_destFolder;
	QString m_modeString;

	// Manages the steady 100ms graph updates
	QTimer *m_graphTimer;

	long m_secondsLeft = -1;
	QString m_baseTitle;

	// Holds the EMA (Exponential Moving Average) smoothing filtered speed value
	double m_smoothedSpeed;

	std::atomic<double> m_currentSpeed{0.0};
	double m_avgSpeed;
	int m_totalProgress;
	int m_filePercent;
	int m_totalFiles;
	int m_filesRemaining;
	int m_filesProcessed;
	bool m_isPaused;
	bool m_testMode = false;
	bool m_progress_updated = false;
	bool m_resize_event = false;
};
