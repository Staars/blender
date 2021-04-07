/*
 * Copyright 2021 Blender Foundation
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

/* Given an array of states, build an array of indices for which the states
 * are active and sorted by a given key. The prefix sum of the number of active
 * states per key must have already been computed.
 *
 * TODO: there may be ways to optimize this to avoid this many atomic ops? */

#include "util/util_atomic.h"

#define CUDA_PARALLEL_SORTED_INDEX_DEFAULT_BLOCK_SIZE 512
#define CUDA_PARALLEL_SORTED_INDEX_INACTIVE_KEY -1

template<uint blocksize, typename GetKeyOp>
__device__ void cuda_parallel_sorted_index_array(const uint num_states,
                                                 int *indices,
                                                 int *num_indices,
                                                 int *key_prefix_sum,
                                                 GetKeyOp get_key_op)
{
  const uint state_index = blockIdx.x * blocksize + threadIdx.x;
  const int key = (state_index < num_states) ? get_key_op(state_index) :
                                               CUDA_PARALLEL_SORTED_INDEX_INACTIVE_KEY;

  if (key != CUDA_PARALLEL_SORTED_INDEX_INACTIVE_KEY) {
    const uint index = atomic_fetch_and_add_uint32(&key_prefix_sum[key], 1);
    indices[index] = state_index;
  }
}

CCL_NAMESPACE_END
