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

#include <LogEnterExit.h>
#include "DeviceFirmwareInfo.h"

std::string logic::device::DeviceFirmwareInfo::getActiveRevision()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return std::string(m_info.active_fw_revision);
}
enum device_fw_type logic::device::DeviceFirmwareInfo::getActiveType()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return m_info.active_fw_type;
}
std::string logic::device::DeviceFirmwareInfo::getActiveCommitId()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return std::string(m_info.active_fw_commit_id);
}
enum device_fw_type logic::device::DeviceFirmwareInfo::getStagedType()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return m_info.staged_fw_type;
}
std::string logic::device::DeviceFirmwareInfo::getStagedRevision()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return std::string(m_info.staged_fw_revision);
}
bool logic::device::DeviceFirmwareInfo::isStagedPending()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return m_info.staged_fw_pending == 1;
}
logic::device::DeviceFirmwareInfo *logic::device::DeviceFirmwareInfo::clone()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	return new DeviceFirmwareInfo(*this);
}