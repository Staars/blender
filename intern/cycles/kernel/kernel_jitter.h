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

CCL_NAMESPACE_BEGIN

/* "Correlated Multi-Jittered Sampling"
 * Andrew Kensler, Pixar Technical Memo 13-01, 2013 */

ccl_device_inline bool cmj_is_pow2(int i)
{
  return (i > 1) && ((i & (i - 1)) == 0);
}

ccl_device_inline int cmj_fast_mod_pow2(int a, int b)
{
  return (a & (b - 1));
}

/* b must be > 1 */
ccl_device_inline int cmj_fast_div_pow2(int a, int b)
{
  kernel_assert(b > 1);
  return a >> count_trailing_zeros(b);
}

ccl_device_inline uint cmj_w_mask(uint w)
{
  kernel_assert(w > 1);
  return ((1 << (32 - count_leading_zeros(w))) - 1);
}

ccl_device_inline uint cmj_permute(uint i, uint l, uint p)
{
  uint w = l - 1;

  if ((l & w) == 0) {
    /* l is a power of two (fast) */
    i ^= p;
    i *= 0xe170893d;
    i ^= p >> 16;
    i ^= (i & w) >> 4;
    i ^= p >> 8;
    i *= 0x0929eb3f;
    i ^= p >> 23;
    i ^= (i & w) >> 1;
    i *= 1 | p >> 27;
    i *= 0x6935fa69;
    i ^= (i & w) >> 11;
    i *= 0x74dcb303;
    i ^= (i & w) >> 2;
    i *= 0x9e501cc3;
    i ^= (i & w) >> 2;
    i *= 0xc860a3df;
    i &= w;
    i ^= i >> 5;

    return (i + p) & w;
  }
  else {
    /* l is not a power of two (slow) */
    w = cmj_w_mask(w);

    do {
      i ^= p;
      i *= 0xe170893d;
      i ^= p >> 16;
      i ^= (i & w) >> 4;
      i ^= p >> 8;
      i *= 0x0929eb3f;
      i ^= p >> 23;
      i ^= (i & w) >> 1;
      i *= 1 | p >> 27;
      i *= 0x6935fa69;
      i ^= (i & w) >> 11;
      i *= 0x74dcb303;
      i ^= (i & w) >> 2;
      i *= 0x9e501cc3;
      i ^= (i & w) >> 2;
      i *= 0xc860a3df;
      i &= w;
      i ^= i >> 5;
    } while (i >= l);

    return (i + p) % l;
  }
}

ccl_device_inline uint cmj_hash(uint i, uint p)
{
  i ^= p;
  i ^= i >> 17;
  i ^= i >> 10;
  i *= 0xb36534e5;
  i ^= i >> 12;
  i ^= i >> 21;
  i *= 0x93fc4795;
  i ^= 0xdf6e307f;
  i ^= i >> 17;
  i *= 1 | p >> 18;

  return i;
}

ccl_device_inline uint cmj_hash_simple(uint i, uint p)
{
  i = (i ^ 61) ^ p;
  i += i << 3;
  i ^= i >> 4;
  i *= 0x27d4eb2d;
  return i;
}

ccl_device_inline float cmj_randfloat(uint i, uint p)
{
  return cmj_hash(i, p) * (1.0f / 4294967808.0f);
}

#  if !defined(__KERNEL_METAL__)
ccl_device float pmj_sample_1D(const KernelGlobals *kg, int sample, int rng_hash, int dimension)
#  else
ccl_device float pmj_sample_1D(device const KernelGlobals *kg, int sample, int rng_hash, int dimension)
#  endif
{
  /* Fallback to random */
  if (sample >= NUM_PMJ_SAMPLES) {
    const int p = rng_hash + dimension;
    return cmj_randfloat(sample, p);
  }
  else {
    const uint mask = cmj_hash_simple(dimension, rng_hash) & 0x007fffff;
    const int index = ((dimension % NUM_PMJ_PATTERNS) * NUM_PMJ_SAMPLES + sample) * 2;
    return __uint_as_float(kernel_tex_fetch(__sample_pattern_lut, index) ^ mask) - 1.0f;
  }
}

#  if !defined(__KERNEL_METAL__)
ccl_device float2 pmj_sample_2D(const KernelGlobals *kg, int sample, int rng_hash, int dimension)
#  else
ccl_device float2 pmj_sample_2D(device const KernelGlobals *kg, int sample, int rng_hash, int dimension)
#  endif
{
  if (sample >= NUM_PMJ_SAMPLES) {
    const int p = rng_hash + dimension;
    const float fx = cmj_randfloat(sample, p);
    const float fy = cmj_randfloat(sample, p + 1);
    return make_float2(fx, fy);
  }
  else {
    const int index = ((dimension % NUM_PMJ_PATTERNS) * NUM_PMJ_SAMPLES + sample) * 2;
    const uint maskx = cmj_hash_simple(dimension, rng_hash) & 0x007fffff;
    const uint masky = cmj_hash_simple(dimension + 1, rng_hash) & 0x007fffff;
    const float fx = __uint_as_float(kernel_tex_fetch(__sample_pattern_lut, index) ^ maskx) - 1.0f;
    const float fy = __uint_as_float(kernel_tex_fetch(__sample_pattern_lut, index + 1) ^ masky) -
                     1.0f;
    return make_float2(fx, fy);
  }
}

CCL_NAMESPACE_END
