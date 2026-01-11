#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

#include "MainWindow.h"
#include "Config.h"

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


// void SpeedGraph::paintEvent(QPaintEvent*) {
//     QPainter p(this);
//     p.setRenderHint(QPainter::Antialiasing);

//     int w = width();
//     int h = height();
//     int padding = 20; // Room at the top for the line/label
//     int bottomMargin = 25; // Extra space for time labels
//     double step = static_cast<double>(w) / (m_history.size() - 1);

//     // Draw Background Grid
//     p.setPen(QPen(Qt::gray, 1));
//     // p.setPen(QPen(QColor(50, 50, 50), 1));
//     for(int i=0; i<=4; ++i) {
//         int y = padding + ((h - padding*2) / 4) * i;
//         p.drawLine(0, y, w, y);
//     }

//     // Draw the Time Scale (X-Axis Labels)
//     p.setPen(QPen(QColor(150, 150, 150), 1));
//     QFont font = p.font();
//     font.setPointSize(8);
//     p.setFont(font);

//     // Points per second = 10 (since timer is 100ms)
//     const int pointsPerSecond = 10;

//     // We want to draw labels at specific intervals (e.g., every 20 seconds)
//     const int intervalSeconds = 20;
//     const int pointsPerInterval = pointsPerSecond * intervalSeconds;

//     for (int i = m_history.size() - 1; i >= 0; i -= pointsPerInterval) {
//         // Calculate X position
//         double x = i * step;

//         // Calculate how many seconds ago this was
//         int secondsAgo = (m_history.size() - 1 - i) / pointsPerSecond;

//         if (secondsAgo == 0) continue; // Skip the "now" label to keep it clean

//         QString label = QString("-%1s").arg(secondsAgo);

//         // Draw a small vertical tick mark
//         p.drawLine(x, h - bottomMargin, x, h - bottomMargin + 5);

//         // Draw the text centered under the tick
//         float textWidth = p.fontMetrics().horizontalAdvance(label);
//         p.drawText(x - (textWidth / 2), h - 5, label);
//     }

//     // Create the Path
//     QPainterPath path;
//     path.moveTo(0, h);
//     for (size_t i = 0; i < m_history.size(); ++i) {
//         double x = i * step;
//         // Map speed to Y: 0 speed = height(), max speed = padding
//         double y = h - ((m_history[i] / m_maxSpeed) * (h - padding*2));
//         path.lineTo(x, y);
//     }

//     // Draw Fill (Gradient)
//     QPainterPath fillPath = path;
//     QLinearGradient gradient(0, 0, 0, height());
//     gradient.setColorAt(0, QColor(0, 255, 0, 150)); // Bright green at top
//     gradient.setColorAt(1, QColor(0, 255, 0, 20));  // Faded green at bottom
//     p.fillPath(path, gradient);
//     fillPath.lineTo(w, h);
//     fillPath.lineTo(0, h);
//     p.fillPath(fillPath, QColor(0, 255, 0, 60));

//     // Draw Main Speed Line
//     p.setPen(QPen(QColor(0, 0, 0), 1));
//     p.drawPath(path);

//     // Draw THE HORIZONTAL LINE (Current Speed)
//     // We draw this LAST so it is on top of everything
//     double currentSpeedY = h - ((m_history.back() / m_maxSpeed) * (h - padding*2));

//     p.setPen(QPen(Qt::black, 2, Qt::SolidLine));
//     p.setOpacity(1.0);
//     p.drawLine(0, currentSpeedY, w, currentSpeedY);

