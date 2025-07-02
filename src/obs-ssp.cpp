/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include "ssp-mdns.h"
#include "ssp-dock.h"
#include "camera-status-manager.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <QDir>
#include <QAction>
#include <QMainWindow>
#include <QMessageBox>
//#include <QApplication>
#include "obs-ssp.h"
#include "ssp-controller.h"
#include "ssp-toolbar.h"
#if defined(__APPLE__)
#include <dlfcn.h>
#endif

// Forward declaration of functions
static bool check_browser_module_available();
static bool check_obs_version_compatibility();

// Minimum OBS version required for this plugin (from buildspec.json)
#define MIN_OBS_VERSION_MAJOR 31
#define MIN_OBS_VERSION_MINOR 0
#define MIN_OBS_VERSION_PATCH 0

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Yibai Zhang")
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ssp", "en-US")

extern struct obs_source_info create_ssp_source_info();
struct obs_source_info ssp_source_info;

create_ssp_class_ptr create_ssp_class;
create_loop_class_ptr create_loop_class;

static SspDock *ssp_dock = nullptr;

static void show_ssp_dock(void *data, obs_data_t *settings)
{
	if (!ssp_dock) {
		QMainWindow *main_window =
			(QMainWindow *)obs_frontend_get_main_window();
		ssp_dock = new SspDock(main_window);
		main_window->addDockWidget(Qt::LeftDockWidgetArea, ssp_dock);
		ssp_dock->setFloating(true);
	}
	ssp_dock->show();
	ssp_dock->raise();
	ssp_dock->activateWindow();
}

bool obs_module_load(void)
{
	ssp_blog(LOG_INFO, "hello ! (obs-ssp version %s) size: %lu",
		 PLUGIN_VERSION, sizeof(ssp_source_info));

	// Check if OBS version is compatible
	if (!check_obs_version_compatibility()) {
		// Display a warning message to the user
		QMainWindow *main_window =
			(QMainWindow *)obs_frontend_get_main_window();
		QMetaObject::invokeMethod(
			main_window,
			[main_window]() {
				QMessageBox::critical(
					main_window,
					obs_module_text(
						"SSPPlugin.VersionCheck.Title"),
					obs_module_text(
						"SSPPlugin.VersionCheck.Error"),
					QMessageBox::Ok);
			},
			Qt::QueuedConnection);

		ssp_blog(
			LOG_ERROR,
			"Incompatible OBS Studio version. This plugin requires OBS Studio %d.%d.%d or higher.",
			MIN_OBS_VERSION_MAJOR, MIN_OBS_VERSION_MINOR,
			MIN_OBS_VERSION_PATCH);
		return false;
	}

	// Check if the OBS Browser module is available
	if (!check_browser_module_available()) {
		// Display a warning message to the user through the UI
		QMainWindow *main_window =
			(QMainWindow *)obs_frontend_get_main_window();
		QMetaObject::invokeMethod(
			main_window,
			[main_window]() {
				QMessageBox::warning(
					main_window,
					obs_module_text(
						"SSPPlugin.BrowserCheck.Title"),
					obs_module_text(
						"SSPPlugin.BrowserCheck.Error"),
					QMessageBox::Ok);
			},
			Qt::QueuedConnection);

		ssp_blog(
			LOG_WARNING,
			"OBS Browser module not found! Some features may not work correctly.");
		ssp_blog(
			LOG_WARNING,
			"Please upgrade to a newer version of OBS that includes the Browser module.");
	}

	// Initialize CameraStatusManager
	CameraStatusManager::instance();
	ssp_blog(LOG_INFO, "CameraStatusManager initialized");

	create_mdns_loop();
	ssp_source_info = create_ssp_source_info();
	obs_register_source(&ssp_source_info);

	// Add menu action
	QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction(
		obs_module_text("SSPPlugin.Menu.ShowDock"));
	QObject::connect(action, &QAction::triggered,
			 [](bool) { show_ssp_dock(nullptr, nullptr); });

	return true;
}

static bool check_browser_module_available()
{
	obs_module_t *browser_module = obs_get_module("obs-browser");
	return browser_module != nullptr;
}

static bool check_obs_version_compatibility()
{
	// Get the current OBS version
	uint32_t obs_version = obs_get_version();

	// Extract major, minor, and patch from the version number
	uint8_t obs_major = (obs_version >> 24) & 0xFF;
	uint8_t obs_minor = (obs_version >> 16) & 0xFF;
	uint8_t obs_patch = (obs_version >> 8) & 0xFF;

	ssp_blog(LOG_INFO, "OBS Studio version: %d.%d.%d", obs_major, obs_minor,
		 obs_patch);

	// Check if the OBS version is greater than or equal to the minimum required version
	if (obs_major < MIN_OBS_VERSION_MAJOR)
		return false;

	if (obs_major == MIN_OBS_VERSION_MAJOR &&
	    obs_minor < MIN_OBS_VERSION_MINOR)
		return false;

	return true;
}

void obs_module_unload()
{

	ssp_blog(LOG_INFO, "[obs-ssp] obs_module_unload: Called.");
	stop_mdns_loop(); // Assuming blog is your macro
	ssp_blog(LOG_INFO, "[obs-ssp] obs_module_unload: MDNS loop stopped.");

	SspToolbarManager::shutdown();
	// Use a consistent logging mechanism, if ssp_blog is different from blog, pick one or ensure both work
	ssp_blog(
		LOG_INFO,
		"[obs-ssp] obs_module_unload: SspToolbarManager::shutdown() returned. s_instance is %s.",
		SspToolbarManager::checkInstance()
			? "NOT NULL"
			: "NULL (Note: s_instance is static in SspToolbarManager, check its value there)");

	ssp_blog(
		LOG_INFO,
		"[obs-ssp] obs_module_unload: Cleaning up CameraStatusManager...");
	CameraStatusManager::instance()->cleanup();
	CameraStatusManager::destroyInstance();
	ssp_blog(
		LOG_INFO,
		"[obs-ssp] obs_module_unload: CameraStatusManager cleaned up.");

	ssp_blog(
		LOG_INFO,
		"[obs-ssp] obs_module_unload: Goodbye!"); // Changed from ssp_blog for consistency example
}

const char *obs_module_name()
{
	return "obs-ssp";
}

const char *obs_module_description()
{
	return "Simple Stream Protocol input integration for OBS Studio";
}
