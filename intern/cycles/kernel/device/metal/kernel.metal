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

/* METAL kernel entry points */

#ifdef __APPLE__
#  include "kernel/device/metal/compat.h"
#  include "kernel/device/metal/globals.h"
//#  include "kernel/device/metal/image.h"
#  include "kernel/device/metal/parallel_active_index.h"
#  include "kernel/device/metal/parallel_prefix_sum.h"
#  include "kernel/device/metal/parallel_sorted_index.h"

//#  include "kernel/integrator/integrator_state.h"
//#  include "kernel/integrator/integrator_state_flow.h"
//#  include "kernel/integrator/integrator_state_util.h"
//
#  include "kernel/integrator/integrator_init_from_camera.h"
//#  include "kernel/integrator/integrator_intersect_closest.h"
//#  include "kernel/integrator/integrator_intersect_shadow.h"
//#  include "kernel/integrator/integrator_intersect_subsurface.h"
//#  include "kernel/integrator/integrator_megakernel.h"
//#  include "kernel/integrator/integrator_shade_background.h"
//#  include "kernel/integrator/integrator_shade_light.h"
//#  include "kernel/integrator/integrator_shade_shadow.h"
//#  include "kernel/integrator/integrator_shade_surface.h"
//#  include "kernel/integrator/integrator_shade_volume.h"
//
#  include "kernel/kernel_adaptive_sampling.h"

//#  include "kernel/kernel_bake.h"
//#  include "kernel/kernel_film.h"
#  include "kernel/kernel_work_stealing.h"



#  if 0
/* kernels */
kernel
    kernel_metal_path_trace(KernelWorkTile *tile, uint work_size, uint index_tpig [[thread_position_in_grid]])
{
//  int work_index = ccl_global_id(0);
  int work_index = index_tpig.x;
  bool thread_is_active = work_index < work_size;
  uint x, y, sample;
  KernelGlobals kg;
  if(thread_is_active) {
    get_work_pixel(tile, work_index, &x, &y, &sample);

    kernel_path_trace(&kg, tile->buffer, sample, x, y, tile->offset, tile->stride);
  }

  if(kernel_data.film.cryptomatte_passes) {
    __syncthreads();
    if(thread_is_active) {
      kernel_cryptomatte_post(&kg, tile->buffer, sample, x, y, tile->offset, tile->stride);
    }
  }
}
#  endif

/* --------------------------------------------------------------------
 * Integrator.
 */

kernel void kernel_metal_integrator_init_from_camera(threadgroup int *path_index_array,
                                             threadgroup KernelWorkTile *tile,
                                             threadgroup float *render_buffer,
                                             constant int &tile_work_size,
                                             constant int &path_index_offset,
                                            uint2 index_tpig [[thread_position_in_grid]])
{
//  const int global_index = ccl_global_id(0);
  const int global_index = index_tpig.x;
  const int work_index = global_index;
  bool thread_is_active = work_index < tile_work_size;
  if (thread_is_active) {
    const int path_index = (path_index_array) ? path_index_array[global_index] :
                                                path_index_offset + global_index;

    uint x, y, sample;
    get_work_pixel(tile, work_index, &x, &y, &sample);
    integrator_init_from_camera(NULL, path_index, tile, render_buffer, x, y, sample);
  }
}



#endif
