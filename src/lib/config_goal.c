/*
 * Copyright (c) 2015, Intel Corporation
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
 * This file contains the implementation of functions to manage configuration
 * goals, which are used to configure the capacity of an NVM-DIMM.
 */

#include "nvm_management.h"
#include <common_types.h>
#include <persistence/logging.h>
#include <persistence/lib_persistence.h>
#include "platform_config_data.h"
#include "utility.h"
#include "monitor.h"
#include "device_utilities.h"
#include "pool_utilities.h"
#include "capabilities.h"
#include "system.h"

/*
 * Helper function to verify the config goal size (in GiB) is valid with the given alignment
 * (in bytes).
 */
int validate_size_alignment(const NVM_UINT64 size_gb, const NVM_UINT64 alignment_bytes)
{
	int rc = NVM_SUCCESS;

	// all or none is always aligned
	if ((size_gb != (NVM_UINT64)-1) && (size_gb != 0))
	{
		// size not aligned
		if ((size_gb * BYTES_PER_GB) % alignment_bytes)
		{
			rc = NVM_ERR_BADALIGNMENT;
		}
	}

	return rc;
}

/*
 * Helper function to verify requested config_goal sizes fit in required alignment.
 * NOTE: Assumes caller has already validated for NULL.
 */
int validate_config_goal_alignment(const struct config_goal *p_goal,
		const struct nvm_capabilities *p_capabilities)
{
	int rc = NVM_SUCCESS;

	// verify requested sizes match our alignment capabilities
	// volatile - 2^n bytes
	NVM_UINT8 vm_alignment_exp =
			p_capabilities->platform_capabilities.two_lm.interleave_alignment_size;
	// persistent - 2^n bytes
	NVM_UINT8 pm_alignment_exp =
			p_capabilities->platform_capabilities.pm_direct.interleave_alignment_size;
	if ((vm_alignment_exp < 64) && (pm_alignment_exp < 64))
		// alignments will fit in 64 bits
	{
		NVM_UINT64 volatile_alignment = (1 << vm_alignment_exp); // bytes
		NVM_UINT64 persistent_alignment = (1 << pm_alignment_exp); // bytes

		// Volatile size is OK
		rc = validate_size_alignment(p_goal->volatile_size, volatile_alignment);
		if (rc == NVM_SUCCESS)
		{
			// Verify persistent sizes
			NVM_UINT64 sizes[NVM_MAX_PM_POOLS_PER_DIMM][2] = {
					{p_goal->persistent_1_size, persistent_alignment},
					{p_goal->persistent_2_size, persistent_alignment}
			};

			for (unsigned int i = 0; (i < p_goal->persistent_count) &&
				(rc == NVM_SUCCESS); i++)
			{
				rc = validate_size_alignment(sizes[i][0], sizes[i][1]);
			}
		}
	}
	else // Got bogus alignment sizes
	{
		COMMON_LOG_ERROR_F("Driver returned bad alignment size (2^n). "
				"2LM alignment_size: %d,"
				"PM alignment_size: %d",
				vm_alignment_exp,
				pm_alignment_exp);
		rc = NVM_ERR_UNKNOWN;
	}

	return rc;
}

/*
 * Helper function to derive the partition size (in GiB) from the capacity (in bytes) and whether
 * the interleave set will be mirrored.
 */
NVM_UINT64 get_size_from_capacity(const NVM_UINT64 size_gb, NVM_UINT64 remaining_capacity,
		NVM_BOOL mirrored)
{
	NVM_UINT64 size = 0;

	// Convert special size values
	if (size_gb == (NVM_UINT64)-1) // flag value - use all remaining capacity
	{
		size = (remaining_capacity / BYTES_PER_GB);

		// Partition size in platform config data is capacity presented to user.
		// For mirrored, this is half of actual capacity.
		if (mirrored)
		{
			size /= 2llu;
		}
	}
	else
	{
		size = size_gb;
	}

	return size;
}

/*
 * Helper function to verify size requested fits in remaining capacity.
 * Returns updated capacity remaining.
 */
