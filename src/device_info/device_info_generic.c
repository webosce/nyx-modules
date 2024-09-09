// Copyright (c) 2012-2024 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

/*
*******************************************************************
* @file device_info_generic.c
*
* @brief The DEVICE_INFO module implementation.
    This file should only build for emulator.
*******************************************************************
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <openssl/sha.h>
#include <ctype.h>

#include <nyx/nyx_module.h>
#include <nyx/module/nyx_utils.h>
#include "msgid.h"

// Internal device info structure
typedef struct
{
	nyx_device_t original;
	const char *product_name;
	const char *device_name;
	const char *nduid_str;
	const char *wifi_mac;
	const char *wired_mac;
	const char *bdaddr;
	const char *devuid_str;
} device_info_device_t;

static const unsigned int  NDUID_LEN = SHA_DIGEST_LENGTH *
                                       2; /* 2 hex chars per byte */
static const char *const  NDUID_DIR = WEBOS_INSTALL_EXECSTATEDIR "/nyx";
static const char *const  NDUID_PATH = WEBOS_INSTALL_EXECSTATEDIR "/nyx/nduid";

static const char *const  read_eth0_mac =
    "ifconfig eth0 2>&1 | awk '/HWaddr/ {print $5}'";
static const char *const  read_wifi_mac =
    "ifconfig wlan0 2>&1 | awk '/HWaddr/ {print $5}'";

static const char *const  read_bdaddr =
    "hcitool dev 2>&1 | awk '/hci0/ {print $2}'";

static const char *const  DEVUID_PATH =
               "/sys/devices/soc0/serial_number";

NYX_DECLARE_MODULE(NYX_DEVICE_DEVICE_INFO, "DeviceInfo");

// Number of random bytes to read from /dev/urandom
static const unsigned int random_bytes = 16;

static nyx_error_t read_device_nduid(char nduid[NDUID_LEN + 1])
{
	nyx_error_t error = NYX_ERROR_NONE;
	FILE *fp = fopen(NDUID_PATH, "r");
	int ret = -1;

	if (!fp)
	{
		nyx_error(MSGID_NYX_MOD_OPEN_NDUID_ERR, 0, "Did not find stored nduid");
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}

	ret = fread(nduid, NDUID_LEN, 1, fp);

	if (ret <= 0)
	{
		nyx_error(MSGID_NYX_MOD_READ_NDUID_ERR, 0, "Error in reading nduid from %s",
		          NDUID_PATH);
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}

	nduid[NDUID_LEN] = '\0';
error:

	if (fp)
	{
		fclose(fp);
	}

	return error;
}

static nyx_error_t write_device_nduid(const char nduid[NDUID_LEN + 1])
{
	nyx_error_t error = NYX_ERROR_NONE;
	int ret = -1;

	if ((mkdir(NDUID_DIR, (mode_t)0755)  == -1) && (errno != EEXIST))
	{
		return NYX_ERROR_GENERIC;
	}

	FILE *fp = fopen(NDUID_PATH, "w");

	if (!fp)
	{
		nyx_error(MSGID_NYX_MOD_WRITE_NDUID_ERR, 0, "Error in opening file : %s",
		          NDUID_PATH);
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}

	ret = fwrite(nduid, NDUID_LEN, 1, fp);

	if (ret <= 0)
	{
		error = NYX_ERROR_GENERIC;
		goto error;
	}

	ret = chmod(NDUID_PATH, S_IRUSR | S_IRGRP | S_IROTH);

	if (ret < 0)
	{
		nyx_error(MSGID_NYX_MOD_CHMOD_ERR, 0, "Error in changing permissions for %s",
		          NDUID_PATH);
		error = NYX_ERROR_GENERIC;
	}

error:

	if (fp)
	{
		fclose(fp);
	}

	return error;
}

