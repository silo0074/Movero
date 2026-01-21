#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QThread>
#include <QVBoxLayout>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <qobject.h>

#include "Config.h"
#include "CopyWorker.h"
#include "DetailsWindow.h"
#include "LogHelper.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"

// --- SpeedGraph Implementation ---
// Initializes members BEFORE constructor body executes
// More efficient than assignment inside constructor body
SpeedGraph::SpeedGraph(QWidget *parent)
	: QWidget(parent), // Base class initialization
	  m_maxSpeed(Config::SPEED_GRAPH_MAX_SPEED) // Member initialization
{
	// Constructor body
	setMinimumHeight(Config::SPEED_GRAPH_MIN_HEIGHT);
	m_history.resize(Config::SPEED_GRAPH_HISTORY_SIZE, 0.0);
}

void SpeedGraph::addSpeedPoint(double mbps) {
	// QMutexLocker locker(&m_mutex); // Lock the mutex for the duration of this function

	// If we hit the limit, remove the oldest (first) point
	if (m_history.size() >= Config::SPEED_GRAPH_HISTORY_SIZE) {
		m_history.erase(m_history.begin());
	}

	// Add the new data point to the end
	m_history.push_back(mbps);

	// Dynamic Scaling logic
	double targetMax = Config::SPEED_GRAPH_MAX_SPEED; // Floor of 10MB/s is usually better for visibility
	// Smooth scaling
	for (double s : m_history) {
		if (s > targetMax)
			targetMax = s;
	}

	if (targetMax > m_maxSpeed) {
		// Jump UP instantly to prevent clipping
		m_maxSpeed = targetMax;
	} else {
		// Roll DOWN slowly (adjust the 0.95 to change the "cool down" speed)
		m_maxSpeed = (m_maxSpeed * 0.95) + (targetMax * 0.05);
	}

	update(); // Trigger paintEvent
}

