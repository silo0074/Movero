#include "Settings.h"
#include "Config.h"
#include "ui_Settings.h"

Settings::Settings(QWidget *parent) : QDialog(parent),
				      ui(new Ui::Settings) {
	ui->setupUi(this);

	// Load values from Config
	ui->checkLogHistory->setChecked(Config::LOG_HISTORY_ENABLED);
	ui->checkChecksum->setChecked(Config::CHECKSUM_ENABLED);
	ui->checkCloseOnFinish->setChecked(Config::CLOSE_ON_FINISH);
	ui->checkTimeLabels->setChecked(Config::SPEED_GRAPH_SHOW_TIME_LABELS);
	ui->checkAlignRight->setChecked(Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT);
	ui->spinHistorySize->setValue(Config::SPEED_GRAPH_HISTORY_SIZE);
	ui->spinMaxSpeed->setValue(Config::SPEED_GRAPH_MAX_SPEED);

	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
		// Save values to Config
		Config::LOG_HISTORY_ENABLED = ui->checkLogHistory->isChecked();
		Config::CHECKSUM_ENABLED = ui->checkChecksum->isChecked();
		Config::CLOSE_ON_FINISH = ui->checkCloseOnFinish->isChecked();
		Config::SPEED_GRAPH_SHOW_TIME_LABELS = ui->checkTimeLabels->isChecked();
		Config::SPEED_GRAPH_ALIGN_LABELS_RIGHT = ui->checkAlignRight->isChecked();
		Config::SPEED_GRAPH_HISTORY_SIZE = ui->spinHistorySize->value();
		Config::SPEED_GRAPH_MAX_SPEED = ui->spinMaxSpeed->value();

		Config::save();
		accept();
	});
	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

Settings::~Settings() {
	delete ui;
}