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

#include "DeviceService.h"

#include <algorithm>
#include <logic/exceptions/NoMemoryException.h>
#include <logic/exceptions/LibraryException.h>
#include <LogEnterExit.h>

logic::device::DeviceService *logic::device::DeviceService::m_pSingleton = new logic::device::DeviceService();

logic::device::DeviceService &logic::device::DeviceService::getService()
{
	LogEnterExit(__FUNCTION__, __FILE__, __LINE__);

	if (m_pSingleton == NULL)
	{
		throw NoMemoryException();
	}

	return *m_pSingleton;
}

std::vector<std::string> logic::device::DeviceService::getAllGuids()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	std::vector<std::string> result = std::vector<std::string>();
	const std::vector<device_discovery> &discoveries = getDiscoveries();

	for(size_t i = 0; i < discoveries.size(); i++)
	{
		result.push_back(Helper::guidToString(discoveries[i].guid));
	}

	return result;
}

std::vector<std::string> logic::device::DeviceService::getManageableGuids()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	std::vector<std::string> result = std::vector<std::string>();
	const std::vector<device_discovery> &discoveries = getDiscoveries();

	for(size_t i = 0; i < discoveries.size(); i++)
	{
		if(discoveries[i].manageability == MANAGEMENT_VALIDCONFIG)
		{
			result.push_back(Helper::guidToString(discoveries[i].guid));
		}
	}

	return result;
}

logic::device::DeviceCollection logic::device::DeviceService::getAllDevices()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	DeviceCollection result;

	const std::vector<device_discovery> &discoveries = getDiscoveries();
	for(size_t i = 0; i < discoveries.size(); i++)
	{
		Device device(m_pApi, discoveries[i]);
		result.push_back(device);
	}

	return result;
}

logic::device::Device *logic::device::DeviceService::getDevice(std::string guid)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	device_discovery d;
	NVM_GUID nvmGuid;
	Helper::stringToGuid(guid, nvmGuid);
	int rc = m_pApi.getDeviceDiscovery(nvmGuid, &d);
	if (rc != NVM_SUCCESS)
	{
		throw LibraryException(rc);
	}
	Device *pResult = new Device(m_pApi, d);

	return pResult;
}

std::vector<device_discovery> logic::device::DeviceService::getDiscoveries() const
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	std::vector<device_discovery> result;
	int rc = m_pApi.getDeviceCount();

	if (rc < 0)
	{
		throw logic::LibraryException(rc);
	}

	int count = rc;

	device_discovery devices[count];

	rc = m_pApi.getDevices(devices, count);
	if (rc < 0)
	{
		throw logic::LibraryException(rc);
	}

	for(int i = 0; i < count; i++)
	{
		result.push_back(devices[i]);
	}

	return result;
}
