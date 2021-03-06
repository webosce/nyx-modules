// Copyright (c) 2010-2018 LG Electronics, Inc.
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
************************************************
* @file system.c
************************************************
*/

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <glib.h>
#include "rtc.h"

#include <nyx/nyx_module.h>
#include <nyx/module/nyx_utils.h>
#include "msgid.h"

nyx_device_t *nyxDev;
nyx_device_callback_function_t alarm_fired_callback = NULL;
bool reformatted = false;

NYX_DECLARE_MODULE(NYX_DEVICE_SYSTEM, "System");

void AlarmFiredCB(void)
{
	if (alarm_fired_callback)
	{
		alarm_fired_callback(nyxDev, NYX_CALLBACK_STATUS_DONE, NULL);
	}
}

nyx_error_t nyx_module_open(nyx_instance_t i, nyx_device_t **d)
{

	if (NULL == d)
	{
		nyx_error(MSGID_NYX_MOD_SYSTEM_OPEN_ERR, 0, "System module already open.");
		return NYX_ERROR_INVALID_VALUE;
	}

	if (nyxDev)
	{
		return NYX_ERROR_NONE;
	}

	nyxDev = (nyx_device_t *)calloc(sizeof(nyx_device_t), 1);

	if (NULL == nyxDev)
	{
		nyx_error(MSGID_NYX_MOD_SYSTEM_OUT_OF_MEMORY, 0, "Out of memory");
		return NYX_ERROR_OUT_OF_MEMORY;
	}

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_SET_ALARM_MODULE_METHOD,
	                           "system_set_alarm");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_QUERY_NEXT_ALARM_MODULE_METHOD,
	                           "system_query_next_alarm");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_QUERY_RTC_TIME_MODULE_METHOD,
	                           "system_query_rtc_time");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_SUSPEND_MODULE_METHOD,
	                           "system_suspend");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_SHUTDOWN_MODULE_METHOD,
	                           "system_shutdown");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_REBOOT_MODULE_METHOD,
	                           "system_reboot");

	nyx_module_register_method(i, (nyx_device_t *)nyxDev,
	                           NYX_SYSTEM_ERASE_PARTITION_MODULE_METHOD,
	                           "system_erase_partition");

	*d = (nyx_device_t *)nyxDev;
	return NYX_ERROR_NONE;
}

nyx_error_t nyx_module_close(nyx_device_t *d)
{
	rtc_close();
	return NYX_ERROR_NONE;
}

nyx_error_t system_set_alarm(nyx_device_handle_t handle, time_t time,
                             nyx_device_callback_function_t callback_func, void *context)
{
	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	if (rtc_open() == 0)
	{
		return NYX_ERROR_INVALID_OPERATION;
	}

	if (!time)
	{
		rtc_clear_alarm();
	}
	else
	{
		if (rtc_set_alarm_time(time) == 0)
		{
			return NYX_ERROR_INVALID_OPERATION;
		}

		if (callback_func)
		{
			alarm_fired_callback = callback_func;
			rtc_add_watch(AlarmFiredCB);
		}
		else
		{
			alarm_fired_callback = NULL;
			rtc_clear_watch();
		}
	}

	return NYX_ERROR_NONE;
}

nyx_error_t system_query_next_alarm(nyx_device_handle_t handle, time_t *time)
{
	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	if (rtc_open() == 0)
	{
		return NYX_ERROR_INVALID_OPERATION;
	}

	if (rtc_read_alarm_time(time) < 0)
	{
		return NYX_ERROR_INVALID_OPERATION;
	}

	return NYX_ERROR_NONE;
}

nyx_error_t system_query_rtc_time(nyx_device_handle_t handle, time_t *time)
{
	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	if (rtc_open() == 0)
	{
		return NYX_ERROR_INVALID_OPERATION;
	}

	if (rtc_time(time) < 0)
	{
		return NYX_ERROR_INVALID_OPERATION;
	}

	return NYX_ERROR_NONE;
}


nyx_error_t system_suspend(nyx_device_handle_t handle, bool *success)
{
	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	int32_t ret = access("/usr/sbin/suspend_action", R_OK | X_OK);

	if (ret || (success == NULL))
	{
		/* dummy sleep function */
		sleep(5);
		ret = 0;
	}
	else
	{
		ret = system("/usr/sbin/suspend_action");
	}

	if (success)
	{
		if (ret < 0)
		{
			*success = false;
		}
		else
		{
			*success = true;
		}
	}

	return NYX_ERROR_NONE;
}


nyx_error_t system_shutdown(nyx_device_handle_t handle ,
                            nyx_system_shutdown_type_t type, const char *reason)
{
	int ret = 0;

	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	switch (type)
	{
		case NYX_SYSTEM_EMERG_SHUTDOWN:
			ret = system("halt -f");
			break;

		case NYX_SYSTEM_NORMAL_SHUTDOWN:
		case NYX_SYSTEM_TEST_SHUTDOWN:
		default:
			ret = system("shutdown -h now");
			break;
	}

	if (ret < 0)
	{
		return NYX_ERROR_GENERIC;
	}

	return NYX_ERROR_NONE;
}


nyx_error_t system_reboot(nyx_device_handle_t handle ,
                          nyx_system_shutdown_type_t type, const char *reason)
{
	int ret = 0;

	if (handle != nyxDev)
	{
		return NYX_ERROR_INVALID_HANDLE;
	}

	switch (type)
	{
		case NYX_SYSTEM_EMERG_SHUTDOWN:
			ret = system("reboot -f");
			break;

		case NYX_SYSTEM_NORMAL_SHUTDOWN:
		case NYX_SYSTEM_TEST_SHUTDOWN:
		default:
			ret = system("reboot");
			break;
	}

	if (ret < 0)
	{
		return NYX_ERROR_GENERIC;
	}

	return NYX_ERROR_NONE;
}


nyx_error_t system_erase_partition(nyx_device_handle_t handle,
                                   nyx_system_erase_type_t type)
{
	return NYX_ERROR_NOT_IMPLEMENTED;
}
