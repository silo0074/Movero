#include "Config.h"
#include "LogHelper.h"
#include <QSettings>

namespace Config {
	void load() {
		QSettings s;
		LOG(LogLevel::DEBUG) << "Loading settings: " << s.fileName();
		LOG_HISTORY_ENABLED = s.value("logHistory", Defaults::LOG_HISTORY_ENABLED).toBool();
		CHECKSUM_ENABLED = s.value("checksumEnabled", Defaults::CHECKSUM_ENABLED).toBool();
		COPY_FILE_MODIFICATION_TIME = s.value("copyFileModTime", Defaults::COPY_FILE_MODIFICATION_TIME).toBool();
		SANITIZE_FILENAMES = s.value("sanitizeFilenames", Defaults::SANITIZE_FILENAMES).toBool();
		SPEED_GRAPH_SHOW_TIME_LABELS = s.value("graphShowTime", Defaults::SPEED_GRAPH_SHOW_TIME_LABELS).toBool();
		SPEED_GRAPH_ALIGN_LABELS_RIGHT = s.value("graphAlignRight", Defaults::SPEED_GRAPH_ALIGN_LABELS_RIGHT).toBool();
		SPEED_GRAPH_HISTORY_SIZE_USER = s.value("graphHistorySize", Defaults::SPEED_GRAPH_HISTORY_SIZE_USER).toInt();
		SPEED_GRAPH_HISTORY_SIZE = SPEED_GRAPH_HISTORY_SIZE_USER;
		SPEED_GRAPH_MAX_SPEED = s.value("graphMaxSpeed", Defaults::SPEED_GRAPH_MAX_SPEED).toDouble();
		CLOSE_ON_FINISH = s.value("closeOnFinish", Defaults::CLOSE_ON_FINISH).toBool();
		UI_STYLE = s.value("uiStyle", "").toString();
		LANGUAGE = s.value("language", Defaults::LANGUAGE).toString();
	}

	void save() {
		QSettings s;
		LOG(LogLevel::DEBUG) << "Saving settings: " << s.fileName();
		s.setValue("logHistory", LOG_HISTORY_ENABLED);
		s.setValue("checksumEnabled", CHECKSUM_ENABLED);
		s.setValue("copyFileModTime", COPY_FILE_MODIFICATION_TIME);
		s.setValue("sanitizeFilenames", SANITIZE_FILENAMES);
		s.setValue("graphShowTime", SPEED_GRAPH_SHOW_TIME_LABELS);
		s.setValue("graphAlignRight", SPEED_GRAPH_ALIGN_LABELS_RIGHT);
		s.setValue("graphHistorySize", SPEED_GRAPH_HISTORY_SIZE_USER);
		s.setValue("graphMaxSpeed", SPEED_GRAPH_MAX_SPEED);
		s.setValue("closeOnFinish", CLOSE_ON_FINISH);
		s.setValue("uiStyle", UI_STYLE);
		s.setValue("language", LANGUAGE);
	}
} // namespace Config