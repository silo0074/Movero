#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QFileInfo>

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Config.h"
#include "LogHelper.h"

// --- SpeedGraph Implementation ---
// Initializes members BEFORE constructor body executes
// More efficient than assignment inside constructor body
SpeedGraph::SpeedGraph(QWidget* parent) 
    : QWidget(parent),                              // Base class initialization
    m_maxSpeed(Config::SPEED_GRAPH_MAX_SPEED)     // Member initialization
{
    // Constructor body
    setMinimumHeight(Config::SPEED_GRAPH_MIN_HEIGHT);
    m_history.resize(Config::SPEED_GRAPH_HISTORY_SIZE, 0.0);
}


void SpeedGraph::addSpeedPoint(double mbps) {
    if (mbps > m_peakSpeed) {
        m_peakSpeed = mbps;
    }

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
        if (s > targetMax) targetMax = s;
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


void SpeedGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Color definitions based on state
    QColor mainColor = m_isPaused ? QColor(255, 140, 0) : QColor(0, 180, 0);       // Orange vs Green
    QColor gradientTop = m_isPaused ? QColor(255, 165, 0, 100) : QColor(0, 255, 0, 100);

    // 1. Define margins for labels
    const int leftMargin = 70;    // Space for "300 MiB/s" labels
    const int rightMargin = 20;   
    const int topMargin = 20;
    const int bottomMargin = 30;  // Space for "-20s" labels

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
    
    // 2. Draw Horizontal Grid Lines and Speed Labels
    p.setFont(QFont("Arial", 8));

    for(int i = 0; i <= 4; ++i) {
        int y = gridRect.top() + (gridRect.height() / 4) * i;
        
        // Draw Grid Line
        p.setPen(QPen(QColor(200, 200, 200, 100), 1, Qt::DashLine));
        p.drawLine(gridRect.left(), y, gridRect.right(), y);
        
        // Calculate Speed for this line (i=0 is Top/Max, i=4 is Bottom/0)
        double speedAtLine = m_maxSpeed * (4 - i) / 4.0;
        QString speedLabel = formatSpeed(speedAtLine);

        // Draw Speed Label on the left
        p.setPen(Qt::gray);
        p.drawText(5, y + 4, speedLabel); 
    }
    
    // 3. Draw Time Scale (X-Axis Labels)
    // Calculate total duration based on the actual update frequency of the UI graph
    double totalSeconds = ((m_history.size() - 1) * Config::UPDATE_INTERVAL_MS) / 1000.0;
    if (totalSeconds <= 0) totalSeconds = 1.0;

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

    p.setPen(Qt::gray);

    // Helper lambda to draw a tick and label
    auto drawTick = [&](double timeVal) {
        double x = gridRect.right() - (timeVal * pixelsPerSecond);
        
        // Draw tick
        p.drawLine(x, gridRect.bottom(), x, gridRect.bottom() + 5);

        // Format Label
        QString timeLabel;
        int tInt = static_cast<int>(timeVal + 0.5);
        
        if (timeVal < 0.1) timeLabel = "0s";
        else if (tInt < 60) timeLabel = QString("-%1s").arg(tInt);
        else {
            int m = tInt / 60;
            int s = tInt % 60;
            if (s == 0) timeLabel = QString("-%1m").arg(m);
            else timeLabel = QString("-%1m %2s").arg(m).arg(s);
        }

        // Draw Text
        float textWidth = p.fontMetrics().horizontalAdvance(timeLabel);
        double textX = x - (textWidth / 2);

        // Clamp to widget bounds to ensure 0s and Max are visible
        if (textX + textWidth > w) textX = w - textWidth - 2;
        if (textX < 0) textX = 2;

        p.drawText(textX, h - 5, timeLabel);
    };

    // 1. Always draw the Max History label (Leftmost)
    drawTick(totalSeconds);

    // 2. Draw 0s and intermediates
    // Stop if we get too close to the Max label (approx 50px clearance) to avoid overlap
    double leftThreshold = gridRect.left() + 50;

    for (int t = 0; t < static_cast<int>(totalSeconds); t += intervalSeconds) {
        double x = gridRect.right() - (t * pixelsPerSecond);
        if (x < leftThreshold) break;
        drawTick(static_cast<double>(t));
    }
    
    // 4. Create and Draw the Path (Data)
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

    // 5. Draw Current Speed Indicator (Dash line if paused)
    double currentY = gridRect.bottom() - ((m_history.back() / m_maxSpeed) * gridRect.height());
    Qt::PenStyle lineStyle = m_isPaused ? Qt::DashLine : Qt::SolidLine;
    p.setPen(QPen(m_isPaused ? Qt::red : Qt::black, 1, lineStyle));
    p.drawLine(gridRect.left(), currentY, gridRect.right(), currentY);

    // Draw Peak Speed Line (Thin dashed red line)
    // double peakY = gridRect.bottom() - ((m_peakSpeed / m_maxSpeed) * gridRect.height());
    // p.setPen(QPen(QColor(255, 0, 0, 100), 1, Qt::DashLine));
    // p.drawLine(gridRect.left(), peakY, gridRect.right(), peakY);
    // p.drawText(gridRect.left() + 5, peakY - 2, "Peak: " + formatSpeed(m_peakSpeed));
}