void SpeedGraph::paintEvent(QPaintEvent *) {
	// QMutexLocker locker(&m_mutex); // Also lock during painting to prevent crashes while drawing
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	// Color definitions based on state
	QColor mainColor(m_isPaused ? Config::COLOR_GRAPH_PAUSED : Config::COLOR_GRAPH_ACTIVE);
	QColor gradientTop(m_isPaused ? Config::COLOR_GRAPH_GRADIENT_PAUSED : Config::COLOR_GRAPH_GRADIENT_ACTIVE);

	// Define margins for labels
	const int leftMargin = Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT ? 5 : 70;
	const int rightMargin = Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT ? 70 : 5;
	const int topMargin = 10;
	const int bottomMargin = Config::SPEED_GRAPH_SHOW_TIME_LABELS ? 30 : 5;

	int w = width();
	int h = height();

	// Effective drawing area for the grid and data
	QRect gridRect(leftMargin, topMargin, w - leftMargin - rightMargin, h - topMargin - bottomMargin);

	// Add 10% headroom so the line never hits the physical top of the widget
	double effectiveMax = m_maxSpeed * 1.1;

	// Points per second value represents the sampling frequency
	// Example: If Interval is 0.5s, pointsPerSecond = 2.0
	// Example: If Interval is 0.1s, pointsPerSecond = 10.0
	const double pointsPerSecond = 1.0 / Config::SPEED_UPDATE_INTERVAL;

	double step = static_cast<double>(gridRect.width()) / (m_history.size() - 1);

	// Draw Horizontal Grid Lines and Speed Labels
	p.setFont(QFont("Arial", 8));

	for (int i = 0; i <= 4; ++i) {
		int y = gridRect.top() + (gridRect.height() / 4) * i;

		// Draw Grid Line
		p.setPen(QPen(QColor(Config::COLOR_GRAPH_GRID), 1, Qt::DashLine));
		p.drawLine(gridRect.left(), y, gridRect.right(), y);

		// Calculate Speed for this line (i=0 is Top/Max, i=4 is Bottom/0)
		double speedAtLine = m_maxSpeed * (4 - i) / 4.0;
		QString speedLabel = formatSpeed(speedAtLine);

		// Draw Speed Label on the left
		p.setPen(QColor(Config::COLOR_GRAPH_TEXT));
		if (Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT) {
			p.drawText(w - rightMargin + 5, y + 4, speedLabel);
		} else {
			p.drawText(5, y + 4, speedLabel);
		}
	}

	// Draw Time Scale (X-Axis Labels)
	if (Config::SPEED_GRAPH_SHOW_TIME_LABELS) {
		// Calculate total duration based on the actual update frequency of the UI graph
		double totalSeconds = ((m_history.size() - 1) * Config::UPDATE_INTERVAL_MS) / 1000.0;
		if (totalSeconds <= 0)
			totalSeconds = 1.0;

		// Calculate pixels per second based on current window width (Scalable GUI)
		double pixelsPerSecond = gridRect.width() / totalSeconds;

		// Dynamic interval calculation: ensure labels don't overlap (min 60px apart)
		double minPixelsPerLabel = 60.0;
		double minInterval = minPixelsPerLabel / pixelsPerSecond;

		// Select a "nice" interval (1s, 2s, 5s, 10s, 15s, 30s, 60s...)
		int intervalSeconds = 1;
		int niceIntervals[] = {1, 2, 5, 10, 15, 30, 60, 120, 300};
		for (int val : niceIntervals) {
			if (val >= minInterval) {
				intervalSeconds = val;
				break;
			}
		}

		p.setPen(QColor(Config::COLOR_GRAPH_TEXT));

		// Helper lambda to draw a tick and label
		auto drawTick = [&](double timeVal) {
			double x = gridRect.right() - (timeVal * pixelsPerSecond);

			// Draw tick
			p.drawLine(x, gridRect.bottom(), x, gridRect.bottom() + 5);

			// Format Label
			QString timeLabel;
			int tInt = static_cast<int>(timeVal + 0.5);

			if (timeVal < 0.1)
				timeLabel = "0s";
			else if (tInt < 60)
				timeLabel = QString("-%1s").arg(tInt);
			else {
				int m = tInt / 60;
				int s = tInt % 60;
				if (s == 0)
					timeLabel = QString("-%1m").arg(m);
				else
					timeLabel = QString("-%1m %2s").arg(m).arg(s);
			}

			// Draw Text
			float textWidth = p.fontMetrics().horizontalAdvance(timeLabel);
			double textX = x - (textWidth / 2);

			// Clamp to widget bounds to ensure 0s and Max are visible
			if (textX + textWidth > w)
				textX = w - textWidth - 2;
			if (textX < 0)
				textX = 2;

			p.drawText(textX, h - 5, timeLabel);
		};

		// Always draw the Max History label (Leftmost)
		drawTick(totalSeconds);

		// Draw 0s and intermediates
		// Stop if we get too close to the Max label (approx 50px clearance) to avoid overlap
		double leftThreshold = gridRect.left() + 50;

		for (int t = 0; t < static_cast<int>(totalSeconds); t += intervalSeconds) {
			double x = gridRect.right() - (t * pixelsPerSecond);
			if (x < leftThreshold)
				break;
			drawTick(static_cast<double>(t));
		}
	}

	// Create and Draw the Path (Data)
	if (m_history.size() > 1) {
		QPainterPath path;
		bool started = false;

		for (size_t i = 0; i < m_history.size(); ++i) {
			double x = gridRect.left() + (i * step);
			// Map speed to Y: 0 speed = gridRect.bottom(), max = gridRect.top()
			double y = gridRect.bottom() - ((m_history[i] / effectiveMax) * gridRect.height());

			if (!started) {
				path.moveTo(x, y);
				started = true;
			} else {
				path.lineTo(x, y);
			}
		}

		// Draw Fill Gradient with state-aware colors
		QLinearGradient gradient(0, gridRect.top(), 0, gridRect.bottom());
		gradient.setColorAt(0, gradientTop);
		gradient.setColorAt(1, QColor(gradientTop.red(), gradientTop.green(), 0, 0));

		QPainterPath fillPath = path;
		fillPath.lineTo(gridRect.right(), gridRect.bottom());
		fillPath.lineTo(gridRect.left(), gridRect.bottom());
		p.fillPath(fillPath, gradient);

		// Draw the line with the state-aware mainColor
		p.setPen(QPen(mainColor, 2));
		p.drawPath(path);
	}

	// Draw Current Speed Indicator (Dash line if paused)
	double currentY = gridRect.bottom() - ((m_history.back() / m_maxSpeed) * gridRect.height());
	Qt::PenStyle lineStyle = m_isPaused ? Qt::DashLine : Qt::SolidLine;
	p.setPen(QPen(m_isPaused ? Qt::red : Qt::black, 1, lineStyle));
	p.drawLine(gridRect.left(), currentY, gridRect.right(), currentY);

	QString curSpeedLabel = formatSpeed(m_history.back());
	int textWidth = p.fontMetrics().horizontalAdvance(curSpeedLabel);

	if (Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT) {
		p.drawText(gridRect.left() + 5, currentY - 2, curSpeedLabel);
	} else {
		p.drawText(gridRect.right() - textWidth - 5, currentY - 2, curSpeedLabel);
	}
}

