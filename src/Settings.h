#pragma once

#include <QWidget>

namespace Ui {
	class Settings;
}

class MainWindow;

class Settings : public QWidget {
	Q_OBJECT

public:
	explicit Settings(QWidget *parent = nullptr);
	~Settings();

private:
	Ui::Settings *ui;
	MainWindow *m_previewWindow = nullptr;

	void onTestModeToggled(bool checked);
	void updatePreview();
};