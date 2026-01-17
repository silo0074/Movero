#pragma once

#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QCloseEvent>

#include "CopyWorker.h"

class SpeedGraph : public QWidget {  // Inherits from QWidget
    Q_OBJECT  // Enables Qt meta-object features
public:

    bool m_update_graph = true;
    // Without explicit, these might be allowed:
    // SpeedGraph graph = someWidget;  // Implicit conversion
    // With explicit, only direct initialization is allowed
    // SpeedGraph graph(someWidget);
    explicit SpeedGraph(QWidget* parent = nullptr);
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
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<double> m_history;
    QMutex m_mutex;
    double m_maxSpeed;
    bool m_isPaused = false;

};


namespace Ui {
class MainWindow;
}

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(const QString& mode, const std::vector<std::string>& sources, const std::string& dest, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onStatusChanged(CopyWorker::Status status);
    void onTotalProgress(int fileCount, int totalFiles);
    void onTogglePause();
    void onUpdateProgress(QString file, int percent, int totalPercent, double curSpeed, double avgSpeed, QString eta);
    void onError(CopyWorker::FileError err);
    void onFinished();
    void onConflictNeeded(QString src, QString dest, QString suggestedName);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateTaskbarProgress(int percent);
    Ui::MainWindow *ui;
    CopyWorker* m_worker;
    SpeedGraph* m_graph;
    QString m_status;
    QString m_currentFile;

    // Manages the steady 100ms graph updates
    QTimer* m_graphTimer;

    QString m_eta;
    QString m_baseTitle;

    // Holds the EMA (Exponential Moving Average) smoothing filtered speed value
    double m_smoothedSpeed;

    // std::atomic<double> m_smoothedSpeed{0.0};
    std::atomic<double> m_currentSpeed{0.0};
    double m_avgSpeed;
    int m_totalProgress;
    int m_filePercent;
    int m_totalFiles;
    int m_filesRemaining;
    bool m_isPaused;
};