// Helper to format labels cleanly
QString SpeedGraph::formatSpeed(double mbps) {
	if (mbps >= 1024)
		return QString::number(mbps / 1024.0, 'f', 1) + " GiB/s";
	if (mbps >= 1)
		return QString::number(mbps, 'f', 1) + " MiB/s";
	return QString::number(mbps * 1024.0, 'f', 0) + " KiB/s";
}

// --- MainWindow Implementation ---
MainWindow::MainWindow(OperationMode mode, const std::vector<std::string> &sources, const std::string &dest, QWidget *parent)
	: QWidget(parent),
	  ui(new Ui::MainWindow),
	  m_isPaused(false),
	  m_smoothedSpeed(0.0),
	  m_totalFiles(0),
	  m_filesRemaining(0),
	  m_filePercent(0),
	  m_totalProgress(0),
	  m_currentSpeed(0.0),
	  m_avgSpeed(0.0) {

	ui->setupUi(this);
	m_graph = ui->speedGraphWidget;

	switch (mode) {
	case OperationMode::Copy:
		m_modeString = "Copying";
		break;
	case OperationMode::Move:
		m_modeString = "Moving";
		break;
	case OperationMode::PreviewUI:
		m_modeString = "Preview UI Mode";
		break;
	default:
		m_modeString = "unknown";
		break;
	}

	// Initialize DetailsWindow with the tree widget from MainWindow.ui
	m_detailsWindow = new DetailsWindow(ui->treeWidget, this);
	connect(ui->btnClearHistory, &QPushButton::clicked, m_detailsWindow, &DetailsWindow::clearHistory);
	m_detailsWindow->loadHistory();

	// Save source folder
	QString source = QString::fromStdString(sources[0]);
	QFileInfo info(QDir::cleanPath(source));
	m_sourceFolder = info.absolutePath();

	// Save destination folder
	m_destFolder = QString::fromStdString(dest);

	LOG(LogLevel::DEBUG) << "Source folder: " << m_sourceFolder;
	LOG(LogLevel::DEBUG) << "Destination folder: " << m_destFolder;

	// Set window title
	// m_modeString = (mode == "mv") ? QStringLiteral("Moving") : QStringLiteral("Copying");
	m_baseTitle = QStringLiteral(APP_NAME) + " - " + m_modeString;
	setWindowTitle(m_baseTitle);

	// Allow labels to shrink below their text content width
	// This prevents the window width from being locked by long text
	ui->labelFrom->setMinimumWidth(0);
	ui->labelTo->setMinimumWidth(0);
	ui->tabWidget->hide();
	ui->tabWidget->setCurrentIndex(0);
	this->adjustSize();
	this->resize(Config::WINDOW_WIDTH, this->height());
	m_collapsedHeight = this->height(); // Save current size
	m_expandedHeight = Config::WINDOW_HEIGHT_EXPANDED;

	if (mode == OperationMode::PreviewUI) {
		m_testMode = true;
		m_worker = nullptr;
		m_status = m_modeString;
		ui->labelStatus->setText(m_status);
	} else {
		CopyWorker::Mode workerMode = (mode == OperationMode::Move) ? CopyWorker::Move : CopyWorker::Copy;
		m_worker = new CopyWorker(sources, dest, workerMode, this);

		connect(m_worker, &CopyWorker::progressChanged, this, &MainWindow::onUpdateProgress);
		connect(m_worker, &CopyWorker::statusChanged, this, &MainWindow::onStatusChanged);
		connect(m_worker, &CopyWorker::totalProgress, this, &MainWindow::onTotalProgress);
		connect(m_worker, &CopyWorker::errorOccurred, this, &MainWindow::onError);
		connect(m_worker, &CopyWorker::finished, this, &MainWindow::onFinished);
		connect(m_worker, &CopyWorker::conflictNeeded, this, &MainWindow::onConflictNeeded, Qt::QueuedConnection);
		connect(m_worker, &CopyWorker::fileCompleted, this, &MainWindow::onFileCompleted);
	}

	connect(ui->btnPause, &QPushButton::clicked, this, &MainWindow::onTogglePause);
	connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::close);
	connect(ui->btnShowBottomPanel, &QPushButton::clicked, this, &MainWindow::onToggleDetails);

	// Update GUI
	// Bad: Capturing [&] (everything by reference) is dangerous for timers
	// Good: Capture [this] and check pointers
	m_graphTimer = new QTimer(this);
	if (m_testMode) {
		connect(m_graphTimer, &QTimer::timeout, this, &MainWindow::generateTestData);
	} else {
		connect(m_graphTimer, &QTimer::timeout, this, [this] { updateProgressUi(); });
	}
	m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // 10 updates per second

	if (m_worker) {
		m_worker->start();
	}
}

