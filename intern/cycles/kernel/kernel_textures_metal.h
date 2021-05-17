/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License"; ;
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

#ifndef KERNEL_TEX
#  define KERNEL_TEX(type, name)
#endif

/* bvh */
constant float4  __bvh_nodes;
constant float4  __bvh_leaf_nodes;
constant float4  __prim_tri_verts;
constant uint  __prim_tri_index;
constant uint  __prim_type;
constant uint  __prim_visibility;
constant uint  __prim_index;
constant uint  __prim_object;
constant uint  __object_node;
constant float2  __prim_time;

/* objects */
constant KernelObject  __objects;
constant Transform  __object_motion_pass;
constant DecomposedTransform  __object_motion;
constant uint  __object_flag;
constant float  __object_volume_step;

/* cameras */
constant DecomposedTransform  __camera_motion;

/* triangles */
constant uint  __tri_shader;
constant float4  __tri_vnormal;
constant uint4  __tri_vindex;
constant uint  __tri_patch;
constant float2  __tri_patch_uv;

/* curves */
constant float4  __curves;
constant float4  __curve_keys;

/* patches */
constant uint  __patches;

/* attributes */
constant uint4  __attributes_map;
constant float  __attributes_float;
constant float2  __attributes_float2;
constant float4  __attributes_float3;
constant uchar4  __attributes_uchar4;

/* lights */
constant KernelLightDistribution  __light_distribution;
constant KernelLight  __lights;
constant float2  __light_background_marginal_cdf;
constant float2  __light_background_conditional_cdf;

/* particles */
constant KernelParticle  __particles;

/* shaders */
constant uint4  __svm_nodes;
constant KernelShader  __shaders;

/* lookup tables */
constant float  __lookup_table;

/* sobol */
constant uint  __sample_pattern_lut;

/* image textures */
constant TextureInfo  __texture_info;

/* ies lights */
constant float  __ies;

#undef KERNEL_TEX
