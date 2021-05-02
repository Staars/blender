/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef WITH_METAL

#  include "device/metal/device_metal.h"
#  include "device/device.h"
#  include "device/device_intern.h"

#  include "clew.h"

#  include "util/util_foreach.h"
#  include "util/util_logging.h"
#  include "util/util_set.h"
#  include "util/util_string.h"

CCL_NAMESPACE_BEGIN

Device *device_metal_create(DeviceInfo &info, Stats &stats, Profiler &profiler, bool background)
{
  return new MetalDevice(info, stats, profiler, background);
}

bool device_metal_init()
{
  static bool initialized = false;
  static bool result = false;

  if (initialized)
    return result;

  initialized = true;

  if (true) {
    int clew_result = clewInit();
    if (clew_result == CLEW_SUCCESS) {
      VLOG(1) << "CLEW initialization succeeded.";
      result = true;
    }
    else {
      VLOG(1) << "CLEW initialization failed: "
              << ((clew_result == CLEW_ERROR_ATEXIT_FAILED) ? "Error setting up atexit() handler" :
                                                              "Error opening the library");
    }
  }
  else {
    VLOG(1) << "Skip initializing CLEW, platform is force disabled.";
    result = false;
  }

  return result;
}

//static cl_int device_metal_get_num_platforms_safe(cl_uint *num_platforms)
//{
//  *num_platforms = 1;
//  return CL_SUCCESS;
//  return CL_DEVICE_NOT_FOUND;
//}

void device_metal_info(vector<DeviceInfo> &devices)
{
//  cl_uint num_platforms = 0;
//  device_metal_get_num_platforms_safe(&num_platforms);
//  if (num_platforms == 0) {
//    return;
//  }

  vector<MetalPlatformDevice> usable_devices;
  MetalInfo::get_usable_devices(&usable_devices);
  /* Devices are numbered consecutively across platforms. */
//  VLOG(1) << "STAARS got usable devices";
  int num_devices = 0;
  set<string> unique_ids;
  foreach (MetalPlatformDevice &platform_device, usable_devices) {
    /* Compute unique ID for persistent user preferences. */
    const string &platform_name = platform_device.platform_name;
    const string &device_name = platform_device.device_name;
    string hardware_id = platform_device.hardware_id;
    if (hardware_id == "") {
      hardware_id = string_printf("ID_%d", num_devices);
    }
    string id = string("METAL_") + platform_name + "_" + device_name + "_" + hardware_id;

    /* Hardware ID might not be unique, add device number in that case. */
    if (unique_ids.find(id) != unique_ids.end()) {
      id += string_printf("_ID_%d", num_devices);
    }
    unique_ids.insert(id);

    /* Create DeviceInfo. */
    DeviceInfo info;
    info.type = DEVICE_METAL;
    info.description = string_remove_trademark(string(device_name));
    info.num = num_devices;
    /* We don't know if it's used for display, but assume it is. */
    info.display_device = true;
    info.use_split_kernel = true;
    info.has_volume_decoupled = false;
    info.has_adaptive_stop_per_sample = false;
    info.denoisers = DENOISER_NLM;
    info.id = id;

    /* Check Metal extensions */
    info.has_half_images = true;

    info.has_nanovdb = false;

    devices.push_back(info);
    num_devices++;
  }
}
string device_metal_capabilities()
{
  return "metal";
}

CCL_NAMESPACE_END

#endif /* WITH_METAL */