MainWindow::~MainWindow() {
	if (m_graphTimer) {
		m_graphTimer->stop();
	}

	if (m_worker) {
		m_worker->cancel();
		m_worker->wait(); // Ensure the thread is dead before we delete 'ui'
	}

	if (m_detailsWindow) {
		delete m_detailsWindow;
	}

	delete ui;
}

/*----------------------------------------------------------------------
	Updated by copy worker faster than the timer updates the GUI
------------------------------------------------------------------------*/
void MainWindow::onUpdateProgress(QString src, QString dest, int percent, int totalPercent, double curSpeed, double avgSpeed, QString eta) {
	m_currentFile = src;
	m_currentDest = dest;
	m_filePercent = percent;
	m_totalProgress = totalPercent;
	m_currentSpeed = curSpeed;
	m_avgSpeed = avgSpeed;
	m_eta = eta;

	if (curSpeed > 0) {
		// smoothing factor: 0.15 (lower = smoother/slower, higher = jumpier/faster)
		m_smoothedSpeed = (m_smoothedSpeed * 0.85) + (curSpeed * 0.15);
	}

	if (percent == 100) {
		logHistory(src, "");
	}
}

void MainWindow::onStatusChanged(CopyWorker::Status status) {
	switch (status) {
	case CopyWorker::DryRunGenerating:
		m_status = "DRY RUN: Generating test file...";
		break;
	case CopyWorker::Scanning:
		m_status = "Scanning and calculating space...";
		break;
	case CopyWorker::RemovingEmptyFolders:
		m_status = "Removing empty folders...";
		break;
	case CopyWorker::Copying:
		m_status = "Copying...";
		break;
	case CopyWorker::GeneratingHash:
		m_status = "Generating Source Hash...";
		break;
	case CopyWorker::Verifying:
		m_status = "Verifying Checksum...";
		break;
	}

	ui->labelStatus->setText(m_status);
	LOG(LogLevel::DEBUG) << "onStatusChanged: " << m_status;
}

