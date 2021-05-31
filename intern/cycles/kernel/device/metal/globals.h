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

/* Constant Globals */

#pragma once

#include "kernel/kernel_profiling.h"
#include "kernel/kernel_types.h"

#include "kernel/integrator/integrator_state.h"

CCL_NAMESPACE_BEGIN

#define KERNEL_TEX(type, name) typedef type name##_t;
#include "kernel/kernel_textures.h"

struct KernelGlobals {
  int unused[1]; //TODO: maybe unnecessary
#define KERNEL_TEX(type, name) type name;
#include "kernel/kernel_textures.h"
  device KernelData *data;
  IntegratorStateGPU __integrator_state;
};

/* Abstraction macros */
#define kernel_data (*kg->data)
#define kernel_tex_array(tex) \
  ((const ccl_global tex##_t *)(kg->buffers[kg->tex.cl_buffer] + kg->tex.data))
#define kernel_tex_fetch(tex, index) kernel_tex_array(tex)[(index)]
#define kernel_integrator_state (kg->__integrator_state)

CCL_NAMESPACE_END
