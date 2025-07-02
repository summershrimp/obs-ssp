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

#ifndef OBS_SSP_CAMERA_STATUS_MANAGER_H
#define OBS_SSP_CAMERA_STATUS_MANAGER_H

#include <string>
#include <map>
#include <mutex>
#include <QString>
#include "ssp-controller.h"

class CameraStatusManager {
public:
	static CameraStatusManager *instance();

	// Get or create a CameraStatus for the given IP
	// If a new CameraStatus is created, it will automatically fetch camera info and current stream data
	CameraStatus *getOrCreate(const std::string &ip);

	// Find a CameraStatus for the given IP (returns nullptr if not found)
	CameraStatus *find(const std::string &ip);

	// Release a CameraStatus (decrements reference count)
	void release(const std::string &ip);

	// Clean up all CameraStatus objects when shutting down
	void cleanup();

	// Static method to destroy the singleton instance
	static void destroyInstance();

	void updateStatus(CameraStatus *, bool needRegister);

protected:
	~CameraStatusManager();

private:
	CameraStatusManager() = default;

	// Map of IP addresses to CameraStatus objects
	std::map<std::string, CameraStatus *> cameraStatusMap;
	// Map of IP addresses to reference counts
	std::map<std::string, int> refCountMap;

	std::mutex mutex;

	static CameraStatusManager *_instance;
};

#endif // OBS_SSP_CAMERA_STATUS_MANAGER_H