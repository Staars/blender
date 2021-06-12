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

#include "integrator/pass_accessor_gpu.h"

#include "device/device_queue.h"
#include "render/buffers.h"
#include "util/util_logging.h"

CCL_NAMESPACE_BEGIN

PassAccessorGPU::PassAccessorGPU(DeviceQueue *queue,
                                 const PassAccessInfo &pass_access_info,
                                 float exposure,
                                 int num_samples)
    : PassAccessor(pass_access_info, exposure, num_samples), queue_(queue)

{
}

/* --------------------------------------------------------------------
 * Kernel execution.
 */

void PassAccessorGPU::run_film_convert_kernels(DeviceKernel kernel,
                                               const RenderBuffers *render_buffers,
                                               const BufferParams &buffer_params,
                                               const Destination &destination) const
{
  KernelFilmConvert kfilm_convert;
  init_kernel_film_convert(&kfilm_convert, buffer_params, destination);

  const int work_size = buffer_params.width * buffer_params.height;

  if (destination.d_pixels_half_rgba) {
    void *args[] = {const_cast<KernelFilmConvert *>(&kfilm_convert),
                    const_cast<device_ptr *>(&destination.d_pixels_half_rgba),
                    const_cast<device_ptr *>(&render_buffers->buffer.device_pointer),
                    const_cast<int *>(&work_size),
                    const_cast<int *>(&buffer_params.offset),
                    const_cast<int *>(&buffer_params.stride)};

    queue_->enqueue(kernel, work_size, args);
  }

  queue_->synchronize();
}

/* --------------------------------------------------------------------
 * Pass accessors.
 */

#define DEFINE_PASS_ACCESSOR(pass, kernel_pass) \
  void PassAccessorGPU::get_pass_##pass(const RenderBuffers *render_buffers, \
                                        const BufferParams &buffer_params, \
                                        const Destination &destination) const \
  { \
    run_film_convert_kernels(DEVICE_KERNEL_FILM_CONVERT_##kernel_pass##_HALF_RGBA, \
                             render_buffers, \
                             buffer_params, \
                             destination); \
  }

/* Float (scalar) passes. */
DEFINE_PASS_ACCESSOR(depth, DEPTH);
DEFINE_PASS_ACCESSOR(mist, MIST);
DEFINE_PASS_ACCESSOR(sample_count, SAMPLE_COUNT);
DEFINE_PASS_ACCESSOR(float, FLOAT);

/* Float3 passes. */
DEFINE_PASS_ACCESSOR(divide_even_color, DIVIDE_EVEN_COLOR);
DEFINE_PASS_ACCESSOR(float3, FLOAT3);

/* Float4 passes. */
DEFINE_PASS_ACCESSOR(motion, MOTION);
DEFINE_PASS_ACCESSOR(cryptomatte, CRYPTOMATTE);
DEFINE_PASS_ACCESSOR(shadow_catcher, SHADOW_CATCHER);
DEFINE_PASS_ACCESSOR(shadow_catcher_matte_with_shadow, SHADOW_CATCHER_MATTE_WITH_SHADOW);
DEFINE_PASS_ACCESSOR(float4, FLOAT4);

/* Float3 or Float4 passes. */
DEFINE_PASS_ACCESSOR(shadow, SHADOW);

#undef DEFINE_PASS_ACCESSOR

CCL_NAMESPACE_END