static nyx_error_t generate_device_nduid(char nduid[NDUID_LEN + 1])
{
	// Arbitrary bits selected as salt for SHA1 hashing
	char salt[] = {0x55, 0xaa, 0x30, 0x08, 0xce, 0xfa, 0xbe, 0xba};
	unsigned long offset = 0;
	unsigned char input[random_bytes + sizeof(salt)];
	unsigned char result[SHA_DIGEST_LENGTH];
	char *unique_id = NULL;
	nyx_error_t error = NYX_ERROR_NONE;
	FILE *fp = NULL;

	memcpy(input, salt, sizeof(salt));
	offset = sizeof(salt);

	// Using random bytes from /dev/urandom to get unique id
	// However unique ids like disk UUID, MAC address, IMEI no., and others
	// can be used when implementing for other MACHINE-s.

	fp = fopen("/dev/urandom", "r");

	if (fp)
	{
		unique_id = malloc(random_bytes);

		if (!unique_id)
		{
			error = NYX_ERROR_OUT_OF_MEMORY;
			goto error;
		}

		int ret = fread(unique_id, random_bytes, 1, fp);

		if (ret <= 0)
		{
			nyx_error(MSGID_NYX_MOD_URANDOM_ERR, 0, "Error in reading from /dev/urandom");
			error = NYX_ERROR_GENERIC;
			goto error;
		}
	}
	else
	{
		nyx_error(MSGID_NYX_MOD_URANDOM_OPEN_ERR, 0, "Error in opening /dev/urandom");
		error = NYX_ERROR_GENERIC;
		goto error;
	}

	memcpy(input + offset, unique_id, random_bytes);
	offset += random_bytes;

	SHA1(input, offset, result);

	/* Need 3 bytes to print out a byte as a hex string */
	char *sptr;
	sptr = nduid;

	for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
	{
		snprintf(sptr, 3, "%02x", result[i]);
		sptr += 2;
	}

	*sptr = '\0';

	error = write_device_nduid(nduid);

error:

	if (fp)
	{
		fclose(fp);
	}

	free(unique_id);
	return error;
}

static nyx_error_t get_device_nduid(char nduid[NDUID_LEN + 1])
{
	if (NULL == nduid)
	{
		return NYX_ERROR_INVALID_VALUE;
	}

	if (read_device_nduid(nduid) != NYX_ERROR_NONE)
	{
		return generate_device_nduid(nduid);
	}

	return NYX_ERROR_NONE;
}

static nyx_error_t get_device_unique_id(char ** target)
{
	nyx_error_t error = NYX_ERROR_NONE;

	if (NULL != *target)
	{
		free(*target);
		*target = NULL;
	}

	FILE *fp = fopen(DEVUID_PATH, "r");

	if (!fp)
	{
		nyx_error(MSGID_NYX_MOD_DEVICEID_OPEN_ERR,0,"Error in Opening File : %s",DEVUID_PATH);
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}

	size_t temp_size = 0;

	// when delim is set to 'EOF' getdelim supports multiple lines
	ssize_t read_count = getdelim(target, &temp_size, EOF, fp);

	if (-1 == read_count)
	{
		if (*target)
		{
			free(*target);
			*target = NULL;
		}
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}
	else
	{
		// remove unnecessary extra endline character
		if (read_count > 0 && (*target)[read_count - 1] == '\n')
		{
			(*target)[read_count - 1] = '\0';
		}
	}

error:
	if (fp)
	{
		fclose(fp);
	}

	return error;
}

/*
* Input parameters:
* command - command line command to execute
* target  - output from the command. If *target != NULL, it must be
*           a dynamically allocated buffer (since it will be freed)
*/
static nyx_error_t execute_read_info(const char *command, char **target)
{
	FILE *fp = NULL;
	nyx_error_t retVal = NYX_ERROR_GENERIC;

	if (NULL != *target)
	{
		free(*target);
		*target = NULL;
	}

	fp = popen(command, "r");

	if (NULL != fp)
	{
		size_t temp_size = 0;
		ssize_t read_count = 0;
		// when delim is set to 'EOF' getdelim supports multiple lines
		read_count = getdelim(target, &temp_size, EOF, fp);

		if (-1 == read_count)
		{
			if (*target)
			{
				free(*target);
				*target = NULL;
			}

			retVal = NYX_ERROR_DEVICE_UNAVAILABLE;
		}
		else
		{
			// remove unnecessary extra endline character
			if (read_count > 0 && (*target)[read_count - 1] == '\n')
			{
				(*target)[read_count - 1] = '\0';
			}

			retVal = NYX_ERROR_NONE;

		}

		pclose(fp);
	}
	else
	{
		retVal = NYX_ERROR_DEVICE_UNAVAILABLE;
	}

	return retVal;
}

