#include <QCheckBox>
#include <QCloseEvent>
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
#include <QtDBus/QDBusReply>
#include <QUrl>
#include <qobject.h>

#include "Config.h"
#include "CopyWorker.h"
#include "DetailsWindow.h"
#include "LogHelper.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"

/*----------------------------------------------------------------
  SpeedGraph Implementation
  This class provides the custom-drawn speed graph widget.
------------------------------------------------------------------*/
// Initializes members BEFORE constructor body executes
// More efficient than assignment inside constructor body
/*----------------------------------------------------------------
  Constructor for the SpeedGraph widget.
  Initializes the graph's minimum height and history buffer.
------------------------------------------------------------------*/
SpeedGraph::SpeedGraph(QWidget *parent)
	: QWidget(parent), // Base class initialization
	  m_maxSpeed(Config::SPEED_GRAPH_MAX_SPEED) // Member initialization
{
	setMinimumHeight(Config::SPEED_GRAPH_MIN_HEIGHT);
	m_history.resize(Config::SPEED_GRAPH_HISTORY_SIZE, 0.0);
}

/*----------------------------------------------------------------
  Adds a new speed data point to the graph's history.
  Manages history size and dynamic scaling of the Y-axis.
------------------------------------------------------------------*/
void SpeedGraph::addSpeedPoint(double mbps) {
	// If we hit the limit, remove the oldest (first) point
	if (m_history.size() >= Config::SPEED_GRAPH_HISTORY_SIZE) {
		m_history.erase(m_history.begin());
	}

	// Add the new data point to the end
	m_history.push_back(mbps);

	// Dynamic Scaling logic
	// Floor of 10MB/s is usually better for visibility
	double targetMax = Config::SPEED_GRAPH_MAX_SPEED;
	// Smooth scaling
	for (double s : m_history) {
		if (s > targetMax)	targetMax = s;
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

/*----------------------------------------------------------------
  Overrides the default paint event to custom-draw the speed graph.
  This includes the grid, labels, data line, and fill gradient.
------------------------------------------------------------------*/
void SpeedGraph::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	// Color definitions based on state
	QColor mainColor(m_isPaused ? Config::COLOR_GRAPH_PAUSED : Config::COLOR_GRAPH_ACTIVE);
	QColor gradientTop(
		m_isPaused ? Config::COLOR_GRAPH_GRADIENT_PAUSED : Config::COLOR_GRAPH_GRADIENT_ACTIVE);

	// Define margins for labels
	const int leftMargin = Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT ? 5 : 70;
	const int rightMargin = Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT ? 70 : 5;
	const int topMargin = 15;
	const int bottomMargin = Config::SPEED_GRAPH_SHOW_TIME_LABELS ? 30 : 5;

	int w = width();
	int h = height();

	// Effective drawing area for the grid and data
	QRect
		gridRect(leftMargin, topMargin, w - leftMargin - rightMargin, h - topMargin - bottomMargin);

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
		// Calculate total duration based on the actual update frequency of the
		// UI graph
		double totalSeconds = ((m_history.size() - 1) * Config::UPDATE_INTERVAL_MS) / 1000.0;
		if (totalSeconds <= 0)
			totalSeconds = 1.0;

		// Calculate pixels per second based on current window width (Scalable
		// GUI)
		double pixelsPerSecond = gridRect.width() / totalSeconds;

		// Dynamic interval calculation: ensure labels don't overlap (min 60px
		// apart)
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

			if (timeVal < 0.1) {
				timeLabel = "0s";
			} else if (tInt < 60) {
				timeLabel = QString("-%1s").arg(tInt);
			} else {
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


		// 1. Calculate the label for the total duration (Max History)
		QString maxTimeLabel;
		int totalInt = static_cast<int>(totalSeconds + 0.5);
		if (totalInt < 60) {
			maxTimeLabel = QString("-%1s").arg(totalInt);
		} else {
			int m = totalInt / 60;
			int s = totalInt % 60;
			maxTimeLabel = (s == 0) ? QString("-%1m").arg(m) : QString("-%1m %2s").arg(m).arg(s);
		}

		// 2. Determine the width of that label
		float maxLabelWidth = p.fontMetrics().horizontalAdvance(maxTimeLabel);

		// 3. Define the dynamic threshold (grid left + label width + 10px padding)
		double dynamicLeftThreshold = gridRect.left() + maxLabelWidth + 10.0;

		// Always draw the Max History label (Leftmost)
		drawTick(totalSeconds);

		// 4. Update the loop to use the dynamic threshold
		for (int t = 0; t < static_cast<int>(totalSeconds); t += intervalSeconds) {
			double x = gridRect.right() - (t * pixelsPerSecond);

			// Use the dynamic threshold instead of + 50
			if (x < dynamicLeftThreshold)
				break;

			drawTick(static_cast<double>(t));
		}

		// Always draw the Max History label (Leftmost)
		//drawTick(totalSeconds);

		// Draw 0s and intermediates
		// Stop if we get too close to the Max label (approx 50px clearance) to
		// avoid overlap
		// double leftThreshold = gridRect.left() + 50;

		// for (int t = 0; t < static_cast<int>(totalSeconds); t += intervalSeconds) {
		// 	double x = gridRect.right() - (t * pixelsPerSecond);
		// 	if (x < leftThreshold)
		// 		break;
		// 	drawTick(static_cast<double>(t));
		// }
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
	double currentY = gridRect.bottom() - ((m_history.back() / effectiveMax) * gridRect.height());
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

/*----------------------------------------------------------------
  Helper function to format a speed value (in MB/s) into a
  human-readable string (e.g., "1.23 GiB/s", "45.6 MiB/s").
------------------------------------------------------------------*/
QString SpeedGraph::formatSpeed(double mbps) {
	if (mbps >= 1024)
		return QString::number(mbps / 1024.0, 'f', 1) + " GiB/s";
	if (mbps >= 1)
		return QString::number(mbps, 'f', 1) + " MiB/s";
	return QString::number(mbps * 1024.0, 'f', 0) + " KiB/s";
}

/*----------------------------------------------------------------
  MainWindow Implementation
  This class manages the main application window, orchestrates the
  UI, and interacts with the CopyWorker background thread.
------------------------------------------------------------------*/
/*----------------------------------------------------------------
  Constructor for the main application window.
  Initializes UI elements, sets up the copy worker, and connects
  all necessary signals and slots.
------------------------------------------------------------------*/
MainWindow::MainWindow(
	OperationMode mode,
	const std::vector<std::string> &sources,
	const std::string &dest,
	QWidget *parent
)	: QWidget(parent), 
	ui(new Ui::MainWindow), 
	m_isPaused(false), 
	m_smoothedSpeed(0.0),
	m_totalFiles(0), 
	m_filesRemaining(0), 
	m_filesProcessed(0),
	m_filePercent(0), 
	m_totalProgress(0),
	m_currentSpeed(0.0), 
	m_avgSpeed(0.0),
	m_status_code(CopyWorker::Copying)
{
	m_topLevelItems.clear();

	ui->setupUi(this);
	m_graph = ui->speedGraphWidget;

	switch (mode) {
		case OperationMode::Copy:
			m_modeString = tr("Copying");
			break;
		case OperationMode::Move:
			m_modeString = tr("Moving");
			break;
		case OperationMode::PreviewUI:
			m_modeString = tr("Preview UI Mode");
			break;
		default:
			m_modeString = tr("unknown");
			break;
	}

	// Initialize DetailsWindow with the tree widget from MainWindow.ui
	m_detailsWindow = new DetailsWindow(ui->treeWidget, this);
	connect(ui->btnClearHistory, &QPushButton::clicked, 
		m_detailsWindow, 
		&DetailsWindow::clearHistory
	);
	m_detailsWindow->loadHistory();

	// Save source folder
	QString source = "";
	m_sourceFolder = "";

	if (!Config::DRY_RUN) {
		source = QString::fromStdString(sources[0]);
		QFileInfo info(QDir::cleanPath(source));
		m_sourceFolder = info.absolutePath();
	} else {
		m_sourceFolder = "Dry run mode";
	}

	// Save destination folder
	m_destFolder = QString::fromStdString(dest);

	LOG(LogLevel::INFO) << "Mode set to: " << m_modeString;
	LOG(LogLevel::INFO) << "Source folder: " << m_sourceFolder;
	LOG(LogLevel::INFO) << "Destination folder: " << m_destFolder;

	// Set window title
	m_baseTitle = QStringLiteral(APP_NAME) + " - " + m_modeString;
	setWindowTitle(m_baseTitle);

	// Allow labels to shrink below their text content width
	// This prevents the window width from being locked by long text
	ui->labelFrom->setMinimumWidth(0);
	ui->labelTo->setMinimumWidth(0);

	// 1. Tell the window to resize itself based on layout needs
	// layout()->setSizeConstraint(QLayout::SetMinAndMaxSize);

	// 2. Remove the stretch factors that cause the 1:2 squeeze
	// ui->verticalLayout->setStretch(0, 0);
	// ui->verticalLayout->setStretch(1, 0);

	ui->btnPause->setProperty("state", "playing"); // Initial state, used by global style sheet
	ui->tabWidget->hide(); // hide 'show more' expandable window
	// Ensure the TabWidget doesn't have a massive minimum size that breaks the layout
	ui->tabWidget->setMinimumHeight(0);
	ui->tabWidget->setCurrentIndex(0);
	ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Interactive);
	this->adjustSize(); // after loading history into 'show more' window
	this->resize(Config::WINDOW_WIDTH, this->height());

	// This is the key for Wayland:
	// 1. Invalidate the current layout
	// ui->verticalLayout->activate();
	// // 2. Tell the window to shrink/grow to fit the new layout state
	// this->adjustSize();

	m_collapsedHeight = this->height(); // Save current size
	m_expandedHeight = Config::WINDOW_HEIGHT_EXPANDED;

	if (mode == OperationMode::PreviewUI) {
		m_testMode = true;
		m_worker = nullptr;
		m_status_string = m_modeString;
		ui->labelStatus->setText(m_status_string);
	} else {
		CopyWorker::Mode workerMode =
			(mode == OperationMode::Move) ? CopyWorker::Move : CopyWorker::Copy;
		m_worker = new CopyWorker(sources, dest, workerMode, this);

		connect(m_worker, &CopyWorker::progressChanged, 
			this, 
			&MainWindow::onUpdateProgress
		);
		connect(m_worker, &CopyWorker::statusChanged, 
			this, 
			&MainWindow::onStatusChanged
		);
		connect(m_worker, &CopyWorker::totalProgress, 
			this, 
			&MainWindow::onTotalProgress
		);
		connect(m_worker, &CopyWorker::errorOccurred, 
			this, 
			&MainWindow::onError
		);
		connect(m_worker, &CopyWorker::finished, 
			this, 
			&MainWindow::onFinished
		);
		connect(m_worker, &CopyWorker::conflictNeeded,
			this,
			&MainWindow::onConflictNeeded,
			Qt::QueuedConnection);
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

/*----------------------------------------------------------------
  Destructor for the main application window.
  Ensures the background worker thread is properly cancelled and
  waited for before cleaning up UI resources.
------------------------------------------------------------------*/
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

/*----------------------------------------------------------------
  Slot that receives real-time progress updates from the worker thread.
  This function is called very frequently and stores the latest data
  in member variables to be processed by the slower UI timer.
------------------------------------------------------------------*/
void MainWindow::onUpdateProgress(QString src, QString dest, int percent,
	int totalPercent, double curSpeed, double avgSpeed,	long secondsLeft) 
{
	m_currentFile = src;
	m_currentDest = dest;
	m_filePercent = percent;
	m_totalProgress = totalPercent;

	if (curSpeed > 0.00001 || (percent == 100 && totalPercent == 100)) {
		m_currentSpeed = curSpeed;
		// Keep avg speed after transfer is complete
		if (percent != 100) m_avgSpeed = avgSpeed;
		m_secondsLeft = secondsLeft;
	}

	if (curSpeed > 0) {
		// smoothing factor: 0.15 (lower = smoother/slower, higher = jumpier/faster)
		m_smoothedSpeed = (m_smoothedSpeed * 0.85) + (curSpeed * 0.15);
	}

	m_progress_updated = true;
}

/*----------------------------------------------------------------
  Slot that receives status changes from the worker thread.
  Updates the main status label with human-readable text.
------------------------------------------------------------------*/
void MainWindow::onStatusChanged(CopyWorker::Status status) {
	switch (status) {
		case CopyWorker::DryRunGenerating:
			m_status_string = tr("DRY RUN: Generating test file...");
			break;
		case CopyWorker::Scanning:
			m_status_string = tr("Scanning and calculating space...");
			break;
		case CopyWorker::RemovingEmptyFolders:
			m_status_string = tr("Removing empty folders...");
			break;
		case CopyWorker::Copying:
			m_status_string = tr("Copying...");
			break;
		case CopyWorker::GeneratingHash:
			m_status_string = tr("Generating Source Hash...");
			break;
		case CopyWorker::Verifying:
			m_status_string = tr("Verifying Checksum...");
			break;
	}

	m_status_code = status;
	ui->labelStatus->setText(m_status_string);
	// LOG(LogLevel::INFO) << "Status Changed: " << m_status_string;
	m_progress_updated = true;
}

/*----------------------------------------------------------------
  Slot that receives updates on the total number of files processed.
------------------------------------------------------------------*/
void MainWindow::onTotalProgress(int fileCount, int totalFiles) {
	m_totalFiles = totalFiles;
	m_filesRemaining = totalFiles - fileCount;
	m_filesProcessed = fileCount;

	if (fileCount == 0) {
		m_graph->m_history.resize(Config::SPEED_GRAPH_HISTORY_SIZE, 0.0);
	}
	m_progress_updated = true;
}


/*----------------------------------------------------------------
  Toggles the paused/resumed state of the copy operation.
  It signals the worker thread and updates the UI accordingly.
------------------------------------------------------------------*/
void MainWindow::onTogglePause() {
	if (m_isPaused) {
		if (m_worker)
			m_worker->resume();
		m_graph->setPaused(false);
		m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // Restart the graph movement
		ui->btnPause->setText("⏸");
		ui->btnPause->setProperty("state", "playing");

	} else {
		if (m_worker)
			m_worker->pause();
		m_graph->setPaused(true);
		m_graphTimer->stop(); // Freeze the graph movement
		ui->btnPause->setText("▶");
		ui->btnPause->setProperty("state", "paused");
	}

	// This forces Qt to re-read the stylesheet for the new property value
	ui->btnPause->style()->unpolish(ui->btnPause);
	ui->btnPause->style()->polish(ui->btnPause);
	m_isPaused = !m_isPaused;
}

/*----------------------------------------------------------------
  Slot to handle errors reported by the worker thread.
  It formats an error message and updates the UI and log.
------------------------------------------------------------------*/
void MainWindow::onError(CopyWorker::FileError err) {
	QString msg;
	switch (err.code) {
		case CopyWorker::DiskFull:
			if (err.path.isEmpty()) {
				auto parts = err.extraInfo.split('|');
				if (parts.size() >= 2) {
					msg = tr("Not enough space. Required: %1 GB, Available: %2 GB")
						.arg(parts[0], parts[1]);
				} else {
					msg = tr("Not enough disk space.");
				}
			} else {
				msg = tr("Not enough disk space");
			}
			break;
		case CopyWorker::DriveCheckFailed:
			msg = tr("Could not determine available space on destination.");
			break;
		case CopyWorker::SourceOpenFailed:
			msg = tr("Failed to open source");
			break;
		case CopyWorker::FileOpenFailed:
			msg = tr("Failed to open file");
			break;
		case CopyWorker::ReadError:
			msg = tr("Read error");
			break;
		case CopyWorker::UnexpectedEOF:
			msg = tr("Unexpected end of file");
			break;
		case CopyWorker::WriteError:
			msg = tr("Write error");
			break;
		case CopyWorker::ChecksumMismatch:
			msg = tr("Checksum Mismatch!");
			break;
		case CopyWorker::DestinationIsDirectory:
			msg = tr("Collision: Destination is a directory, not a link.");
			break;
		default:
			msg = tr("Unknown error");
			break;
	}

	QString logMsg = err.path.isEmpty() ? msg : (err.path + ": " + msg);
	LOG(LogLevel::ERROR) << "Error: " + logMsg;
	ui->labelStatus->setText(msg);
	m_status_string = msg;

	if (!err.path.isEmpty()) {
		logHistory(err.path, msg);
		m_detailsWindow->populateErrorTree(ui->treeWidgetErrors, m_jobHistory);
		ui->tabWidget->setCurrentWidget(ui->tab_2);
		onToggleDetails();
	}
	m_progress_updated = true;
}

/*----------------------------------------------------------------
  Slot that is triggered when a file conflict is detected.
  It displays a dialog to the user to choose an action (Replace,
  Skip, Rename, etc.) and sends the result back to the worker.
------------------------------------------------------------------*/
void MainWindow::onConflictNeeded(QString src, QString dest, QString suggestedName) {
	// PAUSE the graph timer so it doesn't call addSpeedPoint while we are blocked
	m_graphTimer->stop();

	// No need for manual thread check or invokeMethod.
	// The Qt::QueuedConnection in the constructor guarantees this runs on the Main Thread.

	// Use Stack Allocation (No 'new', no pointer)
	// This guarantees the object exists during exec() and is cleaned up immediately after.
	QDialog dialog(this);
	dialog.setWindowTitle(tr("File Conflict"));

	// Do NOT set WA_DeleteOnClose when using stack allocation or exec()
	// dialog.setAttribute(Qt::WA_DeleteOnClose);

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(tr("Destination file already exists. Select an action:"), &dialog));

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
	// Use birthTime() for creation date instead of lastModified()
	QDateTime srcTime = srcInfo.birthTime();
	if (!srcTime.isValid())
		srcTime = srcInfo.lastModified();
		// srcTime = srcInfo.metadataChangeTime(); // Fallback to status change
	QString srcDate = srcInfo.exists() ? srcTime.toString() : tr("Unknown");

	QDateTime destTime = destInfo.birthTime();
	if (!destTime.isValid())
		destTime = destInfo.lastModified();
	QString destDate = destInfo.exists() ? destTime.toString() : tr("Unknown");

	qint64 srcSize = srcInfo.exists() ? srcInfo.size() : 0;
	qint64 destSize = destInfo.exists() ? destInfo.size() : 0;

	// Elide long paths
	QFontMetrics metrics(dialog.font());
	QString elidedSrc = metrics.elidedText(src, Qt::ElideMiddle, 800);
	QString elidedDest = metrics.elidedText(dest, Qt::ElideMiddle, 800);

	grid->addWidget(new QLabel(tr("<b>Source:</b>")), 0, 0);
	QLabel *lblSrc = new QLabel(elidedSrc);
	lblSrc->setToolTip(src);
	// Make the label selectable by mouse
	lblSrc->setTextInteractionFlags(Qt::TextSelectableByMouse);
	grid->addWidget(lblSrc, 0, 1);
	grid->addWidget(new QLabel(tr("Size: %1").arg(fmtSize(srcSize))), 1, 1);
	grid->addWidget(new QLabel(tr("Date: %1").arg(srcDate)), 2, 1);

	grid->addWidget(new QLabel(tr("<b>Destination:</b>")), 3, 0);
	QLabel *lblDest = new QLabel(elidedDest);
	lblDest->setToolTip(dest);
	// Make the label selectable by mouse
	lblDest->setTextInteractionFlags(Qt::TextSelectableByMouse);
	grid->addWidget(lblDest, 3, 1);
	grid->addWidget(new QLabel(tr("Size: %1").arg(fmtSize(destSize))), 4, 1);
	grid->addWidget(new QLabel(tr("Date: %1").arg(destDate)), 5, 1);

	layout->addLayout(grid);

	// --- Rename Input ---
	QHBoxLayout *renameLayout = new QHBoxLayout();
	renameLayout->addWidget(new QLabel(tr("Rename to:")));
	QLineEdit *renameEdit = new QLineEdit(suggestedName);
	renameLayout->addWidget(renameEdit);
	layout->addLayout(renameLayout);

	// --- Controls ---
	QCheckBox *cb = new QCheckBox(tr("Do this for all conflicts"), &dialog);
	layout->addWidget(cb);

	QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
	QPushButton *replaceBtn = buttons->addButton(tr("Replace"), QDialogButtonBox::AcceptRole);
	QPushButton *skipBtn = buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
	QPushButton *renameBtn = buttons->addButton(tr("Rename"), QDialogButtonBox::ActionRole);
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

	// Execution
	// This blocks the Main Thread (but event loop keeps running)
	// Worker Thread is waiting on m_inputWait condition
	dialog.exec();

	// Send Result Back to Worker
	// The dialog is closed, but 'renameEdit' and 'cb' are still valid
	// because they are children of 'dialog' which is still on the stack.
	m_worker->resolveConflict(action, cb->isChecked(), renameEdit->text());

	// Resume the graph timer if we are continuing
	if (action != CopyWorker::Cancel) {
		m_graphTimer->start(Config::UPDATE_INTERVAL_MS);
	}

	// End of function: 'dialog' destructor runs automatically here.
}

/*----------------------------------------------------------------
  Shows or hides the bottom panel containing the details tabs.
  Manages window resizing to expand or collapse the view.
------------------------------------------------------------------*/
void MainWindow::onToggleDetails() {
	bool isVisible = false;

	// Capture the CURRENT width so we don't lose the user's manual resizing
	int currentWidth = this->width();

	if (ui->tabWidget->isVisible()) {
		// --- HIDING (Collapsing) ---
		m_expandedHeight = this->height(); // remember the full height
		ui->tabWidget->setMinimumHeight(0);
		ui->tabWidget->hide();
		this->adjustSize(); // let the layout recalculate the new size
		this->resize(currentWidth, m_collapsedHeight);

	} else {
		// --- SHOWING (Expanding) ---
		ui->tabWidget->show();
		ui->treeWidget->header()->doItemsLayout();

		// Restore the saved height while maintaining the current width
		if (m_expandedHeight > 0){
			this->resize(currentWidth, m_expandedHeight);
		}

	}

	ui->btnShowBottomPanel->setText(isVisible ? "▲" : "▼");
}

/*----------------------------------------------------------------
  Slot that is called when the worker thread has completed all tasks.
  It finalizes the UI, saves the job history, and handles the
  auto-close feature.
------------------------------------------------------------------*/
void MainWindow::onFinished() {
	LOG(LogLevel::INFO) << "Done.";
	m_secondsLeft = 0;
	m_progress_updated = true;
	// Display source and destination folders
	m_currentDest = m_destFolder;
	m_currentFile = m_sourceFolder;
	updateProgressUi();
	m_graphTimer->stop(); // Stop the graph once finished
	updateTaskbarProgress(0); // Clear progress bar
	setWindowTitle(m_baseTitle); // Reset title to remove percentage
	ui->labelStatus->setText(tr("Done."));
	ui->btnPause->setEnabled(false);

	// Save history
	if (!m_jobHistory.isEmpty()) {
		if (m_detailsWindow) {
			QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
			m_detailsWindow->setSourceDest(m_sourceFolder, m_destFolder);
			m_detailsWindow->addHistoryEntry(currentTime, m_modeString, m_jobHistory);
			m_jobHistory.clear();
			m_loggedFiles.clear();
			// Allow user resizing
			ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Interactive);
		}
	}

	if (Config::CLOSE_ON_FINISH) {
		// This prevents the "Transfer in progress" dialog from accidentally triggering 
		// during an auto-close event due to a race condition where isRunning() might still be true
		if (m_worker) m_worker->wait();
		close();
	}
}


/*----------------------------------------------------------------
  Slot that is called each time a single file is successfully
  copied and verified. It logs the file to history and triggers
  the file manager highlight if enabled.
------------------------------------------------------------------*/
void MainWindow::onFileCompleted(QString path, QString srcHash, QString destHash, bool isTopLevel) {
	logHistory(path, "", srcHash, destHash);

	if (isTopLevel && Config::SELECT_FILES_AFTER_COPY) {
		m_topLevelItems.append(path);
		highlightFile(m_topLevelItems);
	}
}


/*----------------------------------------------------------------
  Updates the progress bar on the application's taskbar/dock icon
  using the D-Bus protocol for desktop integration.
------------------------------------------------------------------*/
void MainWindow::updateTaskbarProgress(int percent) {
	// Clamp percentage
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;

	// Unity Launcher API (Supported by Ubuntu Dock, Dash to Dock, KDE Task
	// Manager) We broadcast a signal that the Dock listens to.

	QVariantMap properties;
	properties.insert("progress", percent / 100.0);
	properties.insert("progress-visible", (percent > 0 && percent < 100));

	// URI must match the desktop file name set in main.cpp
	QString uri = "application://" + QCoreApplication::applicationName() + ".desktop";

	QDBusMessage message = QDBusMessage::createSignal(
		"/com/canonical/Unity/LauncherEntry", "com.canonical.Unity.LauncherEntry", "Update");

	message << uri << properties;
	QDBusConnection::sessionBus().send(message);
}

/*----------------------------------------------------------------
  The main UI update function, driven by a QTimer.
  It takes the latest data from member variables and updates all
  labels, progress bars, and the speed graph.
------------------------------------------------------------------*/
void MainWindow::updateProgressUi() {
	uint64_t totalBytes = 0;
	uint64_t completedBytes = 0;

	if(!m_resize_event){
		if (m_graph)
			m_graph->addSpeedPoint(m_smoothedSpeed);

		// Decay speed if no data point received
		m_smoothedSpeed *= 0.98;

	}
	
	if(!m_progress_updated) return;
	m_progress_updated = false;
	m_resize_event = false;

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

	// Status
	if (m_status_code == CopyWorker::Status::Copying){
		ui->labelStatus->setText(tr("Copying %1 of %2").arg(m_filesProcessed + 1).arg(m_totalFiles));
	} else {
		ui->labelStatus->setText(m_status_string);
	}

	// Total progress
	ui->labelProgress->setText(tr("%1% complete (%2 of %3)").arg(m_totalProgress)
								.arg(m_filesProcessed).arg(m_totalFiles));

	QString etaStr;
	if (m_secondsLeft < 0) {
		etaStr = tr("Calculating...");
	} else {
		long h = m_secondsLeft / 3600;
		long m = (m_secondsLeft % 3600) / 60;
		long s = m_secondsLeft % 60;
		etaStr = QString("%1:%2:%3")
			.arg(h, 2, 10, QChar('0'))
			.arg(m, 2, 10, QChar('0'))
			.arg(s, 2, 10, QChar('0'));
	}

	// Remaining: 00:00:00 (MB/s avg)
	ui->labelETA->setText(tr("Remaining: %1 (%2 MiB/s)")
		.arg(etaStr)
		.arg(m_avgSpeed, 0, 'f', 2)
	);

	// From and To
	// Elide file paths manually because QFontMetrics::elidedText does not support rich text.
	// We elide the path part and then construct the rich text string.
	// Get the width of the container holding the labels to determine available space
	int availableWidth = ui->verticalLayout_7->contentsRect().width();

	// Fallback for initialization phase
	if (availableWidth <= 0) {
		availableWidth = this->width() - 40;
	}

	QFontMetrics metrics(ui->labelFrom->font());
	QFont boldFont = ui->labelFrom->font();
	boldFont.setBold(true);
	QFontMetrics boldMetrics(boldFont);

	int fromPrefixWidth = boldMetrics.horizontalAdvance("From: ");
	// QString elidedFile = metrics.elidedText(m_currentFile, Qt::ElideMiddle, ui->labelFrom->width() - fromPrefixWidth - 5);
	QString elidedFile = metrics.elidedText(m_currentFile, Qt::ElideMiddle, availableWidth - fromPrefixWidth - 10);
	ui->labelFrom->setText(tr("<b>From:</b> %1").arg(elidedFile));

	int toPrefixWidth = boldMetrics.horizontalAdvance("To: ");
	// QString elidedDest = metrics.elidedText(m_currentDest, Qt::ElideMiddle, ui->labelTo->width() - toPrefixWidth - 5);
	QString elidedDest = metrics.elidedText(m_currentDest, Qt::ElideMiddle, availableWidth - toPrefixWidth - 10);
	ui->labelTo->setText(tr("<b>To:</b> %1").arg(elidedDest));

	// Transfer size: 0 MB of 0 MB
	ui->labelItems->setText(tr("%1 of %2")
		.arg(completedSizeString)
		.arg(totalSizeString)
	);

	// Current file progress
	ui->labelFileProgress->setText(QString::number(m_filePercent) + "%");

	// Update Window Title for Taskbar Progress
	setWindowTitle(QString("%1% - %2").arg(m_totalProgress).arg(m_baseTitle));

	// Update Taskbar / Dock Progress
	updateTaskbarProgress(m_totalProgress);
}


/*----------------------------------------------------------------
  Overrides the resize event to handle layout changes. It is
  currently used to re-trigger text elision logic when the
  window size changes.
------------------------------------------------------------------*/
void MainWindow::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	m_resize_event = true;
	updateProgressUi(); // Re-trigger elision with new widths
}


/*----------------------------------------------------------------
  Generates simulated data for the UI preview mode.
  Creates a sine wave pattern for the speed graph to demonstrate
  its appearance and behavior without a real file transfer.
------------------------------------------------------------------*/
void MainWindow::generateTestData() {
	static double t = 0;
	t += 0.1;
	// Generate a sine wave speed between 10 and 90 MB/s
	double speed = (sin(t) + 1.2) * 40.0;

	m_smoothedSpeed = speed;
	m_currentSpeed = speed;
	m_avgSpeed = speed;
	m_totalProgress = (static_cast<int>(t * 5) % 100);
	m_secondsLeft = 90;
	m_currentFile = "Test_File_Data.dat";
	m_currentDest = "/tmp/Test_File_Data.dat";
	m_progress_updated = true;
	updateProgressUi();
}

/*----------------------------------------------------------------
  Adds a file entry to the current job's history log.
  This data is held in memory until the job is finished, at which
  point it's saved to the details/history view.
------------------------------------------------------------------*/
void MainWindow::logHistory(
	const QString &path, const QString &error, const QString &srcHash, const QString &destHash) {
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


/*----------------------------------------------------------------
  Uses the D-Bus `org.freedesktop.FileManager1` interface to
  request that the default file manager show and select
  (highlight) the specified list of files.
------------------------------------------------------------------*/
void MainWindow::highlightFile(const QStringList &paths) {
	if (paths.isEmpty()) return;

	QStringList uris;
	uris.reserve(paths.size());
	for (const QString &path : paths) {
		uris.append(QUrl::fromLocalFile(path).toString());
	}

	QDBusMessage message = QDBusMessage::createMethodCall(
		"org.freedesktop.FileManager1",
		"/org/freedesktop/FileManager1",
		"org.freedesktop.FileManager1",
		"ShowItems"
	);

	// This is a way to get a 'legal' ID on X11/Wayland without extra libs
	WId wid = this->winId();
	QString startupId = QString("0");

	// On X11, the window ID + a timestamp often works as a token
	startupId = QString("%1_%2_desktop_file_copier").arg(wid).arg(QDateTime::currentMSecsSinceEpoch());
	// Format: <app_name>-<timestamp>-<hostname>-<wid>_TIME<timestamp>
	// QString startupId = QString("desktop-file-copier-%1-%2_TIME%2")
	// 						.arg(wid)
	// 						.arg(QDateTime::currentMSecsSinceEpoch());

	message << uris << startupId; // StartupId

	QDBusConnection::sessionBus().call(message, QDBus::NoBlock);

	// Force the window manager to remember YOU are active.
	// We use a slight delay because D-Bus is asynchronous;
	// if we call this too fast, Dolphin hasn't popped up yet.
	// QTimer::singleShot(100, this, [this]() {
	// 	this->raise();
	// 	this->activateWindow();
	// });

	// QDBusReply<void> reply = QDBusConnection::sessionBus().call(message);

	// if (!reply.isValid()) {
	// 	qDebug() << "D-Bus Error Name:" << reply.error().name();
	// 	qDebug() << "D-Bus Error Msg:" << reply.error().message();
	// } else {
	// 	qDebug() << "D-Bus call sent successfully.";
	// }
}


/*----------------------------------------------------------------
  Overrides the window's close event.
  If a transfer is in progress, it prompts the user for confirmation
  before cancelling the worker and closing the application.
------------------------------------------------------------------*/
void MainWindow::closeEvent(QCloseEvent *event) {
	LOG(LogLevel::INFO) << "Close event received.";

	if (m_worker && m_worker->isRunning()) {
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, tr("Confirm Exit"), tr("A file transfer is in progress.\nAre you sure you want to cancel the transfer and exit?"), QMessageBox::Yes | QMessageBox::No);

		if (reply == QMessageBox::No) {
			event->ignore();
			return;
		}

		// Update UI to show we are stopping
		ui->labelStatus->setText(tr("Stopping and removing partial files..."));

		// Signal the thread to stop
		LOG(LogLevel::INFO) << "Cancelling copy worker.";
		m_worker->cancel();

		// Wait for the thread to finish safely.
		// This ensures the worker cleans up files and exits run() before we
		// destroy it.
		LOG(LogLevel::INFO) << "Waiting for copy worker to finish.";
		m_worker->wait();
	}

	event->accept();
}