void MainWindow::onTotalProgress(int fileCount, int totalFiles) {
	m_totalFiles = totalFiles;
	m_filesRemaining = totalFiles - fileCount;
	if (fileCount == 0) {
		m_graph->m_history.resize(Config::SPEED_GRAPH_HISTORY_SIZE, 0.0);
	}
}

void MainWindow::closeEvent(QCloseEvent *event) {
	LOG(LogLevel::DEBUG) << "Close event received.";

	if (m_worker && m_worker->isRunning()) {
		// Update UI to show we are stopping
		// m_statusActionLabel->setText("Stopping...");
		// m_statusLabel->setText("Cleaning up...");

		// Signal the thread to stop
		LOG(LogLevel::DEBUG) << "Cancelling copy worker.";
		m_worker->cancel();

		// Wait for the thread to finish safely.
		// This ensures the worker cleans up files and exits run() before we destroy it.
		LOG(LogLevel::DEBUG) << "Waiting for copy worker to finish.";
		m_worker->wait();
	}

	event->accept();
}

// void MainWindow::moveEvent(QMoveEvent *event) {
//     QWidget::moveEvent(event); // Call base class logic

//     // Move the details window only if it exists, is visible, and WE are the ones moving
//     if (m_detailsWindow && m_detailsWindow->isVisible() && m_isOffsetInitialized) {
//         // Apply the stored offset to the new main window position
//         m_detailsWindow->move(this->pos() + m_relativeOffset);
//     }
// }

void MainWindow::onTogglePause() {
	if (m_isPaused) {
		if (m_worker)
			m_worker->resume();
		m_graph->setPaused(false);
		m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // Restart the graph movement
		ui->btnPause->setText("Pause");
	} else {
		if (m_worker)
			m_worker->pause();
		m_graph->setPaused(true);
		m_graphTimer->stop(); // Freeze the graph movement
		ui->btnPause->setText("Resume");
	}
	m_isPaused = !m_isPaused;
}

void MainWindow::onError(CopyWorker::FileError err) {
	QString msg;
	switch (err.code) {
	case CopyWorker::DiskFull:
		if (err.path.isEmpty()) {
			auto parts = err.extraInfo.split('|');
			if (parts.size() >= 2) {
				msg = QString("Not enough space. Required: %1 GB, Available: %2 GB")
						  .arg(parts[0], parts[1]);
			} else {
				msg = "Not enough disk space.";
			}
		} else {
			msg = "Not enough disk space";
		}
		break;
	case CopyWorker::DriveCheckFailed:
		msg = "Could not determine available space on destination.";
		break;
	case CopyWorker::SourceOpenFailed:
		msg = "Failed to open source";
		break;
	case CopyWorker::FileOpenFailed:
		msg = "Failed to open file";
		break;
	case CopyWorker::ReadError:
		msg = "Read error";
		break;
	case CopyWorker::UnexpectedEOF:
		msg = "Unexpected end of file";
		break;
	case CopyWorker::WriteError:
		msg = "Write error";
		break;
	case CopyWorker::ChecksumMismatch:
		msg = "Checksum Mismatch!";
		break;
	default:
		msg = "Unknown error";
		break;
	}

	QString logMsg = err.path.isEmpty() ? msg : (err.path + ": " + msg);
	LOG(LogLevel::DEBUG) << "Error: " + logMsg;
	ui->labelStatus->setText(logMsg);
	if (!err.path.isEmpty()) {
		logHistory(err.path, msg);
		m_detailsWindow->populateErrorTree(ui->treeWidgetErrors, m_jobHistory);
	}
}