static void trim_whitespaces(char *str)
{
	if (!str) return;
	size_t len = strlen(str);
	if (len == 0) return;

	int start = 0;
	while (isspace(str[start]))
		start++;

	int end = len - 1;
	while (end > start && isspace(str[end]))
		end--;

	int shiftIndex = 0;
	for (int i = start; i <= end; i++)
		str[shiftIndex++] = str[i];

	str[shiftIndex] = '\0';
}

// Returns a value string that matches the given key string in /etc/buildinfo
// The caller is responsible to deallocate the returned string.
#define BUILDINFO_MAX_LINE_LENGTH 512
static char *get_buildinfo(const char *key)
{
	FILE *fp = fopen("/etc/buildinfo", "r");
	if (fp == NULL)
		return NULL;

	char line[BUILDINFO_MAX_LINE_LENGTH];
	char *value = NULL;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (line[0] == '#' || line[0] == '\n')
			continue;

		char *sep = strchr(line, '=');
		if (sep != NULL) {
			*sep = '\0';
			char k[BUILDINFO_MAX_LINE_LENGTH];
			char v[BUILDINFO_MAX_LINE_LENGTH];
			strncpy(k, line, sizeof(k));
			strncpy(v, sep + 1, sizeof(v));
			trim_whitespaces(k);
			trim_whitespaces(v);
			if (strcmp(key, k) == 0) {
				value = strdup(v);
				break;
			}
		}
	}

	fclose(fp);
	return value;
}

nyx_error_t nyx_module_open(nyx_instance_t i, nyx_device_t **d)
{
	if (NULL == d)
	{
		nyx_error(MSGID_NYX_MOD_DEV_INFO_OPEN_ERR, 0, "System module already open.");
		return NYX_ERROR_INVALID_VALUE;
	}

	device_info_device_t *device = (device_info_device_t *)calloc(sizeof(
	                                   device_info_device_t), 1);
	nyx_error_t error = NYX_ERROR_NONE;

	if (device == NULL)
	{
		nyx_error(MSGID_NYX_MOD_MALLOC_ERR1, 0 , "Error in allocation memory");
		error = NYX_ERROR_OUT_OF_MEMORY;
		goto out;
	}

	/* device_info_get_info is deprecated */
	nyx_module_register_method(i, (nyx_device_t *)device,
	                           NYX_DEVICE_INFO_GET_INFO_MODULE_METHOD, "device_info_get_info");
	nyx_module_register_method(i, (nyx_device_t *)device,
	                           NYX_DEVICE_INFO_QUERY_MODULE_METHOD, "device_info_query");

	device->nduid_str = (const char *)malloc(sizeof(char) * (NDUID_LEN + 1));

	if (device->nduid_str == NULL)
	{
		nyx_error(MSGID_NYX_MOD_MALLOC_ERR2, 0 , "Error in allocation memory");
		error = NYX_ERROR_OUT_OF_MEMORY;
		goto out;
	}

	error = get_device_nduid((char *)device->nduid_str);

	if (error)
	{
		goto nduid_err;
	}

	char *machine = get_buildinfo("MACHINE");
	device->product_name = machine;
	device->device_name = machine;
	device->wifi_mac = NULL;
	device->wired_mac = NULL;
	device->bdaddr = NULL;
	device->devuid_str = NULL;

	*d = (nyx_device_t *)device;
	return error;

nduid_err:
	free((void *) device->nduid_str);
out:
	free(device);
	*d = NULL;
	return error;
}

