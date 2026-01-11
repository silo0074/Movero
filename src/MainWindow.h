#pragma once

#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QTimer>

#include "CopyWorker.h"

class SpeedGraph : public QWidget {  // Inherits from QWidget
    Q_OBJECT  // Enables Qt meta-object features
public:
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
    double m_maxSpeed;
    double m_peakSpeed = 0.0;
    bool m_isPaused = false;
};


class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(const QString& mode, const std::vector<std::string>& sources, const std::string& dest, QWidget *parent = nullptr);

private slots:
    void onTogglePause();
    void onUpdateProgress(QString file, int percent, double curSpeed, double avgSpeed, QString eta);
    void onError(CopyWorker::FileError err);
    void onFinished();

private:
    CopyWorker* m_worker;
    SpeedGraph* m_graph;
    QLabel* m_statusLabel;
    QLabel* m_speedLabel;
    QLabel* m_statusActionLabel;
    QProgressBar* m_fileProgress;
    QProgressBar* m_totalProgress;
    QPushButton* m_pauseBtn;
    QPushButton* m_cancelBtn;
    QListWidget* m_errorList;
    QTimer* m_graphTimer;      // Manages the steady 100ms graph updates
    double m_smoothedSpeed;    // Holds the EMA (Exponential Moving Average) smoothing filtered speed value
    bool m_isPaused;
};
