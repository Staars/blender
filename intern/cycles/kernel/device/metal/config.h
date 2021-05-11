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

/* Device data taken from METAL occupancy calculator.
 *
 * Terminology
 * - METAL GPUs have multiple streaming multiprocessors
 * - Each multiprocessor executes multiple thread blocks
 * - Each thread block contains a number of threads, also known as the block size
 * - Multiprocessors have a fixed number of registers, and the amount of registers
 *   used by each threads limits the number of threads per block.
 */

/* 3.0 and 3.5 */
#if __METAL_ARCH__ == 300 || __METAL_ARCH__ == 350
#  define METAL_MULTIPRESSOR_MAX_REGISTERS 65536
#  define METAL_MULTIPROCESSOR_MAX_BLOCKS 16
#  define METAL_BLOCK_MAX_THREADS 1024
#  define METAL_THREAD_MAX_REGISTERS 63

/* tunable parameters */
#  define METAL_KERNEL_BLOCK_NUM_THREADS 256
#  define METAL_KERNEL_MAX_REGISTERS 63

/* 3.2 */
#elif __METAL_ARCH__ == 320
#  define METAL_MULTIPRESSOR_MAX_REGISTERS 32768
#  define METAL_MULTIPROCESSOR_MAX_BLOCKS 16
#  define METAL_BLOCK_MAX_THREADS 1024
#  define METAL_THREAD_MAX_REGISTERS 63

/* tunable parameters */
#  define METAL_KERNEL_BLOCK_NUM_THREADS 256
#  define METAL_KERNEL_MAX_REGISTERS 63

/* 3.7 */
#elif __METAL_ARCH__ == 370
#  define METAL_MULTIPRESSOR_MAX_REGISTERS 65536
#  define METAL_MULTIPROCESSOR_MAX_BLOCKS 16
#  define METAL_BLOCK_MAX_THREADS 1024
#  define METAL_THREAD_MAX_REGISTERS 255

/* tunable parameters */
#  define METAL_KERNEL_BLOCK_NUM_THREADS 256
#  define METAL_KERNEL_MAX_REGISTERS 63

/* 5.x, 6.x */
#elif __METAL_ARCH__ <= 699
#  define METAL_MULTIPRESSOR_MAX_REGISTERS 65536
#  define METAL_MULTIPROCESSOR_MAX_BLOCKS 32
#  define METAL_BLOCK_MAX_THREADS 1024
#  define METAL_THREAD_MAX_REGISTERS 255

/* tunable parameters */
#  define METAL_KERNEL_BLOCK_NUM_THREADS 256
/* METAL 9.0 seems to cause slowdowns on high-end Pascal cards unless we increase the number of
 * registers */
#  if __METALCC_VER_MAJOR__ >= 9 && __METAL_ARCH__ >= 600
#    define METAL_KERNEL_MAX_REGISTERS 64
#  else
#    define METAL_KERNEL_MAX_REGISTERS 48
#  endif

/* 7.x, 8.x */
#elif __METAL_ARCH__ <= 899
#  define METAL_MULTIPRESSOR_MAX_REGISTERS 65536
#  define METAL_MULTIPROCESSOR_MAX_BLOCKS 32
#  define METAL_BLOCK_MAX_THREADS 1024
#  define METAL_THREAD_MAX_REGISTERS 255

/* tunable parameters */
#  define METAL_KERNEL_BLOCK_NUM_THREADS 512
#  define METAL_KERNEL_MAX_REGISTERS 96

/* unknown architecture */
#else
#  error "Unknown or unsupported METAL architecture, can't determine launch bounds"
#endif

/* For split kernel using all registers seems fastest for now, but this
 * is unlikely to be optimal once we resolve other bottlenecks. */

#define METAL_KERNEL_SPLIT_MAX_REGISTERS METAL_THREAD_MAX_REGISTERS

/* Compute number of threads per block and minimum blocks per multiprocessor
 * given the maximum number of registers per thread. */

#define METAL_LAUNCH_BOUNDS(block_num_threads, thread_num_registers) \
  __launch_bounds__(block_num_threads, \
                    METAL_MULTIPRESSOR_MAX_REGISTERS / (block_num_threads * thread_num_registers))

/* sanity checks */

#if METAL_KERNEL_BLOCK_NUM_THREADS > METAL_BLOCK_MAX_THREADS
#  error "Maximum number of threads per block exceeded"
#endif

#if METAL_MULTIPRESSOR_MAX_REGISTERS / \
        (METAL_KERNEL_BLOCK_NUM_THREADS * METAL_KERNEL_MAX_REGISTERS) > \
    METAL_MULTIPROCESSOR_MAX_BLOCKS
#  error "Maximum number of blocks per multiprocessor exceeded"
#endif

#if METAL_KERNEL_MAX_REGISTERS > METAL_THREAD_MAX_REGISTERS
#  error "Maximum number of registers per thread exceeded"
#endif