nyx_error_t nyx_module_close(nyx_device_handle_t d)
{
	if (NULL == d)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	device_info_device_t *device_info = (device_info_device_t *)d;

	if (NULL != device_info->wifi_mac)
	{
		free((void *) device_info->wifi_mac);
		device_info->wifi_mac = NULL;
	}

	if (NULL != device_info->wired_mac)
	{
		free((void *) device_info->wired_mac);
		device_info->wired_mac = NULL;
	}

	if (NULL != device_info->bdaddr)
	{
		free((void *) device_info->bdaddr);
		device_info->bdaddr = NULL;
	}

	if (NULL != device_info->devuid_str)
	{
		free((void *) device_info->devuid_str);
		device_info->devuid_str = NULL;
	}

	free((void *) device_info->nduid_str);
	free(device_info);
	return NYX_ERROR_NONE;
}

static nyx_error_t copy_line(const char *src, char *dest, size_t dest_len)
{
	if (NULL == src)
	{
		return NYX_ERROR_OUT_OF_MEMORY;
	}

	if (NULL == dest || 0 == dest_len)
	{
		return NYX_ERROR_GENERIC;
	}

	if (strlen(src) >= dest_len)
	{
		return NYX_ERROR_VALUE_OUT_OF_RANGE;
	}

	strncpy(dest, src, dest_len);
	dest[dest_len - 1] = '\0';

	return NYX_ERROR_NONE;
}

nyx_error_t device_info_query(nyx_device_handle_t d,
                              nyx_device_info_type_t query, const char **dest)
{
	// default error is NYX_ERROR_NONE to minimize code amount
	nyx_error_t error = NYX_ERROR_NONE;

	if (NULL == d)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	device_info_device_t *dev = (device_info_device_t *) d;
	// return an empty string if there's an error
	*dest = "";

	switch (query)
	{
		case NYX_DEVICE_INFO_BATT_CH:
		case NYX_DEVICE_INFO_BATT_RSP:
		case NYX_DEVICE_INFO_BOARD_TYPE:
		case NYX_DEVICE_INFO_HARDWARE_ID:
		case NYX_DEVICE_INFO_HARDWARE_REVISION:
		case NYX_DEVICE_INFO_INSTALLER:
		case NYX_DEVICE_INFO_KEYBOARD_TYPE:
		case NYX_DEVICE_INFO_LAST_RESET_TYPE:
		case NYX_DEVICE_INFO_PRODUCT_ID:
		case NYX_DEVICE_INFO_RADIO_TYPE:
		case NYX_DEVICE_INFO_SERIAL_NUMBER:
		case NYX_DEVICE_INFO_STORAGE_FREE:

		// Need to figure out the right way to get ram size
		// The "MemTotal" value in /proc/meminfo doesn't match with the actual size
		case NYX_DEVICE_INFO_RAM_SIZE:

		// Need to figure out how to round off the value obtained
		// from running statfs on root filesystem
		case NYX_DEVICE_INFO_STORAGE_SIZE:
			error = NYX_ERROR_NOT_IMPLEMENTED;
			break;

		case NYX_DEVICE_INFO_BT_ADDR:
			error = execute_read_info(read_bdaddr, (char **)&dev->bdaddr);

			if (NYX_ERROR_NONE == error)
			{
				*dest = dev->bdaddr;
			}

			break;

		case NYX_DEVICE_INFO_WIFI_ADDR:
			error = execute_read_info(read_wifi_mac, (char **)&dev->wifi_mac);

			if (NYX_ERROR_NONE == error)
			{
				*dest = dev->wifi_mac;
			}

			break;

		case NYX_DEVICE_INFO_WIRED_ADDR:
			error = execute_read_info(read_eth0_mac, (char **)&dev->wired_mac);

			if (NYX_ERROR_NONE == error)
			{
				*dest = dev->wired_mac;
			}

			break;

		case NYX_DEVICE_INFO_MODEM_PRESENT:
			*dest = "N";
			break;

		case NYX_DEVICE_INFO_DEVICE_NAME:
			*dest = dev->device_name;
			break;

		case NYX_DEVICE_INFO_NDUID:
			*dest = dev->nduid_str;
			break;

		case NYX_DEVICE_INFO_DEVICE_ID:
			error = get_device_unique_id((char **)&dev->devuid_str);

			if (NYX_ERROR_NONE == error)
			{
				*dest = dev->devuid_str;
			}
			break;

		default:
			error = NYX_ERROR_INVALID_VALUE;
			break;
	}

	return error;
}