//     // Optional: Draw a small speed label next to the line
//     p.drawText(w - 70, currentSpeedY - 5, QString::number(m_history.back(), 'f', 1) + " MB/s");
// }


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
    const int intervalSeconds = 20;
    const int pointsPerInterval = pointsPerSecond * intervalSeconds;
    
    for (int i = m_history.size() - 1; i >= 0; i -= pointsPerInterval) {
        double x = gridRect.left() + (i * step);
        // int secondsAgo = (m_history.size() - 1 - i) / pointsPerSecond;
        int secondsAgo = static_cast<int>((m_history.size() - 1 - i) / pointsPerSecond);
        
        if (secondsAgo == 0) continue; 
        
        QString timeLabel = QString("-%1s").arg(secondsAgo);
        
        p.setPen(Qt::gray);
        // Draw tick
        p.drawLine(x, gridRect.bottom(), x, gridRect.bottom() + 5);
        // Draw text centered under tick
        float textWidth = p.fontMetrics().horizontalAdvance(timeLabel);
        p.drawText(x - (textWidth / 2), h - 5, timeLabel);
    }
    
    // 4. Create and Draw the Path (Data)
    if (m_history.size() > 1) {
        QPainterPath path;
        bool started = false;
        
        for (size_t i = 0; i < m_history.size(); ++i) {
            double x = gridRect.left() + (i * step);
            // Map speed to Y: 0 speed = gridRect.bottom(), max = gridRect.top()
            // double y = gridRect.bottom() - ((m_history[i] / m_maxSpeed) * gridRect.height());
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
: QWidget(parent), m_isPaused(false), m_smoothedSpeed(0.0){

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

    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onTogglePause);
    connect(m_cancelBtn, &QPushButton::clicked, [this](){ m_worker->cancel(); close(); });

    // m_graphTimer = new QTimer(this);
    // connect(m_graphTimer, &QTimer::timeout, this, [this]() {
    //     // The graph now rolls at a constant 10Hz regardless of disk latency
    //     m_graph->addSpeedPoint(m_smoothedSpeed);
        
    //     // If the worker hasn't sent an update in a while, 
    //     // we slowly decay the speed so the graph drops to 0
    //     m_smoothedSpeed *= 0.9; 
    // });
    // m_graphTimer->start(Config::UPDATE_INTERVAL_MS); // 10 updates per second

    m_worker->start();
}

void MainWindow::onTogglePause() {
    if (m_isPaused) {
        m_worker->resume();
        m_graph->setPaused(false);
        // m_graphTimer->start(100); // Restart the graph movement
        m_pauseBtn->setText("Pause");
    } else {
        m_worker->pause();
        m_graph->setPaused(true);
        // m_graphTimer->stop();     // Freeze the graph movement
        m_pauseBtn->setText("Resume");
    }
    m_isPaused = !m_isPaused;
}


void MainWindow::onUpdateProgress(QString file, int percent, double curSpeed, double avgSpeed, QString eta) {
    m_statusLabel->setText("File: " + file + " AvgSpeed: " + QString::number(avgSpeed) + " ETA: " + eta);
    m_fileProgress->setValue(percent);

    if (curSpeed > 0) {
        // smoothing factor: 0.1 (lower = smoother/slower, higher = jumpier/faster)
        // m_smoothedSpeed = (m_smoothedSpeed * 0.9) + (curSpeed * 0.1);
        m_smoothedSpeed = (m_smoothedSpeed * 0.5) + (curSpeed * 0.5);
    
        // Trigger graph update only when fresh data arrives (every 0.5s)
        m_graph->addSpeedPoint(m_smoothedSpeed);
        m_speedLabel->setText(QString::number(m_smoothedSpeed, 'f', 1) + " MB/s");
    }
}


void MainWindow::onError(CopyWorker::FileError err) {
    // m_graphTimer->stop(); // Stop the graph
    m_errorList->setHidden(false);
    m_errorList->addItem(err.path + ": " + err.errorMsg);
    m_errorList->setStyleSheet("border: 1px solid red;");
}

void MainWindow::onFinished() {
    // m_graphTimer->stop(); // Stop the graph once finished
    m_statusLabel->setText("Operation Complete.");
    m_statusActionLabel->setText("Done.");
    m_pauseBtn->setEnabled(false);
    m_cancelBtn->setText("Close");
}