void MainWindow::onConflictNeeded(QString src, QString dest, QString suggestedName) {
	// PAUSE the graph timer so it doesn't call addSpeedPoint while we are blocked
	m_graphTimer->stop();

	// 1. No need for manual thread check or invokeMethod.
	// The Qt::QueuedConnection in the constructor guarantees this runs on the Main Thread.

	// 2. Use Stack Allocation (No 'new', no pointer)
	// This guarantees the object exists during exec() and is cleaned up immediately after.
	QDialog dialog(this);
	dialog.setWindowTitle("File Conflict");

	// Do NOT set WA_DeleteOnClose when using stack allocation or exec()
	// dialog.setAttribute(Qt::WA_DeleteOnClose);

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel("Destination file already exists. Select an action:", &dialog));

	// --- Details Grid ---
	QGridLayout *grid = new QGridLayout();
	QFileInfo srcInfo(src);
	QFileInfo destInfo(dest);

	// Helper to format size safely
	auto fmtSize = [](qint64 s) {
		if (s > 1024 * 1024 * 1024)
			return QString::number(s / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
		if (s > 1024 * 1024)
			return QString::number(s / (1024.0 * 1024), 'f', 2) + " MB";
		return QString::number(s / 1024.0, 'f', 2) + " KB";
	};

	// Use robust QFileInfo checks (in case file was moved/deleted in background)
	QString srcDate = srcInfo.exists() ? srcInfo.lastModified().toString() : "Unknown";
	QString destDate = destInfo.exists() ? destInfo.lastModified().toString() : "Unknown";
	qint64 srcSize = srcInfo.exists() ? srcInfo.size() : 0;
	qint64 destSize = destInfo.exists() ? destInfo.size() : 0;

	// Elide long paths
	QFontMetrics metrics(dialog.font());
	QString elidedSrc = metrics.elidedText(src, Qt::ElideMiddle, 800);
	QString elidedDest = metrics.elidedText(dest, Qt::ElideMiddle, 800);

	grid->addWidget(new QLabel("<b>Source:</b>"), 0, 0);
	QLabel *lblSrc = new QLabel(elidedSrc);
	lblSrc->setToolTip(src);
	grid->addWidget(lblSrc, 0, 1);
	grid->addWidget(new QLabel(QString("Size: %1").arg(fmtSize(srcSize))), 1, 1);
	grid->addWidget(new QLabel(QString("Date: %1").arg(srcDate)), 2, 1);

	grid->addWidget(new QLabel("<b>Destination:</b>"), 3, 0);
	QLabel *lblDest = new QLabel(elidedDest);
	lblDest->setToolTip(dest);
	grid->addWidget(lblDest, 3, 1);
	grid->addWidget(new QLabel(QString("Size: %1").arg(fmtSize(destSize))), 4, 1);
	grid->addWidget(new QLabel(QString("Date: %1").arg(destDate)), 5, 1);

	layout->addLayout(grid);

	// --- Rename Input ---
	QHBoxLayout *renameLayout = new QHBoxLayout();
	renameLayout->addWidget(new QLabel("Rename to:"));
	QLineEdit *renameEdit = new QLineEdit(suggestedName);
	renameLayout->addWidget(renameEdit);
	layout->addLayout(renameLayout);

	// --- Controls ---
	QCheckBox *cb = new QCheckBox("Do this for all conflicts", &dialog);
	layout->addWidget(cb);

	QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
	QPushButton *replaceBtn = buttons->addButton("Replace", QDialogButtonBox::AcceptRole);
	QPushButton *skipBtn = buttons->addButton("Skip", QDialogButtonBox::RejectRole);
	QPushButton *renameBtn = buttons->addButton("Rename", QDialogButtonBox::ActionRole);
	QPushButton *cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
	layout->addWidget(buttons);

	// Default Action
	CopyWorker::ConflictAction action = CopyWorker::Cancel;

	// Connect Buttons
	// Note: We capture 'dialog' by reference [&dialog] which is safe because
	// the lambda runs inside dialog.exec() while the object is alive.
	connect(replaceBtn, &QPushButton::clicked, [&]() {
		action = CopyWorker::Replace;
		dialog.accept();
	});
	connect(skipBtn, &QPushButton::clicked, [&]() {
		action = CopyWorker::Skip;
		dialog.accept();
	});
	connect(renameBtn, &QPushButton::clicked, [&]() {
		action = CopyWorker::Rename;
		dialog.accept();
	});
	connect(cancelBtn, &QPushButton::clicked, [&]() {
		action = CopyWorker::Cancel;
		dialog.reject();
	});

	// Center the dialog on the screen
	dialog.adjustSize();
	QScreen *screen = this->screen();
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (screen) {
		QRect screenGeometry = screen->availableGeometry();
		int x = screenGeometry.x() + (screenGeometry.width() - dialog.width()) / 2;
		int y = screenGeometry.y() + (screenGeometry.height() - dialog.height()) / 2;
		dialog.move(x, y);
	}

	// 3. Execution
	// This blocks the Main Thread (but event loop keeps running)
	// Worker Thread is waiting on m_inputWait condition
	dialog.exec();

	// 4. Send Result Back to Worker
	// The dialog is closed, but 'renameEdit' and 'cb' are still valid
	// because they are children of 'dialog' which is still on the stack.
	m_worker->resolveConflict(action, cb->isChecked(), renameEdit->text());

	// Resume the graph timer if we are continuing
	if (action != CopyWorker::Cancel) {
		m_graphTimer->start(Config::UPDATE_INTERVAL_MS);
	}

	// 5. End of function: 'dialog' destructor runs automatically here.
}