// Helper to format labels cleanly
QString SpeedGraph::formatSpeed(double mbps) {
    if (mbps >= 1024) return QString::number(mbps / 1024.0, 'f', 1) + " GiB/s";
    if (mbps >= 1) return QString::number(mbps, 'f', 1) + " MiB/s";
    return QString::number(mbps * 1024.0, 'f', 0) + " KiB/s";
}


// --- MainWindow Implementation ---
MainWindow::MainWindow(const QString& mode, const std::vector<std::string>& sources, const std::string& dest, QWidget *parent)
: QWidget(parent), ui(new Ui::MainWindow), m_isPaused(false), m_smoothedSpeed(0.0){

    ui->setupUi(this);

    setWindowTitle("FastCopier - " + mode);
    resize(500, 450);

    auto layout = new QVBoxLayout(this);

    m_statusActionLabel = new QLabel("Initializing...");
    m_statusLabel = new QLabel("Preparing...");
    m_graph = new SpeedGraph(this);
    m_fileProgress = new QProgressBar();
    m_totalProgress = new QProgressBar();
    m_speedLabel = new QLabel("0 MB/s");

    m_errorList = new QListWidget();
    m_errorList->setMaximumHeight(80);
    m_errorList->setHidden(true);

    auto btnLayout = new QHBoxLayout();
    m_pauseBtn = new QPushButton("Pause");
    m_cancelBtn = new QPushButton("Cancel");
    btnLayout->addWidget(m_pauseBtn);
    btnLayout->addWidget(m_cancelBtn);

    layout->addWidget(m_statusActionLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_graph);
    layout->addWidget(m_speedLabel);
    layout->addWidget(m_fileProgress);
    layout->addWidget(m_totalProgress);
    layout->addWidget(m_errorList);
    layout->addLayout(btnLayout);

    CopyWorker::Mode workerMode = (mode == "mv") ? CopyWorker::Move : CopyWorker::Copy;
    m_worker = new CopyWorker(sources, dest, workerMode, this);

    connect(m_worker, &CopyWorker::progressChanged, this, &MainWindow::onUpdateProgress);
    connect(m_worker, &CopyWorker::statusChanged, m_statusActionLabel, &QLabel::setText);
    connect(m_worker, &CopyWorker::totalProgress, m_totalProgress, &QProgressBar::setValue);
    connect(m_worker, &CopyWorker::totalProgress, [&](int val, int max){ m_totalProgress->setMaximum(max); });
    connect(m_worker, &CopyWorker::errorOccurred, this, &MainWindow::onError);
    connect(m_worker, &CopyWorker::finished, this, &MainWindow::onFinished);
    connect(m_worker, &CopyWorker::conflictNeeded, this, &MainWindow::onConflictNeeded);

    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onTogglePause);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::close);

    m_graphTimer = new QTimer(this);
    connect(m_graphTimer, &QTimer::timeout, this, [this]() {
        m_statusLabel->setText("File: " + m_currentFile + " AvgSpeed: " + QString::number(m_avgSpeed) + " ETA: " + m_eta);
        m_fileProgress->setValue(m_filePercent);
        m_speedLabel->setText(QString::number(m_smoothedSpeed, 'f', 1) + " MB/s");

        // The graph now rolls at a constant 10Hz regardless of disk latency
        m_graph->addSpeedPoint(m_smoothedSpeed);
        
        // If the worker hasn't sent an update in a while, 
        // we slowly decay the speed so the graph drops to 0
        m_smoothedSpeed *= 0.9; 
    });
    m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // 10 updates per second

    m_worker->start();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    LOG(LogLevel::INFO) << "Close event received.";

    if (m_worker->isRunning()) {
        // Update UI to show we are stopping
        m_statusActionLabel->setText("Stopping...");
        m_statusLabel->setText("Cleaning up...");
        
        // Signal the thread to stop
        LOG(LogLevel::INFO) << "Cancelling copy worker.";
        m_worker->cancel();
        
        // Wait for the thread to finish safely. 
        // This ensures the worker cleans up files and exits run() before we destroy it.
        LOG(LogLevel::INFO) << "Waiting for copy worker to finish.";
        m_worker->wait();
    }
    event->accept();
}

void MainWindow::onTogglePause() {
    if (m_isPaused) {
        m_worker->resume();
        m_graph->setPaused(false);
        m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // Restart the graph movement
        m_pauseBtn->setText("Pause");
    } else {
        m_worker->pause();
        m_graph->setPaused(true);
        m_graphTimer->stop();     // Freeze the graph movement
        m_pauseBtn->setText("Resume");
    }
    m_isPaused = !m_isPaused;
}


