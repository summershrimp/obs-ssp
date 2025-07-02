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
#include <obs-module.h>
#include "camera-status-manager.h"
#include "obs-ssp.h"
#include <QApplication>
#include <QThread>

CameraStatusManager *CameraStatusManager::_instance = nullptr;

CameraStatusManager *CameraStatusManager::instance()
{
	if (!_instance) {
		_instance = new CameraStatusManager();
	}
	return _instance;
}

void CameraStatusManager::destroyInstance()
{
	if (_instance) {
		delete _instance;
		_instance = nullptr;
		ssp_blog(LOG_INFO, "CameraStatusManager instance destroyed");
	}
}
static CameraStatus *createInMainThread(const QString &ip)
{
	// Check if we're already in the main thread to avoid deadlock

	ssp_blog(LOG_INFO, "Created new CameraStatus for IP: %s",
		 ip.toStdString().c_str());

	if (QThread::currentThread() == QApplication::instance()->thread()) {
		// We're already in the main thread, create directly
		CameraStatus *status = new CameraStatus();
		status->setIp(ip);

		return status;
	} else {
		// We're in a different thread, need to use invokeMethod
		CameraStatus *status = nullptr;
		QMetaObject::invokeMethod(
			QApplication::instance(),
			[&status, ip]() {
				status = new CameraStatus();
				status->setIp(ip);
				CameraStatusManager::instance()->updateStatus(
					status, true);
			},
			Qt::QueuedConnection);
		return status;
	}
}

void CameraStatusManager::updateStatus(CameraStatus *status, bool needRegister)
{

	if (needRegister) {
		std::string ip = status->getIp().toStdString();
		cameraStatusMap[ip] = status;
		refCountMap[ip] = 1;
	}
	// Initialize the camera status by fetching information right away
	status->refreshAll([](bool ok) {
		if (!ok) {
			ssp_blog(LOG_WARNING, "Failed to get camera info");
		}
	});
}
CameraStatus *CameraStatusManager::getOrCreate(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (ip.empty()) {
		return nullptr;
	}

	// Check if we already have a CameraStatus for this IP
	auto it = cameraStatusMap.find(ip);
	if (it != cameraStatusMap.end()) {
		// Increment reference count
		refCountMap[ip]++;
		return it->second;
	}

	// Create a new CameraStatus
	CameraStatus *status = createInMainThread(QString::fromStdString(ip));

	// Add to maps

	if (status != nullptr) {
		cameraStatusMap[ip] = status;
		refCountMap[ip] = 1;
		updateStatus(status, false);
	}

	return status;
}
CameraStatus *CameraStatusManager::find(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (ip.empty()) {
		return nullptr;
	}

	// Look up the CameraStatus for this IP
	auto it = cameraStatusMap.find(ip);
	if (it != cameraStatusMap.end()) {
		return it->second;
	}

	return nullptr;
}

void CameraStatusManager::release(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (ip.empty()) {
		return;
	}

	// Check if we have a CameraStatus for this IP
	auto it = cameraStatusMap.find(ip);
	if (it != cameraStatusMap.end()) {
		// Decrement reference count
		refCountMap[ip]--;

		// If reference count reaches 0, delete the CameraStatus
		if (refCountMap[ip] <= 0) {
			ssp_blog(LOG_INFO, "Deleting CameraStatus for IP: %s",
				 ip.c_str());
			delete it->second;
			cameraStatusMap.erase(it);
			refCountMap.erase(ip);
		}
	}
}

void CameraStatusManager::cleanup()
{
	std::lock_guard<std::mutex> lock(mutex);

	ssp_blog(LOG_INFO, "Cleaning up all CameraStatus objects");

	// Delete all CameraStatus objects
	for (auto &pair : cameraStatusMap) {
		delete pair.second;
	}

	cameraStatusMap.clear();
	refCountMap.clear();
}

CameraStatusManager::~CameraStatusManager()
{
	cleanup();
}