void MainWindow::onToggleDetails() {
	bool isVisible = false;

	// Capture the CURRENT width so we don't lose the user's manual resizing
	int currentWidth = this->width();

	if (ui->tabWidget->isVisible()) {
		// --- HIDING (Collapsing) ---
		m_expandedHeight = this->height(); // remember the full height
		ui->tabWidget->hide();
		this->adjustSize(); // let the layout recalculate the new size
		this->resize(currentWidth, m_collapsedHeight);

	} else {
		// --- SHOWING (Expanding) ---
		ui->tabWidget->show();
		ui->treeWidget->header()->doItemsLayout();

		// Restore the saved height while maintaining the current width
		if (m_expandedHeight > 0)
			this->resize(currentWidth, m_expandedHeight);
	}

	ui->btnShowBottomPanel->setText(isVisible ? "▲" : "▼");
}


void MainWindow::onFinished() {
	LOG(LogLevel::DEBUG) << "Done.";
	updateProgressUi();
	m_graphTimer->stop(); // Stop the graph once finished
	updateTaskbarProgress(0); // Clear progress bar
	setWindowTitle(m_baseTitle); // Reset title to remove percentage
	ui->labelStatus->setText("Done.");
	ui->btnPause->setEnabled(false);
	ui->btnCancel->setText("Close");

	// Save history
	if (!m_jobHistory.isEmpty()) {

		if (m_detailsWindow) {
			QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
			m_detailsWindow->setSourceDest(m_sourceFolder, m_destFolder);
			m_detailsWindow->addHistoryEntry(currentTime, m_modeString, m_jobHistory);
			m_jobHistory.clear();
			m_loggedFiles.clear();
		}
	}

	if (Config::CLOSE_ON_FINISH) {
		close();
	}
}

void MainWindow::updateTaskbarProgress(int percent) {
	// Clamp percentage
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;

	// Unity Launcher API (Supported by Ubuntu Dock, Dash to Dock, KDE Task Manager)
	// We broadcast a signal that the Dock listens to.

	QVariantMap properties;
	properties.insert("progress", percent / 100.0);
	properties.insert("progress-visible", (percent > 0 && percent < 100));

	// URI must match the desktop file name set in main.cpp
	QString uri = "application://" + QCoreApplication::applicationName() + ".desktop";

	QDBusMessage message = QDBusMessage::createSignal(
		"/com/canonical/Unity/LauncherEntry",
		"com.canonical.Unity.LauncherEntry",
		"Update");

	message << uri << properties;
	QDBusConnection::sessionBus().send(message);
}

