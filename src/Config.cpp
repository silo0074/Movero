#include "Config.h"
#include <QSettings>

namespace Config {
	void load() {
		QSettings s;
		LOG_HISTORY_ENABLED = s.value("logHistory", true).toBool();
		CHECKSUM_ENABLED = s.value("checksumEnabled", true).toBool();
		SPEED_GRAPH_SHOW_TIME_LABELS = s.value("graphShowTime", true).toBool();
		SPEED_GRAPH_ALIGN_LABELS_RIGHT = s.value("graphAlignRight", false).toBool();
		SPEED_GRAPH_HISTORY_SIZE = s.value("graphHistorySize", 200).toInt();
		SPEED_GRAPH_MAX_SPEED = s.value("graphMaxSpeed", 10.0).toDouble();
		CLOSE_ON_FINISH = s.value("closeOnFinish", false).toBool();
	}

	void save() {
		QSettings s;
		s.setValue("logHistory", LOG_HISTORY_ENABLED);
		s.setValue("checksumEnabled", CHECKSUM_ENABLED);
		s.setValue("graphShowTime", SPEED_GRAPH_SHOW_TIME_LABELS);
		s.setValue("graphAlignRight", SPEED_GRAPH_ALIGN_LABELS_RIGHT);
		s.setValue("graphHistorySize", SPEED_GRAPH_HISTORY_SIZE);
		s.setValue("graphMaxSpeed", SPEED_GRAPH_MAX_SPEED);
		s.setValue("closeOnFinish", CLOSE_ON_FINISH);
	}
} // namespace Config