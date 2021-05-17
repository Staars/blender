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

#pragma once

#define __KERNEL_GPU__
#define __KERNEL_METAL__

#define CCL_NAMESPACE_BEGIN
#define CCL_NAMESPACE_END

/* Selective nodes compilation. */
#ifndef __NODES_MAX_GROUP__
#  define __NODES_MAX_GROUP__ NODE_GROUP_LEVEL_MAX
#endif
#ifndef __NODES_FEATURES__
#  define __NODES_FEATURES__ NODE_FEATURE_ALL
#endif

/* Manual definitions so we can compile without METAL toolkit. */

//#ifdef __APPLE__
//typedef unsigned int uint32_t;
//typedef unsigned long long uint64_t;
//#else
//#  include <stdint.h>
//#endif
//typedef unsigned short half;
//typedef unsigned long long CUtexObject;

#ifdef CYCLES_CUBIN_CC
#  define FLT_MIN 1.175494350822287507969e-38f
#  define FLT_MAX 340282346638528859811704183484516925440.0f
#  define FLT_EPSILON 1.192092896e-07F
#endif

//__device__ half __float2half(const float f)
//{
//  half val;
//  asm("{  cvt.rn.f16.f32 %0, %1;}\n" : "=h"(val) : "f"(f));
//  return val;
//}

/* Qualifier wrappers for different names on different devices */

#define __device__
#define ccl_device __device__ __inline__
//#if __METAL_ARCH__ < 500
#  define ccl_device_inline
#  define ccl_device_forceinline
//#else
//#  define ccl_device_inline __device__ __inline__
//#  define ccl_device_forceinline __device__ __forceinline__
//#endif
#define ccl_device_noinline __device__ __noinline__
#define ccl_device_noinline_cpu ccl_device
#define ccl_global
#define ccl_static_constant __constant__
#define ccl_device_constant __constant__ __device__
#define ccl_constant const
#define ccl_local __shared__
#define ccl_local_param
#define ccl_private
#define ccl_may_alias
#define ccl_addr_space
#define ccl_restrict __restrict__
#define ccl_loop_no_unroll
/* TODO(sergey): In theory we might use references with METAL, however
 * performance impact yet to be investigated.
 */
#define ccl_ref
#define ccl_align(n) /*__align__(n)*/
#define ccl_optional_struct_init

#define ccl_attr_maybe_unused [[maybe_unused]]

#define ATTR_FALLTHROUGH

#define CCL_MAX_LOCAL_SIZE METAL_KERNEL_BLOCK_NUM_THREADS

/* No assert supported for METAL */
#define kernel_assert(cond)


/* make_type definitions with metal style element initializers */
#ifdef make_float2
#  undef make_float2
#endif
#ifdef make_float3
#  undef make_float3
#endif
#ifdef make_float4
#  undef make_float4
#endif
#ifdef make_int2
#  undef make_int2
#endif
#ifdef make_int3
#  undef make_int3
#endif
#ifdef make_int4
#  undef make_int4
#endif
#ifdef make_uchar4
#  undef make_uchar4
#endif

#ifdef clamp
#  undef clamp
#endif
//#ifdef max
//#  undef max
//#endif

#define make_float2(x, y) (float2(x, y))
#define make_float3(x, y, z) (float3(x, y, z))
#define make_float4(x, y, z, w) (float4(x, y, z, w))
#define make_int2(x, y) (int2(x, y))
#define make_int3(x, y, z) (int3(x, y, z))
#define make_int4(x, y, z, w) (int4(x, y, z, w))
#define make_uchar4(x, y, z, w) (uchar4(x, y, z, w))

///* math functions */
#define __uint_as_float(x) float(x)
#define __float_as_uint(x) uint(x)
#define __int_as_float(x) float(x)
#define __float_as_int(x) int(x)
#define powf(x, y) metal::pow(((float)(x)), ((float)(y)))
#define fabsf(x) metal::fabs(((float)(x)))
#define copysignf(x, y) metal::copysign(((float)(x)), ((float)(y)))
#define asinf(x) metal::asin(((float)(x)))
#define acosf(x) metal::acos(((float)(x)))
#define atanf(x) metal::atan(((float)(x)))
#define floorf(x) metal::floor(((float)(x)))
#define ceilf(x) metal::ceil(((float)(x)))
//#define hypotf(x, y) hypot(((float)(x)), ((float)(y)))
#define atan2f(x, y) metal::atan2(((float)(x)), ((float)(y)))
#define fmaxf(x, y) metal::fmax(((float)(x)), ((float)(y)))
#define fminf(x, y) metal::fmin(((float)(x)), ((float)(y)))
//#define fmodf(x, y) metal::modf((float)(x), (float)(y))
#define sinhf(x) metal::sinh(((float)(x)))
#define coshf(x) metal::cosh(((float)(x)))
#define tanhf(x) metal::tanh(((float)(x)))
//#define dot(x,y) metal::dot(((x), (y)))
//#define dot metal::dot
//#define sqrt metal::sqrt
#define sqrtf sqrt
//#define normalize metal::normalize
#define cosf metal::cos
#define sinf metal::sin
//#define cross metal::cross
//#define modf metal::modf
#define fmodf fmod
#define logf metal::log
#define lgamma metal::gamma
//#define abs metal::fast::abs
//#define log metal::log
#define log3 log
#define exp3 exp
#define float_to_int (float)

/* Types */

#include <metal_stdlib>
#include <metal_geometric>
#include <metal_integer>
using metal::fast::min;
using metal::fast::max;
using metal::fast::clamp;
using metal::fast::saturate;
using metal::fast::abs;
using metal::fast::log;
using metal::fast::fmod;
using metal::fast::sqrt;
using metal::fast::exp;
using metal::dot;
using metal::fast::normalize;
using metal::cross;
#include "util/util_types.h"

/* define NULL */
#ifndef NULL
#  define NULL nullptr
#endif