void MainWindow::updateProgressUi() {
	uint64_t totalBytes = 0;
	uint64_t completedBytes = 0;

	if (m_worker) {
		totalBytes = m_worker->m_totalSizeToCopy;
		completedBytes = m_worker->m_totalBytesCopied;
	} else if (m_testMode) {
		totalBytes = 1024ULL * 1024 * 1024; // 1 GB dummy
		completedBytes = static_cast<uint64_t>(totalBytes * (m_totalProgress / 100.0));
	}

	uint64_t remainingBytes = (totalBytes > completedBytes) ? (totalBytes - completedBytes) : 0;

	auto formatSize = [](uint64_t bytes) -> QString {
		double val = static_cast<double>(bytes);
		if (val < 1024.0)
			return QString::number(val, 'f', 0) + " B";
		val /= 1024.0;
		if (val < 1024.0)
			return QString::number(val, 'f', 2) + " KiB";
		val /= 1024.0;
		if (val < 1024.0)
			return QString::number(val, 'f', 2) + " MiB";
		val /= 1024.0;
		return QString::number(val, 'f', 2) + " GiB";
	};

	QString totalSizeString = formatSize(totalBytes);
	QString remainingSizeString = formatSize(remainingBytes);
	QString completedSizeString = formatSize(completedBytes);

	// Copying 0 of 0 files
	ui->labelCopyingFiles->setText("Copying " + QString::number(m_filesRemaining) +
		" of " + QString::number(m_totalFiles) + " files");
	// Total progress
	ui->labelProgress->setText(QString::number(m_totalProgress) + "% complete");

	// Remaining: 00:00:00 (MB/s avg)
	ui->labelETA->setText("Remaining: " + m_eta + " (" + QString::number(m_avgSpeed, 'f', 0) + " MiB/s)");

	// From and To
	QFontMetrics metricsFrom(ui->labelFrom->font());
	ui->labelFrom->setText(metricsFrom.elidedText("<b>From :</b> " + m_currentFile, Qt::ElideMiddle, ui->labelFrom->width() - 5));

	QFontMetrics metricsTo(ui->labelTo->font());
	ui->labelTo->setText(metricsTo.elidedText("<b>To:</b> " + m_currentDest, Qt::ElideMiddle, ui->labelTo->width() - 5));

	// Transfer size: 0 MB of 0 MB
	ui->labelItems->setText(completedSizeString + " of " + totalSizeString);

	// Current file progress
	ui->labelFileProgress->setText(QString::number(m_filePercent) + "%");

	// Update Window Title for Taskbar Progress
	setWindowTitle(QString("%1% - %2").arg(m_totalProgress).arg(m_baseTitle));

	// Update Taskbar / Dock Progress
	updateTaskbarProgress(m_totalProgress);

	if (m_graph)
		m_graph->addSpeedPoint(m_smoothedSpeed);

	// Decay speed if no data point received
	m_smoothedSpeed *= 0.9;
}

void MainWindow::generateTestData() {
	static double t = 0;
	t += 0.1;
	// Generate a sine wave speed between 10 and 90 MB/s
	double speed = (sin(t) + 1.2) * 40.0;

	m_smoothedSpeed = speed;
	m_currentSpeed = speed;
	m_avgSpeed = speed;
	m_totalProgress = (static_cast<int>(t * 5) % 100);
	m_eta = "00:01:30";
	m_currentFile = "Test_File_Data.dat";
	m_currentDest = "/tmp/Test_File_Data.dat";
	updateProgressUi();
}

void MainWindow::logHistory(const QString &path, const QString &error, const QString &srcHash, const QString &destHash) {
	if (!Config::LOG_HISTORY_ENABLED)
		return;

	// If we already logged this file (e.g. as success), update it if we now have an error
	if (m_loggedFiles.contains(path)) {
		for (auto &entry : m_jobHistory) {
			if (entry.path == path) {
				if (!error.isEmpty()) {
					entry.error = error;
				}
				if (!srcHash.isEmpty()) {
					entry.srcHash = srcHash;
				}
				if (!destHash.isEmpty()) {
					entry.destHash = destHash;
				}
				return;
			}
		}
	}
	// Otherwise add new entry
	m_jobHistory.append({path, error, srcHash, destHash});
	m_loggedFiles.insert(path);
}

void MainWindow::onFileCompleted(QString path, QString srcHash, QString destHash) {
	logHistory(path, "", srcHash, destHash);
}