int validate_interleave_set_size(const NVM_UINT64 size_gb,
		NVM_UINT64 *p_remaining_capacity,
		NVM_BOOL mirrored)
{
	int rc = NVM_SUCCESS;
	NVM_UINT64 remaining_capacity = *p_remaining_capacity;
	NVM_UINT64 actual_size_gb = 0;
	NVM_UINT64 size = 0;

	// Calculate capacity occupied by this interleave set in gibibytes
	actual_size_gb =
			get_size_from_capacity(size_gb, remaining_capacity, mirrored);
	if (mirrored) // mirrored takes up 2x the space presented as its partition size
	{
		actual_size_gb *= 2llu;
	}

	// too big to be stored in an NVM_UINT64
	if (actual_size_gb > MAX_UINT64_GB)
	{
		COMMON_LOG_ERROR_F("Caller requested size in GiB %llu will overrun 64 bits",
				actual_size_gb);
		rc = NVM_ERR_BADSIZE;
	}
	else
	{
		size = actual_size_gb * BYTES_PER_GB;
		if (size > remaining_capacity) // Won't fit
		{
			rc = NVM_ERR_BADSIZE;
		}
		else
		{
			// update remaining capacity
			*p_remaining_capacity = remaining_capacity - size;
		}
	}

	return rc;
}

/*
 * Helper function to verify sizes requested in config_goal are valid.
 */
int validate_config_goal_size(const struct config_goal *p_goal,
		const struct device_discovery *p_discovery)
{
	int rc = NVM_SUCCESS;
	NVM_UINT64 capacity = USABLE_CAPACITY_BYTES(p_discovery->capacity);

	// Validate volatile size
	rc = validate_interleave_set_size(p_goal->volatile_size, &capacity, 0);
	if (rc == NVM_SUCCESS)
	{
		// Validate PM interleave set sizes
		NVM_UINT64 sizes[NVM_MAX_PM_POOLS_PER_DIMM] = {
				p_goal->persistent_1_size,
				p_goal->persistent_2_size
		};
		NVM_BOOL mirrored[NVM_MAX_PM_POOLS_PER_DIMM] = {
				p_goal->persistent_1_settings.mirrored,
				p_goal->persistent_2_settings.mirrored
		};

		for (unsigned int i = 0; (i < p_goal->persistent_count) && (rc == NVM_SUCCESS); i++)
		{
			if (sizes[i] == 0) // we expect a pool to have a capacity > 0
			{
				rc = NVM_ERR_BADSIZE;
			}
			else
			{
				rc = validate_interleave_set_size(sizes[i], &capacity, mirrored[i]);
			}
		}
	}

	return rc;
}

NVM_UINT32 get_number_of_mirrored_pm_interleave_sets(const struct config_goal *p_goal)
{
	NVM_UINT32 num_mirrored = 0;

	if ((p_goal->persistent_count > 0) && p_goal->persistent_1_settings.mirrored)
	{
		num_mirrored++;
	}

	if ((p_goal->persistent_count > 1) && p_goal->persistent_2_settings.mirrored)
	{
		num_mirrored++;
	}

	return num_mirrored;
}

int current_volatile_mode_is_supported(enum volatile_mode volatile_mode)
{
	int result = 0;

	if ((volatile_mode == VOLATILE_MODE_2LM) ||
		(volatile_mode == VOLATILE_MODE_AUTO))
	{
		result = 1;
	}
	return result;
}

int memory_capacity_is_requested_but_not_supported(NVM_UINT64 volatile_size,
	enum volatile_mode current_volatile_mode, NVM_BOOL memory_mode_capable)
{
	int result = 0;

	if (volatile_size > 0 &&
		(!current_volatile_mode_is_supported(current_volatile_mode) ||
			!memory_mode_capable))
	{
		result = 1;
	}
	return result;
}

int appdirect_capacity_is_requested_but_not_supported(NVM_UINT16 persistent_count,
	enum pm_mode current_pm_mode, NVM_BOOL app_direct_mode_capable)
{
	int result = 0;

	if (persistent_count > 0 &&
		((current_pm_mode != PM_MODE_PM_DIRECT) ||
			!app_direct_mode_capable))
	{
		result = 1;
	}
	return result;
}

/*
 * Helper function to verify that pools and QOS attributes requested are supported.
 * NOTE: Assumes caller has validated for NULL inputs.
 */
