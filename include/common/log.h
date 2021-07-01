#pragma once
/****************************************************************************
 *      This is part of the libYafaRay-Gui-Qt package
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef LIBYAFARAY_GUI_QT_LOG_ENTRY_H
#define LIBYAFARAY_GUI_QT_LOG_ENTRY_H

#include "common/yafaray_gui_qt_common.h"
#include <yafaray_c_api.h>
#include <string>
#include <vector>

BEGIN_YAFARAY_GUI_QT

class LogEntry final
{
	public:
		LogEntry(yafaray_LogLevel_t log_level, long datetime, const char *time_of_day, const char *description) : log_level_(log_level), datetime_(datetime), time_of_day_(time_of_day), description_(description) { }
		yafaray_LogLevel_t getLogLevel() const { return log_level_; }
		long getDateTime() const { return datetime_; }
		std::string getTimeOfDay() const { return time_of_day_; }
		std::string getDescription() const { return description_; }

	private:
		yafaray_LogLevel_t log_level_ = YAFARAY_LOG_LEVEL_MUTE;
		long datetime_ = 0;
		std::string time_of_day_;
		std::string description_;
};

class Log final
{
	public:
		void append(const LogEntry &log_entry) { data_.emplace_back(log_entry); }
		std::vector<LogEntry> getLog() const { return data_; }
		static void loggerCallback(yafaray_LogLevel_t log_level, long datetime, const char *time_of_day, const char *description, void *callback_user_data);

	private:
		std::vector<LogEntry> data_;
};

END_YAFARAY_GUI_QT

#endif //LIBYAFARAY_GUI_QT_LOG_ENTRY_H
