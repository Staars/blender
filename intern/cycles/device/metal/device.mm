/*
 * Copyright 2011-2013 Blender Foundation
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

#include "device/metal/device.h"

#include "util/util_logging.h"

#ifdef WITH_METAL
#  include <Metal/Metal.h>
#  include "device/metal/device_impl.h"
#  include "device/device.h"

#  include "util/util_string.h"
#  include "util/util_windows.h"
#endif /* WITH_METAL */

CCL_NAMESPACE_BEGIN

bool device_metal_init()
{
#if !defined(WITH_METAL)
  return false;
#else

  bool initialized = false;
  
  NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
  for (id<MTLDevice> device in devices) {
      if (device.supportsRaytracing && !device.isLowPower) {
        initialized = true;
        VLOG(1) << "Metal device with ray tracing support found: " << device.name.UTF8String;
        break;
      }
  }
  if (!initialized) {
    VLOG(1) << "No Metal device with ray tracing support found!\n";
  }
  return initialized;
#endif //WITH_METAL
}

Device *device_metal_create(const DeviceInfo &info, Stats &stats, Profiler &profiler)
{
  VLOG(1) << "device_metal_create\n";

#ifdef WITH_METAL
  return new METALDevice(info, stats, profiler);
#else
  (void)info;
  (void)stats;
  (void)profiler;

  LOG(FATAL) << "Request to create METAL device without compiled-in support. Should never happen.";

  return nullptr;
#endif
}

//#ifdef WITH_METAL
//static void device_metal_safe_init()
//{
//  // probably not necessary
//}
//#endif /* WITH_METAL */

void device_metal_info(vector<DeviceInfo> &devices)
{
  VLOG(1) << "device_metal_info\n";
  
#ifdef WITH_METAL
  vector<DeviceInfo> display_devices;
  NSArray<id<MTLDevice>> *mtlDevices = MTLCopyAllDevices();
  for (id<MTLDevice> device in mtlDevices) {
      if (device.supportsRaytracing && !device.isLowPower) {
        char name[256];
        DeviceInfo info;
        info.type = DEVICE_METAL;
        info.description = device.name.UTF8String;
        info.num = 0;
        info.id = 666;
        info.has_peer_memory = false;
        info.has_half_images = false;
        info.has_nanovdb = false;
        info.has_volume_decoupled = false;
        info.denoisers = 0;
        info.display_device = true;
        display_devices.push_back(info);

        VLOG(1) << "Addedd device: " << device.name.UTF8String;
        break;
      }
  }
  if (!display_devices.empty())
    devices.insert(devices.end(), display_devices.begin(), display_devices.end());
  
#else  /* WITH_METAL */
  (void)devices;
#endif /* WITH_METAL */
}

string device_metal_capabilities()
{
  VLOG(1) << "device_metal_capabilities\n";

#ifdef WITH_METAL
  
  string capabilities = "Some Metal ...";
  return capabilities;

#else  /* WITH_METAL */
  return "";
#endif /* WITH_METAL */
}

CCL_NAMESPACE_END
