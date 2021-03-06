/*
 * Copyright (c) 2015 2017, Intel Corporation
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
 * Implementations of ACPI helper functions for Linux
 */

#include "acpi.h"
#include <string/s_str.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

#define	SYSFS_ACPI_PATH	"/sys/firmware/acpi/tables/"
int g_count = 0;

/*!
 * Return the specified ACPI table or the size
 * required
 */
int get_acpi_table(
		const char *signature,
		struct acpi_table *p_table,
		const unsigned int size)
{
	int rc = 0;

	char table_path[PATH_MAX];
	s_strncpy(table_path, PATH_MAX, SYSFS_ACPI_PATH,
		strnlen(SYSFS_ACPI_PATH, PATH_MAX));
	s_strncat(table_path, PATH_MAX, signature, strnlen(signature, PATH_MAX));

	int fd = open(table_path, O_RDONLY|O_CLOEXEC);
	if (fd < 0)
	{
		rc = ACPI_ERR_TABLENOTFOUND;
	}
	else
	{
		struct acpi_table_header header;
		size_t header_size = sizeof (header);

		ssize_t hdr_bytes_read = read(fd, &header, header_size);
		if (hdr_bytes_read != header_size)
		{
			rc = ACPI_ERR_BADTABLE;
		}
		else
		{
			size_t total_table_size = header.length;
			rc = (int)total_table_size;
			if (p_table)
			{
				memset(p_table, 0, size);
				memcpy(&(p_table->header), &header, header_size);
				if (size < total_table_size)
				{
					rc = ACPI_ERR_BADTABLE;
				}
				else
				{
					size_t requested_bytes = total_table_size - header_size;
					unsigned char *p_buff = p_table->p_ext_tables;
					ssize_t total_read = 0;

					while (total_read < requested_bytes)
					{
						ssize_t bytes_read = read(fd, p_buff, requested_bytes);
						p_buff += bytes_read;
						total_read += bytes_read;
					}

					if (total_read != requested_bytes)
					{
						rc = ACPI_ERR_BADTABLE;
					}
					else
					{
						rc = check_acpi_table(signature, p_table);
					}
				}
			}
		}
		close(fd);
	}

	return rc;
}