void MainWindow::onUpdateProgress(QString file, int percent, double curSpeed, double avgSpeed, QString eta) {
    m_currentFile = file;
    m_filePercent = percent;
    m_currentSpeed = curSpeed;
    m_avgSpeed = avgSpeed;
    m_eta = eta;

    // m_statusLabel->setText("File: " + file + " AvgSpeed: " + QString::number(avgSpeed) + " ETA: " + eta);
    // m_fileProgress->setValue(percent);

    if (curSpeed > 0) {
    //     // smoothing factor: 0.1 (lower = smoother/slower, higher = jumpier/faster)
    //     // m_smoothedSpeed = (m_smoothedSpeed * 0.9) + (curSpeed * 0.1);
        m_smoothedSpeed = (m_smoothedSpeed * 0.5) + (curSpeed * 0.5);
    
    //     // Trigger graph update only when fresh data arrives (every 0.5s)
    //     // m_graph->addSpeedPoint(m_smoothedSpeed);
    //     m_speedLabel->setText(QString::number(m_smoothedSpeed, 'f', 1) + " MB/s");
    }
}


void MainWindow::onError(CopyWorker::FileError err) {
    m_graphTimer->stop(); // Stop the graph
    m_errorList->setHidden(false);
    m_errorList->addItem(err.path + ": " + err.errorMsg);
    m_errorList->setStyleSheet("border: 1px solid red;");
}

void MainWindow::onConflictNeeded(QString src, QString dest, QString suggestedName) {
    QDialog dialog(this);
    dialog.setWindowTitle("File Conflict");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    layout->addWidget(new QLabel("Destination file already exists. Select an action:", &dialog));
    
    // --- Details Grid ---
    QGridLayout* grid = new QGridLayout();
    QFileInfo srcInfo(src);
    QFileInfo destInfo(dest);
    
    auto fmtSize = [](qint64 s) {
        if (s > 1024*1024*1024) return QString::number(s/(1024.0*1024*1024), 'f', 2) + " GB";
        if (s > 1024*1024) return QString::number(s/(1024.0*1024), 'f', 2) + " MB";
        return QString::number(s/1024.0, 'f', 2) + " KB";
    };
    
    grid->addWidget(new QLabel("<b>Source:</b>"), 0, 0);
    grid->addWidget(new QLabel(src), 0, 1);
    grid->addWidget(new QLabel(QString("Size: %1").arg(fmtSize(srcInfo.size()))), 1, 1);
    grid->addWidget(new QLabel(QString("Date: %1").arg(srcInfo.lastModified().toString())), 2, 1);
    
    grid->addWidget(new QLabel("<b>Destination:</b>"), 3, 0);
    grid->addWidget(new QLabel(dest), 3, 1);
    grid->addWidget(new QLabel(QString("Size: %1").arg(fmtSize(destInfo.size()))), 4, 1);
    grid->addWidget(new QLabel(QString("Date: %1").arg(destInfo.lastModified().toString())), 5, 1);
    
    layout->addLayout(grid);
    
    // --- Rename Input ---
    QHBoxLayout* renameLayout = new QHBoxLayout();
    renameLayout->addWidget(new QLabel("Rename to:"));
    QLineEdit* renameEdit = new QLineEdit(suggestedName);
    renameLayout->addWidget(renameEdit);
    layout->addLayout(renameLayout);
    
    // --- Controls ---
    QCheckBox *cb = new QCheckBox("Do this for all conflicts", &dialog);
    layout->addWidget(cb);
    
    QDialogButtonBox* buttons = new QDialogButtonBox();
    QPushButton *replaceBtn = buttons->addButton("Replace", QDialogButtonBox::AcceptRole);
    QPushButton *skipBtn = buttons->addButton("Skip", QDialogButtonBox::RejectRole);
    QPushButton *renameBtn = buttons->addButton("Rename", QDialogButtonBox::ActionRole);
    QPushButton *cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    
    CopyWorker::ConflictAction action = CopyWorker::Cancel;
    
    connect(replaceBtn, &QPushButton::clicked, [&](){ action = CopyWorker::Replace; dialog.accept(); });
    connect(skipBtn, &QPushButton::clicked, [&](){ action = CopyWorker::Skip; dialog.accept(); });
    connect(renameBtn, &QPushButton::clicked, [&](){ action = CopyWorker::Rename; dialog.accept(); });
    connect(cancelBtn, &QPushButton::clicked, [&](){ action = CopyWorker::Cancel; dialog.reject(); });
    
    dialog.exec();
    
    m_worker->resolveConflict(action, cb->isChecked(), renameEdit->text());
}

void MainWindow::onFinished() {
    m_graphTimer->stop(); // Stop the graph once finished
    m_statusLabel->setText("Operation Complete.");
    m_statusActionLabel->setText("Done.");
    m_pauseBtn->setEnabled(false);
    m_cancelBtn->setText("Close");
}