int validate_config_goal_supported(const struct config_goal *p_goal,
		const struct nvm_capabilities *p_capabilities,
		const struct device_discovery *p_discovery)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (memory_capacity_is_requested_but_not_supported(p_goal->volatile_size,
			p_capabilities->platform_capabilities.current_volatile_mode,
			p_discovery->device_capabilities.memory_mode_capable))
	{
		COMMON_LOG_WARN("Memory capacity requested but is not supported.");
		rc = NVM_ERR_CONFIGNOTSUPPORTED;
	}
	else if (appdirect_capacity_is_requested_but_not_supported(p_goal->persistent_count,
			p_capabilities->platform_capabilities.current_pm_mode,
			p_discovery->device_capabilities.app_direct_mode_capable))
	{
		COMMON_LOG_WARN("App-direct capacity requested but is not supported.");
		rc = NVM_ERR_CONFIGNOTSUPPORTED;
	}
	else
	{
		NVM_UINT32 num_mirrored = get_number_of_mirrored_pm_interleave_sets(p_goal);
		if (num_mirrored > 0 &&
			!p_capabilities->platform_capabilities.memory_mirror_supported)
		// mirroring not supported
		{
			COMMON_LOG_ERROR("Mirroring requested but is not supported");
			rc = NVM_ERR_CONFIGNOTSUPPORTED;
		}
		else if (num_mirrored > 1) // only one mirrored interleave per DIMM supported
		{
			COMMON_LOG_ERROR("More than one mirrored interleave per DIMM is not supported");
			rc = NVM_ERR_BADDEVICECONFIG;
		}
	}

	// log an event
	if (rc == NVM_ERR_CONFIGNOTSUPPORTED)
	{
		NVM_GUID_STR guid_str;
		guid_to_str(p_discovery->guid, guid_str);
		store_event_by_parts(
				EVENT_TYPE_HEALTH,
				EVENT_SEVERITY_CRITICAL,
				EVENT_CODE_CONFIG_GOAL_SKU_VIOLATION,
				p_discovery->guid,
				0,
				guid_str,
				NULL,
				NULL,
				DIAGNOSTIC_RESULT_UNKNOWN);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to validate that the config_goal values are valid.
 */
int validate_config_goal(const struct config_goal *p_goal,
		const struct nvm_capabilities *p_capabilities,
		const struct device_discovery *p_discovery)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (p_goal->persistent_count > NVM_MAX_PM_POOLS_PER_DIMM)
	{
		rc = NVM_ERR_BADDEVICECONFIG;
	}
	else if ((rc = validate_config_goal_supported(p_goal,
			p_capabilities, p_discovery)) == NVM_SUCCESS)
	{
		rc = validate_config_goal_size(p_goal, p_discovery);
		if (rc == NVM_SUCCESS)
		{
			rc = validate_config_goal_alignment(p_goal, p_capabilities);
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to populate partition change extension table.
 */
void config_goal_to_partition_ext_table(const struct config_goal *p_goal,
		struct partition_size_change_extension_table *p_table,
		const struct device_discovery *p_discovery)
{
	memset(p_table, 0, sizeof (struct partition_size_change_extension_table));
	p_table->header.type = PARTITION_CHANGE_TABLE;
	p_table->header.length = sizeof (struct partition_size_change_extension_table);

	NVM_UINT64 overall_capacity = USABLE_CAPACITY_BYTES(p_discovery->capacity);
	NVM_UINT64 pm_capacity = 0; // in bytes

	// Goal entered for volatile gets first dibs.
	// In BIOS all space not designated PM is volatile.
	// -1 => all capacity used for volatile
	if (p_goal->volatile_size != (NVM_UINT64)-1)
	{
		// Everything else is PM - either direct or block
		NVM_UINT64 volatile_size_gb = get_size_from_capacity(p_goal->volatile_size,
				overall_capacity, 0);
		pm_capacity = overall_capacity - (volatile_size_gb * BYTES_PER_GB);
	}

	p_table->partition_size = pm_capacity; // in bytes
}

/*
 * Helper function to populate interleave extension table.
 * Caller should pass pp_table as a pointer to a NULL pointer. It will
 * return the allocated memory with the table populated.
 * Caller is responsible for freeing *pp_table.
 */
int config_goal_to_interleave_ext_table(const struct pm_attributes *p_qos,
		const NVM_UINT64 interleave_set_size,
		const NVM_UINT64 interleave_set_offset,
		const NVM_UINT16 interleave_set_id,
		struct interleave_info_extension_table **pp_table)
{
	int rc = NVM_SUCCESS;

	if (pp_table && (*pp_table == NULL))
	{
		NVM_UINT32 num_dimms = p_qos->interleave.ways;
		NVM_UINT32 table_size = sizeof (struct interleave_info_extension_table) +
				(num_dimms * sizeof (struct dimm_info_extension_table));
		*pp_table = malloc(table_size);
		struct interleave_info_extension_table *p_table = *pp_table;
		if (p_table)
		{
			memset(p_table, 0, table_size);
			p_table->header.type = INTERLEAVE_TABLE;
			p_table->header.length = table_size;

			p_table->mirror_enable = p_qos->mirrored;

			p_table->index = interleave_set_id;
			interleave_struct_to_format(&p_qos->interleave, &p_table->interleave_format);
			p_table->dimm_count = num_dimms;

			// Add dimm_info_extension_tables
			struct dimm_info_extension_table *p_dimms =
					(struct dimm_info_extension_table *)&p_table->p_dimms;
			for (NVM_UINT32 i = 0; i < num_dimms; i++)
			{
				struct device_discovery discovery;

				// Look up the DIMM
				rc = exists_and_manageable(p_qos->dimms[i], &discovery, 1);
				if (rc != NVM_SUCCESS) // invalid dimm
				{
					break;
				}

				// The DIMM is OK - put it in the list
				p_dimms[i].size = interleave_set_size * BYTES_PER_GB;
				p_dimms[i].offset = interleave_set_offset * BYTES_PER_GB;
				memmove(p_dimms[i].serial_number, discovery.serial_number, NVM_SERIAL_LEN);
				memmove(p_dimms[i].manufacturer, discovery.manufacturer, NVM_MANUFACTURER_LEN);
				memmove(p_dimms[i].model_number, discovery.model_number, NVM_MODEL_LEN-1);
			}
		}
		else
		{
			rc = NVM_ERR_NOMEMORY;
		}
	}
	else // caller passed a weird pointer
	{
		rc = NVM_ERR_INVALIDPARAMETER;
	}

	return rc;
}

/*
 * Helper function to convert a config_goal structure to a config data input
 * table.
 * Expect expect pp_input_table to point to a NULL pointer.
 * Caller must free *pp_input_table.
 */
int config_goal_to_config_input(const NVM_GUID device_guid,
		const struct config_goal *p_goal,
		struct config_input_table **pp_input_table,
		const struct nvm_capabilities *p_capabilities,
		const struct device_discovery *p_discovery,
		NVM_UINT32 seq_num) // must be one the BIOS hasn't seen before
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	NVM_UINT32 ext_table_length = 0;

	// Mapping from config goal to extension tables

	// Partition size change table
	struct partition_size_change_extension_table partition_table;
	config_goal_to_partition_ext_table(p_goal, &partition_table, p_discovery);
	ext_table_length += partition_table.header.length;

	// Interleave tables
	struct interleave_info_extension_table *p_pm1_interleave_table = NULL;
	struct interleave_info_extension_table *p_pm2_interleave_table = NULL;

	NVM_UINT64 capacity = USABLE_CAPACITY_BYTES(p_discovery->capacity);
	capacity -= (p_goal->volatile_size * BYTES_PER_GB); // subtract volatile space

	// interleave - persistent
	if (p_goal->persistent_count > 0)
	{
		// interleave - persistent pool 1
		NVM_UINT64 pm1_size = get_size_from_capacity(p_goal->persistent_1_size,
				capacity, p_goal->persistent_1_settings.mirrored);
		rc = config_goal_to_interleave_ext_table(&p_goal->persistent_1_settings,
				pm1_size, // in GiB
				0, // offset - top of PM partition
				p_goal->persistent_1_set_id,
				&p_pm1_interleave_table);
		if (rc == NVM_SUCCESS)
		{
			p_pm1_interleave_table->memory_type = INTERLEAVE_MEMORY_TYPE_PERSISTENT;
			ext_table_length += p_pm1_interleave_table->header.length;
			capacity -= (pm1_size * BYTES_PER_GB);

			// interleave - persistent pool 2
			if (p_goal->persistent_count > 1)
			{
				// interleave - persistent pool 2
				NVM_UINT64 pm2_size = get_size_from_capacity(p_goal->persistent_2_size,
						capacity, p_goal->persistent_2_settings.mirrored);
				rc = config_goal_to_interleave_ext_table(&p_goal->persistent_2_settings,
						pm2_size, // in GiB
						p_goal->persistent_1_size, // offset - after first pool
						p_goal->persistent_2_set_id,
						&p_pm2_interleave_table);
				if (rc == NVM_SUCCESS)
				{
					p_pm2_interleave_table->memory_type = INTERLEAVE_MEMORY_TYPE_PERSISTENT;
					ext_table_length += p_pm2_interleave_table->header.length;
					capacity -= (pm2_size * BYTES_PER_GB);
				}
			}
		}
	}

	// All ext tables filled out successfully
	if (rc == NVM_SUCCESS)
	{
		// Set up the final table
		// Make sure pp_input_table is a ptr to a NULL ptr (so we can pass it back)
		if (pp_input_table && (*pp_input_table == NULL))
		{
			NVM_UINT64 cfg_size = sizeof (struct config_input_table) + ext_table_length;
			*pp_input_table = malloc(cfg_size);
			struct config_input_table *p_input_table = *pp_input_table;

			if (p_input_table)
			{
				memset(p_input_table, 0, cfg_size);

				// Populate header
				memmove(p_input_table->header.signature, CONFIG_INPUT_TABLE_SIGNATURE,
						SIGNATURE_LEN);
				p_input_table->header.revision = 1;
				memmove(&p_input_table->header.oem_id, "INTEL ", OEM_ID_LEN);
				memmove(&p_input_table->header.oem_table_id, "PURLEY  ", OEM_TABLE_ID_LEN);
				p_input_table->header.oem_revision = 2;
				memmove(&p_input_table->header.creator_id, "INTL", sizeof (NVM_UINT32));
				p_input_table->header.creator_revision = 0;

				// Sequence number - let the BIOS know it's a fresh request
				p_input_table->sequence_number = seq_num;

				// Determine size based on size of extension tables
				p_input_table->header.length = cfg_size;

				// copy ext tables
				void *p_table = &(p_input_table->p_ext_tables);

				NVM_UINT64 table_size = partition_table.header.length;
				memmove(p_table, &partition_table, table_size);
				p_table += table_size;

				if (p_pm1_interleave_table)
				{
					table_size = p_pm1_interleave_table->header.length;
					memmove(p_table, p_pm1_interleave_table, table_size);
					p_table += table_size;

					if (p_pm2_interleave_table)
					{
						table_size = p_pm2_interleave_table->header.length;
						memmove(p_table, p_pm2_interleave_table, table_size);
						p_table += table_size;
					}
				}
			}
			else
			{
				COMMON_LOG_ERROR("Couldn't allocate memory for config input table");
				rc = NVM_ERR_NOMEMORY;
			}
		}
		else
		{
			COMMON_LOG_ERROR("Caller passed in a bad input table ptr");
			rc = NVM_ERR_INVALIDPARAMETER;
		}
	}

	// Free dynamically-sized ext tables
	if (p_pm1_interleave_table)
	{
		free(p_pm1_interleave_table);
	}

	if (p_pm2_interleave_table)
	{
		free(p_pm2_interleave_table);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Build the config tables for a DIMM config goal and write them to the
 * platform config.
 * If p_goal is NULL, we delete the existing goal.
 */
int update_config_goal(const struct device_discovery *p_discovery,
		const struct config_goal *p_goal,
		const struct nvm_capabilities *p_capabilities)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	struct platform_config_data *p_old_cfg = NULL;
	rc = get_dimm_platform_config(p_discovery->device_handle, &p_old_cfg);
	if (rc == NVM_SUCCESS)
	{
		// If a goal was provided, build a new config input table.
		struct config_input_table *p_input_table = NULL;
		if (p_goal)
		{
			// Generate next config input sequence number
			NVM_UINT32 seq_num = 0;
			seq_num = get_last_config_output_sequence_number(p_old_cfg) + 1;

			rc = config_goal_to_config_input(p_discovery->guid, p_goal, &p_input_table,
					p_capabilities, p_discovery, seq_num);
			if (rc == NVM_SUCCESS)
			{
				// Generate a checksum for our newly-generated input table
				generate_checksum((NVM_UINT8*)p_input_table,
						p_input_table->header.length,
						CHECKSUM_OFFSET);
			}
		}

		// Create the platform config table and write it
		if (rc == NVM_SUCCESS)
		{
			// Fetch current config and config output tables from existing data
			// We won't be changing these, but we need to avoid clobbering them when we
			// write our new input table.
			struct current_config_table *p_old_current_cfg = cast_current_config(p_old_cfg);
			struct config_output_table *p_old_output = cast_config_output(p_old_cfg);

			// build our new table
			struct platform_config_data *p_cfg_data = NULL;
			rc = build_platform_config_data(p_old_current_cfg, p_input_table, p_old_output,
					&p_cfg_data);
			if (rc == NVM_SUCCESS)
			{
				// Write it
				rc = set_dimm_platform_config(p_discovery->device_handle, p_cfg_data);
			}

			if (p_cfg_data) // clean up
			{
				free(p_cfg_data);
			}
		}

		// Free dynamically-allocated input table if needed
		if (p_input_table)
		{
			free(p_input_table);
		}
	}
	free(p_old_cfg);
	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Provision the capacity of the specified NVM-DIMM into one or more pools.
 */
int nvm_create_config_goal(const NVM_GUID device_guid,
		struct config_goal *p_goal)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	struct device_discovery discovery;

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else
	{
		// Get system capabilities so we can validate
		struct nvm_capabilities capabilities;
		rc = nvm_get_nvm_capabilities(&capabilities);
		if (rc == NVM_SUCCESS)
		{
			if (!capabilities.nvm_features.modify_device_capacity)
			{
				COMMON_LOG_WARN("Modifying device capacity is not supported.");
				rc = NVM_ERR_NOTSUPPORTED;
			}
			else if (device_guid == NULL)
			{
				COMMON_LOG_ERROR("Invalid parameter, device_guid is NULL");
				rc = NVM_ERR_INVALIDPARAMETER;
			}
			else if (p_goal == NULL)
			{
				COMMON_LOG_ERROR("Invalid parameter, p_goal is NULL");
				rc = NVM_ERR_INVALIDPARAMETER;
			}
			else if ((rc = exists_and_manageable(device_guid, &discovery, 1)) == NVM_SUCCESS)
			{
				// Check that no namespaces exist on this DIMM
				if ((rc = dimm_has_namespaces_of_type(discovery.device_handle,
						NAMESPACE_TYPE_UNKNOWN)) != 0)
				{
					if (rc > 0)
					{
						rc = NVM_ERR_NAMESPACESEXIST;
					}
				}
				else if ((rc = validate_config_goal(p_goal, &capabilities, &discovery))
						== NVM_SUCCESS)
				{
					rc = update_config_goal(&discovery, p_goal, &capabilities);
					if (rc == NVM_SUCCESS)
					{
						// Log an event indicating we successfully applied the goal
						NVM_EVENT_ARG guid_arg;
						guid_to_event_arg(device_guid, guid_arg);
						log_mgmt_event(EVENT_SEVERITY_INFO,
								EVENT_CODE_MGMT_CONFIG_GOAL_CREATED,
								device_guid,
								0, // no action required
								guid_arg, NULL, NULL);
					}
				}
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

enum config_goal_status get_config_goal_status_error_from_config_output(
		struct config_output_table *p_config_output)
{
	enum config_goal_status status = CONFIG_GOAL_STATUS_UNKNOWN;

	// config_error maps to our expected error states when applied to config output table
	enum config_error config_error = get_config_error_from_config_output(p_config_output);
	switch (config_error)
	{
		case CONFIG_ERROR_BADREQUEST:
		case CONFIG_ERROR_BROKENINTERLEAVE: // asked for DIMMs that don't exist
			status = CONFIG_GOAL_STATUS_ERR_BADREQUEST;
			break;
		case CONFIG_ERROR_FW:
			status = CONFIG_GOAL_STATUS_ERR_FW;
			break;
		case CONFIG_ERROR_INSUFFICIENTRESOURCES:
			status = CONFIG_GOAL_STATUS_ERR_INSUFFICIENTRESOURCES;
			break;
		case CONFIG_ERROR_UNKNOWN: // BIOS failed to apply goal but doesn't know why
			status = CONFIG_GOAL_STATUS_ERR_UNKNOWN;
			break;
		case CONFIG_ERROR_NOTFOUND: // Implies badly-formed output table
		default: // Anything that isn't a config output error is a surprise
			break;
	}

	return status;
}

enum config_goal_status get_config_goal_status_from_platform_config_data(
		struct platform_config_data *p_config)
{
	COMMON_LOG_ENTRY();

	enum config_goal_status status = CONFIG_GOAL_STATUS_UNKNOWN;

	struct config_input_table *p_input = cast_config_input(p_config);
	struct config_output_table *p_output = cast_config_output(p_config);
	if (p_input)
	{
		if (p_output)
		{
			if (p_output->sequence_number != p_input->sequence_number)
			{
				status = CONFIG_GOAL_STATUS_NEW;
			}
			else if (p_output->validation_status == CONFIG_OUTPUT_STATUS_SUCCESS)
			{
				status = CONFIG_GOAL_STATUS_SUCCESS;
			}
			else // some error occurred when applying the goal - check output table
			{
				status = get_config_goal_status_error_from_config_output(p_output);
			}
		}
		else // No config output
		{
			status = CONFIG_GOAL_STATUS_NEW;
		}
	}

	COMMON_LOG_EXIT_RETURN_I(status);
	return status;
}

/*
 * Helper function to convert a platform config data input table to a config
 * goal structure.
 */
int config_input_table_to_config_goal(const NVM_GUID device_guid,
		unsigned char *p_table,
		const NVM_UINT32 ext_table_offset,
		const NVM_UINT32 table_length,
		struct config_goal *p_config_goal)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	NVM_UINT32 offset = ext_table_offset;
	while (offset < table_length && rc == NVM_SUCCESS)
	{
		struct extension_table_header *p_header =
				(struct extension_table_header *)(p_table + offset);
		// check the length for validity
		if ((offset + p_header->length) > table_length)
		{
			COMMON_LOG_ERROR_F("Extension table length %d invalid", p_header->length);
			rc = NVM_ERR_BADDEVICECONFIG; // bad data
			break;
		}

		// get volatile size from the partition size change table
		if (p_header->type == PARTITION_CHANGE_TABLE)
		{
			struct partition_size_change_extension_table *p_partition =
					(struct partition_size_change_extension_table *)
					((void*)p_header);

			struct device_discovery discovery;
			rc = nvm_get_device_discovery(device_guid, &discovery);
			if (rc != NVM_SUCCESS) // failed to get discovery info
			{
				break;
			}

			// if no pm partition, all dimm capacity is volatile
			p_config_goal->volatile_size = USABLE_CAPACITY_BYTES(discovery.capacity) / BYTES_PER_GB;
			if (p_partition->partition_size != 0)
			{
				// else volatile size is total dimm capacity - pm partition
				p_config_goal->volatile_size -= (p_partition->partition_size / BYTES_PER_GB);
			}
		}
		// get persistent interleave sets from interleave table
		else if (p_header->type == INTERLEAVE_TABLE)
		{
			struct interleave_info_extension_table *p_interleave =
					(struct interleave_info_extension_table *)
					((void*)p_header);
			if (p_interleave->memory_type == INTERLEAVE_MEMORY_TYPE_PERSISTENT)
			{
				struct pm_attributes *p_qos = NULL;
				NVM_UINT64 *p_size = NULL;
				if (p_config_goal->persistent_1_size)
				{
					// bad data, more than 2 pm pools
					if (p_config_goal->persistent_2_size)
					{
						COMMON_LOG_ERROR("Bad config data, more than two PM interleave sets found");
						rc =  NVM_ERR_BADDEVICECONFIG;
						break;
					}
					// this is our second pm pool
					else
					{
						p_config_goal->persistent_count = 2;
						p_config_goal->persistent_2_set_id = p_interleave->index;
						p_qos = &p_config_goal->persistent_2_settings;
						p_size = &p_config_goal->persistent_2_size;
					}
				}
				// this is our first pm pool
				else
				{
					p_config_goal->persistent_count = 1;
					p_config_goal->persistent_1_set_id = p_interleave->index;
					p_qos = &p_config_goal->persistent_1_settings;
					p_size = &p_config_goal->persistent_1_size;
				}

				p_qos->mirrored = p_interleave->mirror_enable;
				interleave_format_to_struct(p_interleave->interleave_format, &p_qos->interleave);

				// fill out dimm info and calculate size
				int set_offset = offset + sizeof (struct interleave_info_extension_table);
				for (int i = 0; i < p_interleave->dimm_count; i++)
				{
					// check the length for validity
					if (set_offset > table_length)
					{
						COMMON_LOG_ERROR_F("Extension table length %d invalid", p_header->length);
						rc = NVM_ERR_BADDEVICECONFIG; // bad data
						break;
					}

					struct dimm_info_extension_table *p_dimm =
						(struct dimm_info_extension_table *)(p_table + set_offset);
					// dimm info to dimm guid
					struct device_discovery discovery;
					if (lookup_dev_manufacturer_serial_model(p_dimm->manufacturer,
							p_dimm->serial_number,
							p_dimm->model_number,
							&discovery) != NVM_SUCCESS)
					{
						char serial_str[NVM_SERIALSTR_LEN];
						SERIAL_NUMBER_TO_STRING(p_dimm->serial_number, serial_str);
						COMMON_LOG_ERROR_F("Interleave set dimm serial # %s not found",
								serial_str);
						rc = NVM_ERR_BADDEVICECONFIG; // bad data unrecognized device
						break;
					}
					memmove(p_qos->dimms[i], discovery.guid, NVM_GUID_LEN);

					// If this is the requested DIMM, get the size
					if (guid_cmp(p_qos->dimms[i], device_guid))
					{
						if (p_size)
						{
							*p_size = (p_dimm->size / BYTES_PER_GB);
						}
					}
					set_offset += sizeof (struct dimm_info_extension_table);
				}
			}
			// recognize but ignore volatile interleave tables
			else if (p_interleave->memory_type != INTERLEAVE_MEMORY_TYPE_VOLATILE)
			{
				COMMON_LOG_ERROR("Bad config data, unrecognized interleave set type");
				rc =  NVM_ERR_BADDEVICECONFIG;
				break;
			}
		}
		// go to next extension table
		offset += p_header->length;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Retrieve the configuration goal from the specified NVM-DIMM.
 */
int nvm_get_config_goal(const NVM_GUID device_guid, struct config_goal *p_goal)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	struct device_discovery discovery;

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if ((rc = IS_NVM_FEATURE_SUPPORTED(get_device_capacity)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Retrieving device capacity is not supported.");
	}
	else if (device_guid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, device_guid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_goal == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_goal is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = exists_and_manageable(device_guid, &discovery, 1)) == NVM_SUCCESS)
	{
		// assume no config goal
		memset(p_goal, 0, sizeof (*p_goal));

		// get the platform config data for this dimm
		struct platform_config_data *p_cfg_data = NULL;
		int tmp_rc = get_dimm_platform_config(discovery.device_handle, &p_cfg_data);
		if (tmp_rc != NVM_SUCCESS)
		{
			rc = tmp_rc; // propagate the error
		}
		else
		{
			struct config_input_table *p_config_input = cast_config_input(p_cfg_data);
			// no config input is valid and will return ERR_NOTFOUND in this case
			if (!p_config_input)
			{
				rc = NVM_ERR_NOTFOUND;
			}
			else
			{
				p_goal->status = get_config_goal_status_from_platform_config_data(p_cfg_data);

				// Fill in the remaining data from the config input table
				rc = config_input_table_to_config_goal(device_guid,
						(unsigned char *)p_config_input, sizeof (struct config_input_table),
						p_config_input->header.length, p_goal);
			}
			free(p_cfg_data);
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Change the configuration goal on an NVM-DIMM without reconfiguring the
 * it completely (e.g. modify the quality of service attributes
 * on existing interleave sets, add a persistent memory interleave set).
 */
int nvm_modify_config_goal(const NVM_GUID device_guid, struct config_goal *p_goal)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	// Differences between create and modify TBD.
	// For now, overwrite the existing goal.
	rc = nvm_create_config_goal(device_guid, p_goal);

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Erase the configuration goal from the specified NVM-DIMM.
 */
int nvm_delete_config_goal(const NVM_GUID device_guid)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	struct device_discovery discovery;

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if ((rc = IS_NVM_FEATURE_SUPPORTED(modify_device_capacity)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Modifying device capacity is not supported.");
	}
	else if (device_guid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, device_guid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = exists_and_manageable(device_guid, &discovery, 1)) == NVM_SUCCESS)
	{
		// Is there a config goal on this DIMM?
		struct config_goal current_goal;
		rc = nvm_get_config_goal(device_guid, &current_goal);
		if (rc == NVM_SUCCESS) // Goal found!
		{
			// Delete it
			rc = update_config_goal(&discovery, NULL, NULL);
			if (rc == NVM_SUCCESS)
			{
				// Log an event indicating we successfully removed the goal
				NVM_EVENT_ARG guid_arg;
				guid_to_event_arg(device_guid, guid_arg);
				log_mgmt_event(EVENT_SEVERITY_INFO,
						EVENT_CODE_MGMT_CONFIG_GOAL_DELETED,
						device_guid,
						0, // no action required
						guid_arg, NULL, NULL);

				// clear any action required events for the bad config goals on this dimm
				struct event_filter filter;
				memset(&filter, 0, sizeof (filter));
				filter.filter_mask = NVM_FILTER_ON_GUID | NVM_FILTER_ON_AR | NVM_FILTER_ON_TYPE;
				memmove(filter.guid, device_guid, NVM_GUID_LEN);
				filter.action_required = 1;
				filter.type = EVENT_TYPE_CONFIG;
				acknowledge_events(&filter);
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}
