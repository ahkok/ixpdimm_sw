/*
 * Copyright (c) 2016, Intel Corporation
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
#ifndef CR_MGMT_DEVICE_H
#define CR_MGMT_DEVICE_H


#include <nvm_management.h>
#include <logic/Helper.h>
#include <common_types.h>
#include <system/jedec_manufacturer.h>
#include <utility.h>
#include <nvm_types.h>

#include <string>
#include <vector>
#include <string/s_str.h>
#include <sstream>
#include <logic/exceptions/LibraryException.h>
#include <logic/NvmApi.h>

static const NVM_UINT32 SECURITY_PASSPHRASE = 0;
static const NVM_UINT32 SECURITY_UNLOCK = 1;
static const NVM_UINT32 SECURITY_ERASE = 2;

static const NVM_UINT16 MEMORY_CAPABILITY_MEMORYMODE = 0;
static const NVM_UINT16 MEMORYTYPE_CAPABILITY_STORAGEMODE = 1;
static const NVM_UINT16 MEMORYTYPE_CAPABILITY_APPDIRECTMODE = 2;

static const NVM_UINT16 DEVICE_HEALTH_UNMANAGEABLE = 65534;

static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_UKNOWN = 0;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_FW_FLUSH_COMPLETE = 1;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_PM_ADR_COMMAND = 2;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_PM_S3 = 3;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_PM_S5 = 4;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_DDRT_POWER_FAIL = 5;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_PMIC_12V_POWER_FAIL = 6;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_PM_WARM_RESET = 7;
static const NVM_UINT16 DEVICE_LAST_SHUTDOWN_STATUS_THERMAL_SHUTDOWN = 8;


namespace logic
{
namespace device
{

class NVM_API Device
{
public:

	Device(NvmApi &api, const device_discovery &discovery) :
		m_api(api),
		m_pDetails(NULL),
		m_pActionRequiredEvents(NULL)
	{
		memmove(&m_discovery, &discovery, sizeof(m_discovery));
	}

	virtual ~Device()
	{
		if (m_pDetails)
		{
			delete m_pDetails;
		}
		if (m_pActionRequiredEvents)
		{
			delete m_pActionRequiredEvents;
		}
	}

	Device &operator=(const Device &other)
	{
		if (&other == this)
			return *this;
		this->m_pDetails = other.m_pDetails;
		this->m_discovery = other.m_discovery;
		this->m_api = other.m_api;

		return *this;
	}

	virtual Device *clone();

	virtual std::string getGuid();
	virtual enum manageability_state getManageabilityState();
	virtual bool isManageable();
	virtual NVM_UINT32 getDeviceHandle();
	virtual NVM_UINT32 getChannelPosition();
	virtual NVM_UINT32 getChannelId();
	virtual NVM_UINT16 getPhysicalId();
	virtual NVM_UINT16 getVendorId();
	virtual NVM_UINT16 getDeviceId();
	virtual NVM_UINT16 getRevisionId();
	virtual NVM_UINT16 getSocketId();
	virtual NVM_UINT16 getMemoryControllerId();
	virtual enum memory_type getMemoryType();
	virtual std::string getManufacturer();
	virtual NVM_UINT16 getManufacturerId();
	virtual std::string getSerialNumber();
	virtual std::string getModelNumber();
	virtual std::string getFwRevision();
	virtual std::string getFwApiVersion();
	virtual fw_log_level getFwLogLevel();
	virtual NVM_UINT64 getRawCapacity();
	virtual NVM_UINT16 getInterfaceFormatCode();
	virtual bool isPassphraseCapable();
	virtual bool isUnlockDeviceCapable();
	virtual bool isEraseOverwriteCapable();
	virtual bool isEraseCryptoCapable();
	virtual std::vector<NVM_UINT32> getSecurityCapabilities();
	virtual enum lock_state getLockState();
	virtual bool isDieSparingCapable();
	virtual bool isAppDirectModeCapable();
	virtual bool isMemoryModeCapable();
	virtual bool isStorageModeCapable();
	virtual std::vector<NVM_UINT16> getMemoryCapabilities();
	virtual NVM_UINT32 getSku();
	virtual enum device_health getDeviceStatusHealth();
	virtual NVM_UINT16 getHealthState();
	virtual bool isNew();
	virtual bool getIsMissing();
	virtual NVM_UINT8 getDieSparesUsed();
	virtual std::vector<NVM_UINT16> getLastShutdownStatus();
	virtual enum config_status getConfigStatus();
	virtual NVM_UINT64 getLastShutdownTime();
	virtual bool isMixedSku();
	virtual bool isSkuViolation();
	virtual time_t getPerformanceTime();
	virtual NVM_UINT64 getBytesRead();
	virtual NVM_UINT64 getHostReads();
	virtual NVM_UINT64 getBytesWritten();
	virtual NVM_UINT64 getHostWrites();
	virtual NVM_UINT64 getBlockReads();
	virtual NVM_UINT64 getBlockWrites();
	virtual NVM_UINT64 getTotalCapacity();
	virtual NVM_UINT64 getVolatileCapacity();
	virtual NVM_UINT64 getPersistentCapacity();
	virtual NVM_UINT64 getBlockCapacity();
	virtual NVM_UINT64 getUnconfiguredCapacity();
	virtual NVM_UINT64 getInaccessibleCapacity();
	virtual NVM_UINT64 getReservedCapacity();
	virtual enum device_form_factor getFormFactor();
	virtual NVM_UINT64 getDataWidth();
	virtual NVM_UINT64 getTotalWidth();
	virtual NVM_UINT64 getSpeed();
	virtual bool getPowerManagementEnabled();
	virtual NVM_UINT8 getPowerLimit();
	virtual NVM_UINT16 getPeakPowerBudget();
	virtual NVM_UINT16 getAvgPowerBudget();
	virtual bool getDieSparingEnabled();
	virtual NVM_UINT8 getDieSparingLevel();
	virtual std::string getPartNumber();
	virtual std::string getDeviceLocator();
	virtual std::string getBankLabel();
	virtual bool isFirstFastRefresh();
	virtual bool isActionRequired();
	virtual std::vector<std::string> getActionRequiredEvents();

private:
	NvmApi &m_api;
	device_discovery m_discovery;
	device_details *m_pDetails;
	std::vector<std::string> *m_pActionRequiredEvents;

	const device_discovery &getDiscovery();
	const device_details &getDetails();
	const std::vector<std::string> &getEvents();
};




}
}
#endif //CR_MGMT_DEVICE_H
