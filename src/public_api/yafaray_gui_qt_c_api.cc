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

#include "public_api/yafaray_gui_qt_c_api.h"
#include "common/version_build_info.h"
#include "gui/mywindow.h"
#include <QApplication>

static QApplication *app_global = nullptr;

void yafaray_gui_qt_init()
{
	static int argc = 0;

	if(!QApplication::instance())
	{
#if defined(__APPLE__)
		QApplication::instance()->setAttribute(Qt::AA_MacPluginApplication);
		QApplication::instance()->setAttribute(Qt::AA_DontUseNativeMenuBar);
#endif
		app_global = new QApplication(argc, nullptr);
	}
	else app_global = dynamic_cast<QApplication *>(QApplication::instance());
}

int yafaray_gui_qt_createRenderWidget(yafaray_Interface_t *yafaray_interface, int xsize, int ysize, int b_start_x, int b_start_y, bool auto_save, bool close_after_finish)
{
	yafaray_gui_qt::MainWindow w(yafaray_interface, xsize, ysize, b_start_x, b_start_y, auto_save, close_after_finish);
	w.show();
	w.adjustWindow();
	//FIXME AUTORENDER w.slotRender();
	return app_global->exec();
}

void yafaray_gui_qt_getVersionString(char *dest_string, unsigned int dest_string_size)
{
	if(!dest_string || dest_string_size == 0) return;
	const std::string version_string = yafaray_gui_qt::buildinfo::getVersionString();
	const unsigned int copy_length = std::min(dest_string_size - 1, static_cast<unsigned int>(version_string.size()));
	strncpy(dest_string, version_string.c_str(), copy_length);
	*(dest_string + copy_length) = 0x00; //Make sure that the destination string gets null terminated
}

int yafaray_gui_qt_getVersionMajor() { return yafaray_gui_qt::buildinfo::getVersionMajor(); }
int yafaray_gui_qt_getVersionMinor() { return yafaray_gui_qt::buildinfo::getVersionMinor(); }
int yafaray_gui_qt_getVersionPatch() { return yafaray_gui_qt::buildinfo::getVersionPatch(); }
