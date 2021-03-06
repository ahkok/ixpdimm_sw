/*
 * Copyright (c) 2015 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the implementation of the event monitoring class
 * of the NvmMonitor service which periodically detects and stores interesting events.
 */

#include "NvmMonitorBase.h"
#include "nvm_management.h"
#include <persistence/schema.h>
#include <string>
#include <map>
#include <vector>
#include <core/NvmLibrary.h>

#ifndef _MONITOR_EVENTMONITOR_H_
#define _MONITOR_EVENTMONITOR_H_

namespace monitor
{
	static std::string ERASURE_CODED = "erasure coded";
	static std::string CORRECTED = "corrected";
	static std::string UNCORRECTABLE = "uncorrectable";

	struct deviceInfo
	{
		bool discovered;
		struct device_discovery discovery;
		struct device_status status;
		bool stored;
		struct db_topology_state storedState;
	};

	//!< Map with a UID string key and deviceInfo Struct
	typedef std::map<std::string, struct deviceInfo> DeviceMap;

	/*!
	 * @brief Process to monitor conditions on the system and generate events for important
	 * changes.
	 */
	class EventMonitor : public NvmMonitorBase
	{
	public:
		class NoDeviceSavedState : public std::exception
		{};

		/*!
		 * Constructor
		 */
		EventMonitor(core::NvmLibrary &lib = core::NvmLibrary::getNvmLibrary());

		virtual ~EventMonitor();

		/*!
		 * Startup and initialization logic.
		 */
		virtual void init();

		/*!
		 * Shutdown and cleanup logic.
		 */
		virtual void cleanup();

		/*!
		 * Core monitoring logic.
		 */
		virtual void monitor();

		/*
		 * Find and acknowledge an event of the specified code
		 */
		static void acknowledgeEventCodeForDevice(const int eventCode, const NVM_UID deviceUid);

	private:
		int m_nsMgmtCallbackId; // callback identifer for delete namespace events
		core::NvmLibrary &m_lib;

		/*
		 * Process "start of day" events - conditions to be detected on process start-up.
		 */
		void startOfDay();
		void runPlatformConfigDiagnostic();
		void runDiagnostic(const diagnostic_test diagType, const std::string& uid = "");
		void checkDeviceStartUpStatus();

		DeviceMap getCurrentDeviceMapWithSavedTopology();
		DeviceMap getCurrentDeviceMap();
		void addCurrentDevicesToDeviceMap(DeviceMap& map);
		deviceInfo getTopologyInfoForDevice(const struct device_discovery &device);
		bool isSavedTopologyStateValid();
		void addSavedTopologyStateToDeviceMap(DeviceMap& map);
		std::vector<struct db_topology_state> getSavedTopologyState();

		void checkDeviceTopologyForChanges(const DeviceMap &devices);
		void processTopologyNewDimms(const DeviceMap &devices,
				std::vector<std::string> &replacedHandles);
		std::string getReplacedDimmUid(const DeviceMap &devices, const NVM_UINT32 &handle);
		void processTopologyModifiedDimms(const DeviceMap &devices,
				const std::vector<std::string> &replacedHandles);
		void saveCurrentTopologyState(const DeviceMap &devices);

		void checkConfigStatusForAllDevices(DeviceMap &devices);
		void checkConfigGoalStatus(const std::string &uidStr, const deviceInfo &device);
		config_goal_status getConfigGoalStatusForDevice(const std::string &uid);
		void createEventForConfigGoalAppliedOnDevice(const std::string &uid);
		void acknowledgePlatformConfigEventsForDevice(const std::string &uid);
		void acknowledgeEventTypeForDevice(const event_type type, const std::string &uid);

		void acknowledgeDeletedNamespaces();
		bool namespaceDeleted(const NVM_UID nsUid,
				const std::vector<std::string> &nsUids);

		/*
		 * Process conditions to be detected on each monitor cycle.
		 */
		void monitorDevices();
		void runQuickHealthDiagnosticForDevice(const std::string &uid);
		void monitorChangesForDevice(const deviceInfo &device);
		struct db_dimm_state getSavedStateForDevice(const deviceInfo &device);
		void saveStateForDevice(struct db_dimm_state &newState);

		void processSensorStateChangesForDevice(const deviceInfo &device, struct db_dimm_state &dimmState);
		std::vector<sensor> getSensorsForDevice(const deviceInfo &device);
		void detectMediaErrorSensorChanges(const std::vector<sensor> &sensors, const NVM_UID deviceUid,
				const struct db_dimm_state &savedState);
		bool sensorReadingHasIncreased(const std::vector<sensor>& sensors,
				const sensor_type sensorType, const NVM_UINT64 oldReading);
		bool sensorsIncludeType(const std::vector<sensor> &sensors, const sensor_type type);
		void createMediaErrorEvent(const NVM_UID uid, const std::string &errorType);
		void updateStateForMediaErrorSensors(struct db_dimm_state &dimmState,
				const std::vector<sensor> &sensors);

		void detectFwErrorSensorChanges(const std::vector<sensor> &sensors, const NVM_UID deviceUid,
				const struct db_dimm_state &savedState);
		void createFwErrorLogEvent(const NVM_UID deviceUid, const NVM_UINT64 errorCount);
		void updateStateForFwErrorSensors(struct db_dimm_state &dimmState,
				const std::vector<sensor> &sensors);

		void processHealthChangesForDevice(const deviceInfo &device, struct db_dimm_state &dimmState);
		void createDeviceHealthEvent(const NVM_UID uid, const device_health oldHealth, const device_health newHealth);
		void processSanitizeChangesForDevice(const deviceInfo &device, struct db_dimm_state &dimmState);
		void createSanitizeOperationEvent(const NVM_UID uid, const device_sanitize_status oldSanitizeStatus,
				const device_sanitize_status newSanitizeStatus);
		bool isActionRequiredForDeviceHealth(enum device_health health);
		event_severity getEventSeverityForDeviceHealth(enum device_health health);
		std::string deviceHealthToStr(enum device_health health);

		void initializeDimmState(struct db_dimm_state &dimmState, const deviceInfo& device);
		void initializeSensorStateForDevice(struct db_dimm_state &dimmState, const deviceInfo& device);
		NVM_UINT64 getLatestSensorReading(const std::vector<sensor> &sensors,
				const sensor_type sensorType,
				const NVM_UINT64 oldReading);

		void monitorNamespaces(PersistentStore *p_Store);
		std::string namespaceHealthToStr(enum namespace_health health);
	};
}
#endif /* _MONITOR_EVENTMONITOR_H_ */
