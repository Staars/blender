/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup gpu
 */

#include "BKE_global.h"

#include "DNA_userdef_types.h"

#include "GPU_extensions.h"
#include "GPU_framebuffer.h"
#include "GPU_platform.h"

#include "gl_backend.hh"
#include "gl_debug.hh"
#include "gl_state.hh"

#include "gl_texture.hh"

namespace blender::gpu {

/* -------------------------------------------------------------------- */
/** \name Creation & Deletion
 * \{ */

GLTexture::GLTexture(const char *name) : Texture(name)
{
  BLI_assert(GPU_context_active_get() != NULL);

  glGenTextures(1, &tex_id_);
}

GLTexture::~GLTexture()
{
  if (framebuffer_) {
    GPU_framebuffer_free(framebuffer_);
  }
  GPUContext *ctx = GPU_context_active_get();
  if (ctx != NULL && is_bound_) {
    /* This avoid errors when the texture is still inside the bound texture array. */
    ctx->state_manager->texture_unbind(this);
  }
  GLBackend::get()->tex_free(tex_id_);
}

/* Return true on success. */
bool GLTexture::init_internal(void)
{
  if ((format_ == GPU_DEPTH24_STENCIL8) && GPU_depth_blitting_workaround()) {
    /* MacOS + Radeon Pro fails to blit depth on GPU_DEPTH24_STENCIL8
     * but works on GPU_DEPTH32F_STENCIL8. */
    format_ = GPU_DEPTH32F_STENCIL8;
  }

  if ((type_ == GPU_TEXTURE_CUBE_ARRAY) && !GPU_arb_texture_cube_map_array_is_supported()) {
    debug::raise_gl_error("Attempt to create a cubemap array without hardware support!");
    return false;
  }

  target_ = to_gl_target(type_);

  /* We need to bind once to define the texture type.  */
  GLContext::state_manager_active_get()->texture_bind_temp(this);

  if (!this->proxy_check(0)) {
    return false;
  }

  this->ensure_mipmaps(0);

  /* Avoid issue with incomplete textures. */
  if (GLEW_ARB_direct_state_access) {
    glTextureParameteri(tex_id_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }
  else {
    glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }

#ifndef __APPLE__
  if ((G.debug & G_DEBUG_GPU) && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
    char sh_name[64];
    SNPRINTF(sh_name, "Texture-%s", name_);
    /* Binding before setting the label is needed on some drivers. */
    glObjectLabel(GL_TEXTURE, tex_id_, -1, sh_name);
  }
#endif

  GL_CHECK_ERROR("Post-texture creation");
  return true;
}

/* Return true on success. */
bool GLTexture::init_internal(GPUVertBuf *vbo)
{
  target_ = to_gl_target(type_);

  /* We need to bind once to define the texture type.  */
  GLContext::state_manager_active_get()->texture_bind_temp(this);

  GLenum internal_format = to_gl_internal_format(format_);

  if (GLEW_ARB_direct_state_access) {
    glTextureBuffer(tex_id_, internal_format, vbo->vbo_id);
  }
  else {
    glTexBuffer(target_, internal_format, vbo->vbo_id);
  }

#ifndef __APPLE__
  if ((G.debug & G_DEBUG_GPU) && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
    char sh_name[64];
    SNPRINTF(sh_name, "Texture-%s", name_);
    /* Binding before setting the label is needed on some drivers. */
    glObjectLabel(GL_TEXTURE, tex_id_, -1, sh_name);
  }
#endif

  GL_CHECK_ERROR("Post-texture buffer creation");
  return true;
}

/* Will create enough mipmaps up to get to the given level. */
void GLTexture::ensure_mipmaps(int miplvl)
{
  int effective_h = (type_ == GPU_TEXTURE_1D_ARRAY) ? 0 : h_;
  int effective_d = (type_ != GPU_TEXTURE_3D) ? 0 : d_;
  int max_dimension = max_iii(w_, effective_h, effective_d);
  int max_miplvl = floor(log2(max_dimension));
  miplvl = min_ii(miplvl, max_miplvl);

  while (mipmaps_ < miplvl) {
    int mip = ++mipmaps_;
    const int dimensions = this->dimensions_count();

    int w = mip_width_get(mip);
    int h = mip_height_get(mip);
    int d = mip_depth_get(mip);
    GLenum internal_format = to_gl_internal_format(format_);
    GLenum gl_format = to_gl_data_format(format_);
    GLenum gl_type = to_gl(to_data_format(format_));

    GLContext::state_manager_active_get()->texture_bind_temp(this);

    if (type_ == GPU_TEXTURE_CUBE) {
      for (int i = 0; i < d; i++) {
        GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
        glTexImage2D(target, mip, internal_format, w, h, 0, gl_format, gl_type, NULL);
      }
    }
    else if (format_flag_ & GPU_FORMAT_COMPRESSED) {
      size_t size = ((w + 3) / 4) * ((h + 3) / 4) * to_block_size(format_);
      switch (dimensions) {
        default:
        case 1:
          glCompressedTexImage1D(target_, mip, internal_format, w, 0, size, NULL);
          break;
        case 2:
          glCompressedTexImage2D(target_, mip, internal_format, w, h, 0, size, NULL);
          break;
        case 3:
          glCompressedTexImage3D(target_, mip, internal_format, w, h, d, 0, size, NULL);
          break;
      }
    }
    else {
      switch (dimensions) {
        default:
        case 1:
          glTexImage1D(target_, mip, internal_format, w, 0, gl_format, gl_type, NULL);
          break;
        case 2:
          glTexImage2D(target_, mip, internal_format, w, h, 0, gl_format, gl_type, NULL);
          break;
        case 3:
          glTexImage3D(target_, mip, internal_format, w, h, d, 0, gl_format, gl_type, NULL);
          break;
      }
    }

    GL_CHECK_ERROR("Post-mipmap creation");
  }

  this->mip_range_set(0, mipmaps_);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Operations
 * \{ */

void GLTexture::update_sub_direct_state_access(
    int mip, int offset[3], int extent[3], GLenum format, GLenum type, const void *data)
{
  if (format_flag_ & GPU_FORMAT_COMPRESSED) {
    size_t size = ((extent[0] + 3) / 4) * ((extent[1] + 3) / 4) * to_block_size(format_);
    switch (this->dimensions_count()) {
      default:
      case 1:
        glCompressedTextureSubImage1D(tex_id_, mip, offset[0], extent[0], format, size, data);
        break;
      case 2:
        glCompressedTextureSubImage2D(
            tex_id_, mip, UNPACK2(offset), UNPACK2(extent), format, size, data);
        break;
      case 3:
        glCompressedTextureSubImage3D(
            tex_id_, mip, UNPACK3(offset), UNPACK3(extent), format, size, data);
        break;
    }
  }
  else {
    switch (this->dimensions_count()) {
      default:
      case 1:
        glTextureSubImage1D(tex_id_, mip, offset[0], extent[0], format, type, data);
        break;
      case 2:
        glTextureSubImage2D(tex_id_, mip, UNPACK2(offset), UNPACK2(extent), format, type, data);
        break;
      case 3:
        glTextureSubImage3D(tex_id_, mip, UNPACK3(offset), UNPACK3(extent), format, type, data);
        break;
    }
  }
  GL_CHECK_ERROR("Post-update_sub_direct_state_access");
}

void GLTexture::update_sub(
    int mip, int offset[3], int extent[3], eGPUDataFormat type, const void *data)
{
  BLI_assert(validate_data_format(format_, type));
  BLI_assert(data != NULL);

  this->ensure_mipmaps(mip);

  if (mip > mipmaps_) {
    debug::raise_gl_error("Updating a miplvl on a texture too small to have this many levels.");
    return;
  }

  const int dimensions = this->dimensions_count();
  GLenum gl_format = to_gl_data_format(format_);
  GLenum gl_type = to_gl(type);

  if (GLEW_ARB_direct_state_access) {
    this->update_sub_direct_state_access(mip, offset, extent, gl_format, gl_type, data);
    return;
  }

  GLContext::state_manager_active_get()->texture_bind_temp(this);
  if (type_ == GPU_TEXTURE_CUBE) {
    for (int i = 0; i < extent[2]; i++) {
      GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + offset[2] + i;
      glTexSubImage2D(target, mip, UNPACK2(offset), UNPACK2(extent), gl_format, gl_type, data);
    }
  }
  else if (format_flag_ & GPU_FORMAT_COMPRESSED) {
    size_t size = ((extent[0] + 3) / 4) * ((extent[1] + 3) / 4) * to_block_size(format_);
    switch (dimensions) {
      default:
      case 1:
        glCompressedTexSubImage1D(target_, mip, offset[0], extent[0], gl_format, size, data);
        break;
      case 2:
        glCompressedTexSubImage2D(
            target_, mip, UNPACK2(offset), UNPACK2(extent), gl_format, size, data);
        break;
      case 3:
        glCompressedTexSubImage3D(
            target_, mip, UNPACK3(offset), UNPACK3(extent), gl_format, size, data);
        break;
    }
  }
  else {
    switch (dimensions) {
      default:
      case 1:
        glTexSubImage1D(target_, mip, offset[0], extent[0], gl_format, gl_type, data);
        break;
      case 2:
        glTexSubImage2D(target_, mip, UNPACK2(offset), UNPACK2(extent), gl_format, gl_type, data);
        break;
      case 3:
        glTexSubImage3D(target_, mip, UNPACK3(offset), UNPACK3(extent), gl_format, gl_type, data);
        break;
    }
  }

  GL_CHECK_ERROR("Post-update_sub");
}

/** This will create the mipmap images and populate them with filtered data from base level.
 * WARNING: Depth textures are not populated but they have their mips correctly defined.
 * WARNING: This resets the mipmap range.
 */
void GLTexture::generate_mipmap(void)
{
  this->ensure_mipmaps(9999);
  /* Some drivers have bugs when using glGenerateMipmap with depth textures (see T56789).
   * In this case we just create a complete texture with mipmaps manually without
   * down-sampling. You must initialize the texture levels using other methods like
   * GPU_framebuffer_recursive_downsample(). */
  if (format_flag_ & GPU_FORMAT_DEPTH) {
    return;
  }

  /* Downsample from mip 0 using implementation. */
  if (GLEW_ARB_direct_state_access) {
    glGenerateTextureMipmap(tex_id_);
  }
  else {
    GLContext::state_manager_active_get()->texture_bind_temp(this);
    glGenerateMipmap(target_);
  }
}

void GLTexture::clear(eGPUDataFormat data_format, const void *data)
{
  BLI_assert(validate_data_format(format_, data_format));

  if (GLEW_ARB_clear_texture && !(G.debug & G_DEBUG_GPU_FORCE_WORKAROUNDS)) {
    int mip = 0;
    GLenum gl_format = to_gl_data_format(format_);
    GLenum gl_type = to_gl(data_format);
    glClearTexImage(tex_id_, mip, gl_format, gl_type, data);
  }
  else {
    /* Fallback for older GL. */
    GPUFrameBuffer *prev_fb = GPU_framebuffer_active_get();

    FrameBuffer *fb = reinterpret_cast<FrameBuffer *>(this->framebuffer_get());
    fb->bind(true);
    fb->clear_attachment(this->attachment_type(0), data_format, data);

    GPU_framebuffer_bind(prev_fb);
  }
}

void GLTexture::copy_to(Texture *dst_)
{
  GLTexture *dst = static_cast<GLTexture *>(dst_);
  GLTexture *src = this;

  BLI_assert((dst->w_ == src->w_) && (dst->h_ == src->h_) && (dst->d_ == src->d_));
  BLI_assert(dst->format_ == src->format_);
  BLI_assert(dst->type_ == src->type_);
  /* TODO support array / 3D textures. */
  BLI_assert(dst->d_ == 0);

  if (GLEW_ARB_copy_image && !GPU_texture_copy_workaround()) {
    /* Opengl 4.3 */
    int mip = 0;
    /* NOTE: mip_size_get() won't override any dimension that is equal to 0. */
    int extent[3] = {1, 1, 1};
    this->mip_size_get(mip, extent);
    glCopyImageSubData(
        src->tex_id_, target_, mip, 0, 0, 0, dst->tex_id_, target_, mip, 0, 0, 0, UNPACK3(extent));
  }
  else {
    /* Fallback for older GL. */
    GPU_framebuffer_blit(
        src->framebuffer_get(), 0, dst->framebuffer_get(), 0, to_framebuffer_bits(format_));
  }
}

void *GLTexture::read(int mip, eGPUDataFormat type)
{
  BLI_assert(!(format_flag_ & GPU_FORMAT_COMPRESSED));
  BLI_assert(mip <= mipmaps_);
  BLI_assert(validate_data_format(format_, type));

  /* NOTE: mip_size_get() won't override any dimension that is equal to 0. */
  int extent[3] = {1, 1, 1};
  this->mip_size_get(mip, extent);

  size_t sample_len = extent[0] * extent[1] * extent[2];
  size_t sample_size = to_bytesize(format_, type);
  size_t texture_size = sample_len * sample_size;

  /* AMD Pro driver have a bug that write 8 bytes past buffer size
   * if the texture is big. (see T66573) */
  void *data = MEM_mallocN(texture_size + 8, "GPU_texture_read");

  GLenum gl_format = to_gl_data_format(format_);
  GLenum gl_type = to_gl(type);

  if (GLEW_ARB_direct_state_access) {
    glGetTextureImage(tex_id_, mip, gl_format, gl_type, texture_size, data);
  }
  else {
    GLContext::state_manager_active_get()->texture_bind_temp(this);
    if (type_ == GPU_TEXTURE_CUBE) {
      size_t cube_face_size = texture_size / 6;
      char *face_data = (char *)data;
      for (int i = 0; i < 6; i++, face_data += cube_face_size) {
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, gl_format, gl_type, face_data);
      }
    }
    else {
      glGetTexImage(target_, mip, gl_format, gl_type, data);
    }
  }
  return data;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Getters & setters
 * \{ */

void GLTexture::swizzle_set(const char swizzle[4])
{
  GLint gl_swizzle[4] = {(GLint)swizzle_to_gl(swizzle[0]),
                         (GLint)swizzle_to_gl(swizzle[1]),
                         (GLint)swizzle_to_gl(swizzle[2]),
                         (GLint)swizzle_to_gl(swizzle[3])};
  if (GLEW_ARB_direct_state_access) {
    glTextureParameteriv(tex_id_, GL_TEXTURE_SWIZZLE_RGBA, gl_swizzle);
  }
  else {
    GLContext::state_manager_active_get()->texture_bind_temp(this);
    glTexParameteriv(target_, GL_TEXTURE_SWIZZLE_RGBA, gl_swizzle);
  }
}

void GLTexture::mip_range_set(int min, int max)
{
  BLI_assert(min <= max && min >= 0 && max <= mipmaps_);
  if (GLEW_ARB_direct_state_access) {
    glTextureParameteri(tex_id_, GL_TEXTURE_BASE_LEVEL, min);
    glTextureParameteri(tex_id_, GL_TEXTURE_MAX_LEVEL, max);
  }
  else {
    GLContext::state_manager_active_get()->texture_bind_temp(this);
    glTexParameteri(target_, GL_TEXTURE_BASE_LEVEL, min);
    glTexParameteri(target_, GL_TEXTURE_MAX_LEVEL, max);
  }
}

struct GPUFrameBuffer *GLTexture::framebuffer_get(void)
{
  if (framebuffer_) {
    return framebuffer_;
  }
  BLI_assert(!(type_ & (GPU_TEXTURE_ARRAY | GPU_TEXTURE_CUBE | GPU_TEXTURE_1D | GPU_TEXTURE_3D)));
  /* TODO(fclem) cleanup this. Don't use GPU object but blender::gpu ones. */
  GPUTexture *gputex = reinterpret_cast<GPUTexture *>(static_cast<Texture *>(this));
  framebuffer_ = GPU_framebuffer_create(name_);
  GPU_framebuffer_texture_attach(framebuffer_, gputex, 0, 0);
  return framebuffer_;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Sampler objects
 * \{ */

GLuint GLTexture::samplers_[GPU_SAMPLER_MAX] = {0};

void GLTexture::samplers_init(void)
{
  glGenSamplers(GPU_SAMPLER_MAX, samplers_);
  for (int i = 0; i <= GPU_SAMPLER_ICON - 1; i++) {
    eGPUSamplerState state = static_cast<eGPUSamplerState>(i);
    GLenum clamp_type = (state & GPU_SAMPLER_CLAMP_BORDER) ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;
    GLenum wrap_s = (state & GPU_SAMPLER_REPEAT_S) ? GL_REPEAT : clamp_type;
    GLenum wrap_t = (state & GPU_SAMPLER_REPEAT_T) ? GL_REPEAT : clamp_type;
    GLenum wrap_r = (state & GPU_SAMPLER_REPEAT_R) ? GL_REPEAT : clamp_type;
    GLenum mag_filter = (state & GPU_SAMPLER_FILTER) ? GL_LINEAR : GL_NEAREST;
    GLenum min_filter = (state & GPU_SAMPLER_FILTER) ?
                            ((state & GPU_SAMPLER_MIPMAP) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) :
                            ((state & GPU_SAMPLER_MIPMAP) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
    GLenum compare_mode = (state & GPU_SAMPLER_COMPARE) ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE;

    glSamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_S, wrap_s);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_T, wrap_t);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_R, wrap_r);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_MIN_FILTER, min_filter);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_MAG_FILTER, mag_filter);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_COMPARE_MODE, compare_mode);
    glSamplerParameteri(samplers_[i], GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    /** Other states are left to default:
     * - GL_TEXTURE_BORDER_COLOR is {0, 0, 0, 0}.
     * - GL_TEXTURE_MIN_LOD is -1000.
     * - GL_TEXTURE_MAX_LOD is 1000.
     * - GL_TEXTURE_LOD_BIAS is 0.0f.
     **/
  }
  samplers_update();

  /* Custom sampler for icons. */
  GLuint icon_sampler = samplers_[GPU_SAMPLER_ICON];
  glSamplerParameteri(icon_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  glSamplerParameteri(icon_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameterf(icon_sampler, GL_TEXTURE_LOD_BIAS, -0.5f);
}

void GLTexture::samplers_update(void)
{
  if (!GLEW_EXT_texture_filter_anisotropic) {
    return;
  }

  float max_anisotropy = 1.0f;
  glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);

  float aniso_filter = max_ff(max_anisotropy, U.anisotropic_filter);

  for (int i = 0; i <= GPU_SAMPLER_ICON - 1; i++) {
    eGPUSamplerState state = static_cast<eGPUSamplerState>(i);
    if (state & GPU_SAMPLER_MIPMAP) {
      glSamplerParameterf(samplers_[i], GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso_filter);
    }
  }
}

void GLTexture::samplers_free(void)
{
  glDeleteSamplers(GPU_SAMPLER_MAX, samplers_);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Proxy texture
 *
 * Dummy texture to see if the implementation supports the requested size.
 * \{ */

/* NOTE: This only checks if this mipmap is valid / supported.
 * TODO(fclem) make the check cover the whole mipmap chain. */
bool GLTexture::proxy_check(int mip)
{
  /* Manual validation first, since some implementation have issues with proxy creation. */
  int max_size = GPU_max_texture_size();
  int max_3d_size = GPU_max_texture_3d_size();
  int max_cube_size = GPU_max_cube_map_size();
  int size[3] = {1, 1, 1};
  this->mip_size_get(mip, size);

  if (type_ & GPU_TEXTURE_ARRAY) {
    if (this->layer_count() > GPU_max_texture_layers()) {
      return false;
    }
  }

  if (type_ == GPU_TEXTURE_3D) {
    if (size[0] > max_3d_size || size[1] > max_3d_size || size[2] > max_3d_size) {
      return false;
    }
  }
  else if ((type_ & ~GPU_TEXTURE_ARRAY) == GPU_TEXTURE_2D) {
    if (size[0] > max_size || size[1] > max_size) {
      return false;
    }
  }
  else if ((type_ & ~GPU_TEXTURE_ARRAY) == GPU_TEXTURE_1D) {
    if (size[0] > max_size) {
      return false;
    }
  }
  else if ((type_ & ~GPU_TEXTURE_ARRAY) == GPU_TEXTURE_CUBE) {
    if (size[0] > max_cube_size) {
      return false;
    }
  }

  if (GPU_type_matches(GPU_DEVICE_ATI, GPU_OS_WIN, GPU_DRIVER_ANY) ||
      GPU_type_matches(GPU_DEVICE_NVIDIA, GPU_OS_MAC, GPU_DRIVER_OFFICIAL) ||
      GPU_type_matches(GPU_DEVICE_ATI, GPU_OS_UNIX, GPU_DRIVER_OFFICIAL)) {
    /* Some AMD drivers have a faulty `GL_PROXY_TEXTURE_..` check.
     * (see T55888, T56185, T59351).
     * Checking with `GL_PROXY_TEXTURE_..` doesn't prevent `Out Of Memory` issue,
     * it just states that the OGL implementation can support the texture.
     * So we already manually check the maximum size and maximum number of layers.
     * Same thing happens on Nvidia/macOS 10.15 (T78175). */
    return true;
  }

  if ((type_ == GPU_TEXTURE_CUBE_ARRAY) &&
      GPU_type_matches(GPU_DEVICE_ANY, GPU_OS_MAC, GPU_DRIVER_ANY)) {
    /* Special fix for T79703. */
    return true;
  }

  GLenum gl_proxy = to_gl_proxy(type_);
  GLenum internal_format = to_gl_internal_format(format_);
  GLenum gl_format = to_gl_data_format(format_);
  GLenum gl_type = to_gl(to_data_format(format_));
  /* Small exception. */
  int dimensions = (type_ == GPU_TEXTURE_CUBE) ? 2 : this->dimensions_count();

  if (format_flag_ & GPU_FORMAT_COMPRESSED) {
    size_t img_size = ((size[0] + 3) / 4) * ((size[1] + 3) / 4) * to_block_size(format_);
    switch (dimensions) {
      default:
      case 1:
        glCompressedTexImage1D(gl_proxy, mip, size[0], 0, gl_format, img_size, NULL);
        break;
      case 2:
        glCompressedTexImage2D(gl_proxy, mip, UNPACK2(size), 0, gl_format, img_size, NULL);
        break;
      case 3:
        glCompressedTexImage3D(gl_proxy, mip, UNPACK3(size), 0, gl_format, img_size, NULL);
        break;
    }
  }
  else {
    switch (dimensions) {
      default:
      case 1:
        glTexImage1D(gl_proxy, mip, internal_format, size[0], 0, gl_format, gl_type, NULL);
        break;
      case 2:
        glTexImage2D(gl_proxy, mip, internal_format, UNPACK2(size), 0, gl_format, gl_type, NULL);
        break;
      case 3:
        glTexImage3D(gl_proxy, mip, internal_format, UNPACK3(size), 0, gl_format, gl_type, NULL);
        break;
    }
  }

  int width = 0;
  glGetTexLevelParameteriv(gl_proxy, 0, GL_TEXTURE_WIDTH, &width);
  return (width > 0);
}

/** \} */

/* TODO(fclem) Legacy. Should be removed at some point. */
uint GLTexture::gl_bindcode_get(void) const
{
  return tex_id_;
}

}  // namespace blender::gpu