/* device_info_get_info is deprecated */
nyx_error_t device_info_get_info(nyx_device_handle_t d,
                                 nyx_device_info_type_t type, char *dest, size_t dest_len)
{
	const char *tmp_dest = NULL;

	nyx_error_t err = device_info_query(d, type, &tmp_dest);

	if (NYX_ERROR_NONE == err)
	{
		err = copy_line(tmp_dest, dest, dest_len);
	}

	return err;
}

// TODO: Work on the following code to get appropriate ram size and storage size values

#if 0
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include <limits.h>

// This overestimates, as it includes any U or L suffix characters. Note that sizeof() counts the terminating '\0'.
#define _MAX_DIGITS_INTEGER(max_val) (sizeof(#max_val)+1) /* +1 for a possible minus sign */
#define MAX_DIGITS_INTEGER(max_val) _MAX_DIGITS_INTEGER(max_val)

static const size_t numeric_string_len = MAX_DIGITS_INTEGER(
            ULLONG_MAX) + 3; /* +3 for the " [KMG]B" unit of measure */

// KB to MB or MB to GB conversion factor
static const unsigned int bytes_converter = 1024;

tatic nyx_error_t get_ram_size(char *ram_size, int size_len)
{
	char line[256];
	int val;
	nyx_error_t error = NYX_ERROR_NONE;

	if (numeric_string_len >= size_len)
	{
		return NYX_ERROR_VALUE_OUT_OF_RANGE;
	}

	FILE *fp = fopen("/proc/meminfo", "r");

	if (!fp)
	{
		nyx_error(MSGID_NYX_MOD_MEMINFO_OPEN_ERR, 0, "Unable to open /proc/meminfo");
		error = NYX_ERROR_NOT_FOUND;
		goto error;
	}

	if (fgets(line, sizeof(line), fp) <= 0)
	{
		error = NYX_ERROR_GENERIC;
		goto error;
	}

	sscanf(line, "MemTotal:        %dKB", &val);

	// Convert Bytes to MB or GB
	if (val > bytes_converter)
	{
		// Round off the value
		val = (val + 500) / bytes_converter;

		if (val > bytes_converter)
		{
			val = (val + 500) / bytes_converter;
			sprintf(ram_size, "%d GB", val);
		}
		else
		{
			sprintf(ram_size, "%d MB", val);
		}
	}
	else
	{
		sprintf(ram_size, "%d KB", val);
	}

error:

	if (fp)
	{
		fclose(fp);
	}

	return error;
}


static nyx_error_t get_storage_size(char *storage_size, int size_len)
{
	uint64_t lluBytes, size;

	struct statfs buf;

	if (numeric_string_len >= size_len)
	{
		return NYX_ERROR_VALUE_OUT_OF_RANGE;
	}

	if (0 == statfs("/", &buf))
	{
		lluBytes = buf.f_blocks * (uint64_t) buf.f_bsize;
	}
	else
	{
		nyx_error(MSGID_NYX_MOD_STORAGE_ERR, 0, "Error in getting root storage size");
		return NYX_ERROR_GENERIC;
	}

	// Convert bytes to MB or GB
	size = lluBytes / (bytes_converter * bytes_converter);

	if (size > bytes_converter)
	{
		// Round off the value
		size = (size + 500) / bytes_converter;
		sprintf(storage_size, "%llu GB", size);
	}
	else
	{
		sprintf(storage_size, "%llu MB", size);
	}

	return NYX_ERROR_NONE;
}
#endif
