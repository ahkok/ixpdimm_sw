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

#include <core/exceptions/NoMemoryException.h>
#include <LogEnterExit.h>
#include "SystemService.h"

core::system::SystemService *core::system::SystemService::m_pSingleton = new core::system::SystemService();

core::system::SystemService::SystemService(const NvmApi &pApi) : m_api(pApi)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
}

core::system::SystemService::~SystemService()
{
	if (this == m_pSingleton)
	{
		m_pSingleton = NULL;
	}
}

core::system::SystemService &core::system::SystemService::getService()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	if (m_pSingleton == NULL)
	{
		throw NoMemoryException();
	}
	return *m_pSingleton;
}

std::string core::system::SystemService::getHostName()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	char hostname[NVM_COMPUTERNAME_LEN];
	int rc = m_api.getHostName(hostname, NVM_COMPUTERNAME_LEN);

	if (rc != NVM_SUCCESS)
	{
		throw LibraryException(rc);
	}

	return std::string(hostname);
}

core::Result<core::system::SystemInfo> core::system::SystemService::getHostInfo()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	host host;
	int rc = m_api.getHost(&host);
	if (rc != NVM_SUCCESS)
	{
		throw LibraryException(rc);
	}

	rc = m_api.debugLoggingEnabled();
	if (rc < 0)
	{
		throw LibraryException(rc);
	}
	int logLevel = rc;

	SystemInfo result(host, logLevel);
	return Result<SystemInfo>(result);
}

core::Result<core::system::SystemMemoryResources> core::system::SystemService::getMemoryResources()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	device_capacities device_capacities;
	int rc = m_api.getNvmCapacities(&device_capacities);
	if (rc != NVM_SUCCESS)
	{
		throw LibraryException(rc);
	}

	SystemMemoryResources result(device_capacities);
	return Result<SystemMemoryResources>(result);
}