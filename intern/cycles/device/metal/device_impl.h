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

#ifdef WITH_METAL

#  include "device/metal/kernel.h"
#  include "device/metal/queue.h"
#  include "device/metal/util.h"
#  include "device/device.h"
#  include <Metal/Metal.h>

#  include "util/util_map.h"

CCL_NAMESPACE_BEGIN

class DeviceQueue;

class METALDevice : public Device {

  friend class METALContextScope;

 public:
  id<MTLDevice> mtlDevice;
  id<MTLCommandQueue> mtlQueue;
  id<MTLLibrary> mtlLibrary;
  id<MTLCommandBuffer> mtlCommandBuffer;
  string device_name;
  
  // borrowed from Cuda, let's see if it is usable
  size_t device_texture_headroom;
  size_t device_working_headroom;
  bool move_texture_to_host;
  size_t map_host_used;
  size_t map_host_limit;
  int can_map_host;
  int pitch_alignment;
  bool first_error;

  struct METALMem {
    METALMem() : texobject(0), array(0), use_mapped_host(false)
    {
    }

    char texobject;
    char array;

    /* If true, a mapped host memory in shared_pointer is being used. */
    bool use_mapped_host;
  };
  typedef map<device_memory *, METALMem> METALMemMap;
  METALMemMap metal_mem_map;
  thread_mutex metal_mem_map_mutex;

  struct PixelMem {
    int cuPBO;
    char cuPBOresource;
    int cuTexId;
    int w, h;
  };
  map<device_ptr, PixelMem> pixel_mem_map;

  /* Bindless Textures */
  device_vector<TextureInfo> texture_info;
  bool need_texture_info;

  METALDeviceKernels kernels;

  static bool have_precompiled_kernels();

  virtual bool show_samples() const override;

  virtual BVHLayoutMask get_bvh_layout_mask() const override;

  void set_error(const string &error) override;

  METALDevice(const DeviceInfo &info, Stats &stats, Profiler &profiler);

  virtual ~METALDevice();

  bool support_device(const DeviceRequestedFeatures & /*requested_features*/);

  bool check_peer_access(Device *peer_device) override;

  bool use_adaptive_compilation();

  virtual string compile_kernel_get_common_cflags(
      const DeviceRequestedFeatures &requested_features);

  string compile_kernel(const DeviceRequestedFeatures &requested_features,
                        const char *name,
                        const char *base = "metal",
                        bool force_ptx = false);

  virtual bool load_kernels(const DeviceRequestedFeatures &requested_features) override;

  void reserve_local_memory(const DeviceRequestedFeatures &requested_features);

  void init_host_memory();

  void load_texture_info();

  void move_textures_to_host(size_t size, bool for_texture);

  METALMem *generic_alloc(device_memory &mem, size_t pitch_padding = 0);

  void generic_copy_to(device_memory &mem);

  void generic_free(device_memory &mem);

  void mem_alloc(device_memory &mem) override;

  void mem_copy_to(device_memory &mem) override;

  void mem_copy_from(device_memory &mem, int y, int w, int h, int elem) override;

  void mem_zero(device_memory &mem) override;

  void mem_free(device_memory &mem) override;

  device_ptr mem_alloc_sub_ptr(device_memory &mem, int offset, int /*size*/) override;

  virtual void const_copy_to(const char *name, void *host, size_t size) override;

  void global_alloc(device_memory &mem);

  void global_free(device_memory &mem);

  void tex_alloc(device_texture &mem);

  void tex_free(device_texture &mem);

  /* Graphics resources interoperability. */
  virtual bool should_use_graphics_interop() override;
  virtual unique_ptr<DeviceGraphicsInterop> graphics_interop_create() override;

  virtual unique_ptr<DeviceQueue> gpu_queue_create() override;
};

CCL_NAMESPACE_END

#endif
