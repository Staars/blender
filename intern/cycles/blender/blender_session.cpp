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

#include <stdlib.h>

#include "device/device.h"
#include "render/background.h"
#include "render/buffers.h"
#include "render/camera.h"
#include "render/colorspace.h"
#include "render/film.h"
#include "render/integrator.h"
#include "render/light.h"
#include "render/mesh.h"
#include "render/object.h"
#include "render/scene.h"
#include "render/session.h"
#include "render/shader.h"
#include "render/stats.h"

#include "util/util_algorithm.h"
#include "util/util_color.h"
#include "util/util_foreach.h"
#include "util/util_function.h"
#include "util/util_hash.h"
#include "util/util_logging.h"
#include "util/util_murmurhash.h"
#include "util/util_progress.h"
#include "util/util_time.h"

#include "blender/blender_gpu_display.h"
#include "blender/blender_session.h"
#include "blender/blender_sync.h"
#include "blender/blender_util.h"

CCL_NAMESPACE_BEGIN

DeviceTypeMask BlenderSession::device_override = DEVICE_MASK_ALL;
bool BlenderSession::headless = false;
int BlenderSession::num_resumable_chunks = 0;
int BlenderSession::current_resumable_chunk = 0;
int BlenderSession::start_resumable_chunk = 0;
int BlenderSession::end_resumable_chunk = 0;
bool BlenderSession::print_render_stats = false;

BlenderSession::BlenderSession(BL::RenderEngine &b_engine,
                               BL::Preferences &b_userpref,
                               BL::BlendData &b_data,
                               bool preview_osl)
    : session(NULL),
      scene(NULL),
      sync(NULL),
      b_engine(b_engine),
      b_userpref(b_userpref),
      b_data(b_data),
      b_render(b_engine.render()),
      b_depsgraph(PointerRNA_NULL),
      b_scene(PointerRNA_NULL),
      b_v3d(PointerRNA_NULL),
      b_rv3d(PointerRNA_NULL),
      width(0),
      height(0),
      preview_osl(preview_osl),
      python_thread_state(NULL)
{
  /* offline render */
  background = true;
  last_redraw_time = 0.0;
  start_resize_time = 0.0;
  last_status_time = 0.0;
}

BlenderSession::BlenderSession(BL::RenderEngine &b_engine,
                               BL::Preferences &b_userpref,
                               BL::BlendData &b_data,
                               BL::SpaceView3D &b_v3d,
                               BL::RegionView3D &b_rv3d,
                               int width,
                               int height)
    : session(NULL),
      scene(NULL),
      sync(NULL),
      b_engine(b_engine),
      b_userpref(b_userpref),
      b_data(b_data),
      b_render(b_engine.render()),
      b_depsgraph(PointerRNA_NULL),
      b_scene(PointerRNA_NULL),
      b_v3d(b_v3d),
      b_rv3d(b_rv3d),
      width(width),
      height(height),
      preview_osl(false),
      python_thread_state(NULL),
      use_developer_ui(b_userpref.experimental().use_cycles_debug() &&
                       b_userpref.view().show_developer_ui())
{
  /* 3d view render */
  background = false;
  last_redraw_time = 0.0;
  start_resize_time = 0.0;
  last_status_time = 0.0;
}

BlenderSession::~BlenderSession()
{
  free_session();
}

void BlenderSession::create_session()
{
  SessionParams session_params = BlenderSync::get_session_params(
      b_engine, b_userpref, b_scene, background);
  SceneParams scene_params = BlenderSync::get_scene_params(b_scene, background);
  bool session_pause = BlenderSync::get_session_pause(b_scene, background);

  /* reset status/progress */
  last_status = "";
  last_error = "";
  last_progress = -1.0f;
  start_resize_time = 0.0;

  /* create session */
  session = new Session(session_params, scene_params);
  session->progress.set_update_callback(function_bind(&BlenderSession::tag_redraw, this));
  session->progress.set_cancel_callback(function_bind(&BlenderSession::test_cancel, this));
  session->set_pause(session_pause);

  /* create scene */
  scene = session->scene;
  scene->name = b_scene.name();

  /* create sync */
  sync = new BlenderSync(
      b_engine, b_data, b_scene, scene, !background, use_developer_ui, session->progress);
  BL::Object b_camera_override(b_engine.camera_override());
  if (b_v3d) {
    sync->sync_view(b_v3d, b_rv3d, width, height);
  }
  else {
    sync->sync_camera(b_render, b_camera_override, width, height, "");
  }

  /* set buffer parameters */
  BufferParams buffer_params = BlenderSync::get_buffer_params(
      b_v3d, b_rv3d, scene->camera, width, height);
  session->reset(buffer_params, session_params.samples);

  /* Create GPU display. */
  if (!background) {
    session->set_gpu_display(make_unique<BlenderGPUDisplay>(b_engine, b_scene));
  }

  /* TODO(sergey): Decice on what is to be communicated to the engine here. There is no tiled
   * rendering for from visual point of view when render buffer fits big tile. But for huge
   * render resolutions it might still be helpful to see which big tile is being sampled. */
  /* b_engine.use_highlight_tiles(session_params.progressive_refine == false); */

  update_resumable_tile_manager(session_params.samples);
}

void BlenderSession::reset_session(BL::BlendData &b_data, BL::Depsgraph &b_depsgraph)
{
  /* Update data, scene and depsgraph pointers. These can change after undo. */
  this->b_data = b_data;
  this->b_depsgraph = b_depsgraph;
  this->b_scene = b_depsgraph.scene_eval();
  if (sync) {
    sync->reset(this->b_data, this->b_scene);
  }

  if (preview_osl) {
    PointerRNA cscene = RNA_pointer_get(&b_scene.ptr, "cycles");
    RNA_boolean_set(&cscene, "shading_system", preview_osl);
  }

  if (b_v3d) {
    this->b_render = b_scene.render();
  }
  else {
    this->b_render = b_engine.render();
    width = render_resolution_x(b_render);
    height = render_resolution_y(b_render);
  }

  bool is_new_session = (session == NULL);
  if (is_new_session) {
    /* Initialize session and remember it was just created so not to
     * re-create it below.
     */
    create_session();
  }

  if (b_v3d) {
    /* NOTE: We need to create session, but all the code from below
     * will make viewport render to stuck on initialization.
     */
    return;
  }

  SessionParams session_params = BlenderSync::get_session_params(
      b_engine, b_userpref, b_scene, background);
  SceneParams scene_params = BlenderSync::get_scene_params(b_scene, background);

  if (scene->params.modified(scene_params) || session->params.modified(session_params) ||
      !this->b_render.use_persistent_data()) {
    /* if scene or session parameters changed, it's easier to simply re-create
     * them rather than trying to distinguish which settings need to be updated
     */
    if (!is_new_session) {
      free_session();
      create_session();
    }
    return;
  }

  session->progress.reset();

  /* peak memory usage should show current render peak, not peak for all renders
   * made by this render session
   */
  session->stats.mem_peak = session->stats.mem_used;

  if (is_new_session) {
    /* Sync object should be re-created for new scene. */
    delete sync;
    sync = new BlenderSync(
        b_engine, b_data, b_scene, scene, !background, use_developer_ui, session->progress);
  }
  else {
    /* Sync recalculations to do just the required updates. */
    sync->sync_recalc(b_depsgraph, b_v3d);
  }

  BL::Object b_camera_override(b_engine.camera_override());
  sync->sync_camera(b_render, b_camera_override, width, height, "");

  BL::SpaceView3D b_null_space_view3d(PointerRNA_NULL);
  BL::RegionView3D b_null_region_view3d(PointerRNA_NULL);
  BufferParams buffer_params = BlenderSync::get_buffer_params(
      b_null_space_view3d, b_null_region_view3d, scene->camera, width, height);
  session->reset(buffer_params, session_params.samples);

  /* TODO(sergey): Decice on what is to be communicated to the engine here. There is no tiled
   * rendering for from visual point of view when render buffer fits big tile. But for huge
   * render resolutions it might still be helpful to see which big tile is being sampled. */
  /* TODO(sergey): If some logic is needed here, de-duplicate it with the constructor using some
   * sort of utility function. */
  /* b_engine.use_highlight_tiles(session_params.progressive_refine == false); */

  /* reset time */
  start_resize_time = 0.0;
}

void BlenderSession::free_session()
{
  session->cancel(true);

  delete sync;
  delete session;
}

void BlenderSession::do_write_update_render_tile(bool do_update_only)
{
  const int2 tile_offset = session->get_render_tile_offset();
  const int2 tile_size = session->get_render_tile_size();

  /* get render result */
  BL::RenderResult b_rr = b_engine.begin_result(tile_offset.x,
                                                tile_offset.y,
                                                tile_size.x,
                                                tile_size.y,
                                                b_rlay_name.c_str(),
                                                b_rview_name.c_str());

  /* can happen if the intersected rectangle gives 0 width or height */
  if (b_rr.ptr.data == NULL) {
    return;
  }

  BL::RenderResult::layers_iterator b_single_rlay;
  b_rr.layers.begin(b_single_rlay);

  /* layer will be missing if it was disabled in the UI */
  if (b_single_rlay == b_rr.layers.end())
    return;

  BL::RenderLayer b_rlay = *b_single_rlay;

  if (do_update_only) {
    update_render_result(b_rlay);
  }
  else {
    write_render_result(b_rlay);
  }

  b_engine.end_result(b_rr, true, false, true);
}

void BlenderSession::read_render_tile()
{
  const int2 tile_offset = session->get_render_tile_offset();
  const int2 tile_size = session->get_render_tile_size();

  /* get render result */
  BL::RenderResult b_rr = b_engine.begin_result(tile_offset.x,
                                                tile_offset.y,
                                                tile_size.x,
                                                tile_size.y,
                                                b_rlay_name.c_str(),
                                                b_rview_name.c_str());

  /* can happen if the intersected rectangle gives 0 width or height */
  if (b_rr.ptr.data == NULL) {
    return;
  }

  BL::RenderResult::layers_iterator b_single_rlay;
  b_rr.layers.begin(b_single_rlay);

  /* layer will be missing if it was disabled in the UI */
  if (b_single_rlay == b_rr.layers.end())
    return;

  BL::RenderLayer b_rlay = *b_single_rlay;

  vector<float> pixels(tile_size.x * tile_size.y * 4);

  /* Copy each pass.
   * TODO:copy only the required ones for better performance? */
  for (BL::RenderPass &b_pass : b_rlay.passes) {
    session->set_render_tile_pixels(b_pass.name(), b_pass.channels(), (float *)b_pass.rect());
  }

  b_engine.end_result(b_rr, false, false, false);
}

void BlenderSession::write_render_tile()
{
  do_write_update_render_tile(false);
}

void BlenderSession::update_render_tile()
{
  /* use final write for preview renders, otherwise render result wouldn't be
   * be updated in blender side
   * would need to be investigated a bit further, but for now shall be fine
   */
  if (!b_engine.is_preview())
    do_write_update_render_tile(true);
  else
    do_write_update_render_tile(false);
}

static void add_cryptomatte_layer(BL::RenderResult &b_rr, string name, string manifest)
{
  string identifier = string_printf("%08x", util_murmur_hash3(name.c_str(), name.length(), 0));
  string prefix = "cryptomatte/" + identifier.substr(0, 7) + "/";

  render_add_metadata(b_rr, prefix + "name", name);
  render_add_metadata(b_rr, prefix + "hash", "MurmurHash3_32");
  render_add_metadata(b_rr, prefix + "conversion", "uint32_to_float32");
  render_add_metadata(b_rr, prefix + "manifest", manifest);
}

void BlenderSession::stamp_view_layer_metadata(Scene *scene, const string &view_layer_name)
{
  BL::RenderResult b_rr = b_engine.get_result();
  string prefix = "cycles." + view_layer_name + ".";

  /* Configured number of samples for the view layer. */
  b_rr.stamp_data_add_field((prefix + "samples").c_str(),
                            to_string(session->params.samples).c_str());

  /* Store ranged samples information. */
  /* TODO(sergey): Need to bring this information back. */
#if 0
  if (session->tile_manager.range_num_samples != -1) {
    b_rr.stamp_data_add_field((prefix + "range_start_sample").c_str(),
                              to_string(session->tile_manager.range_start_sample).c_str());
    b_rr.stamp_data_add_field((prefix + "range_num_samples").c_str(),
                              to_string(session->tile_manager.range_num_samples).c_str());
  }
#endif

  /* Write cryptomatte metadata. */
  if (scene->film->get_cryptomatte_passes() & CRYPT_OBJECT) {
    add_cryptomatte_layer(b_rr,
                          view_layer_name + ".CryptoObject",
                          scene->object_manager->get_cryptomatte_objects(scene));
  }
  if (scene->film->get_cryptomatte_passes() & CRYPT_MATERIAL) {
    add_cryptomatte_layer(b_rr,
                          view_layer_name + ".CryptoMaterial",
                          scene->shader_manager->get_cryptomatte_materials(scene));
  }
  if (scene->film->get_cryptomatte_passes() & CRYPT_ASSET) {
    add_cryptomatte_layer(b_rr,
                          view_layer_name + ".CryptoAsset",
                          scene->object_manager->get_cryptomatte_assets(scene));
  }

  /* Store synchronization and bare-render times. */
  double total_time, render_time;
  session->progress.get_time(total_time, render_time);
  b_rr.stamp_data_add_field((prefix + "total_time").c_str(),
                            time_human_readable_from_seconds(total_time).c_str());
  b_rr.stamp_data_add_field((prefix + "render_time").c_str(),
                            time_human_readable_from_seconds(render_time).c_str());
  b_rr.stamp_data_add_field((prefix + "synchronization_time").c_str(),
                            time_human_readable_from_seconds(total_time - render_time).c_str());
}

void BlenderSession::render(BL::Depsgraph &b_depsgraph_)
{
  b_depsgraph = b_depsgraph_;

  if (session->progress.get_cancel()) {
    update_status_progress();
    return;
  }

  /* set callback to write out render results */
  session->write_render_tile_cb = [&]() { write_render_tile(); };
  session->update_render_tile_cb = [&]() { update_render_tile(); };

  BL::ViewLayer b_view_layer = b_depsgraph.view_layer_eval();

  /* get buffer parameters */
  SessionParams session_params = BlenderSync::get_session_params(
      b_engine, b_userpref, b_scene, background);
  BufferParams buffer_params = BlenderSync::get_buffer_params(
      b_v3d, b_rv3d, scene->camera, width, height);

  /* temporary render result to find needed passes and views */
  BL::RenderResult b_rr = b_engine.begin_result(0, 0, 1, 1, b_view_layer.name().c_str(), NULL);
  BL::RenderResult::layers_iterator b_single_rlay;
  b_rr.layers.begin(b_single_rlay);
  BL::RenderLayer b_rlay = *b_single_rlay;
  b_rlay_name = b_view_layer.name();

  /* Compute render passes and film settings. */
  sync->sync_render_passes(b_rlay, b_view_layer);

  BL::RenderResult::views_iterator b_view_iter;

  int num_views = 0;
  for (b_rr.views.begin(b_view_iter); b_view_iter != b_rr.views.end(); ++b_view_iter) {
    num_views++;
  }

  int view_index = 0;
  for (b_rr.views.begin(b_view_iter); b_view_iter != b_rr.views.end();
       ++b_view_iter, ++view_index) {
    b_rview_name = b_view_iter->name();

    /* set the current view */
    b_engine.active_view_set(b_rview_name.c_str());

    /* update scene */
    BL::Object b_camera_override(b_engine.camera_override());
    sync->sync_camera(b_render, b_camera_override, width, height, b_rview_name.c_str());
    sync->sync_data(
        b_render, b_depsgraph, b_v3d, b_camera_override, width, height, &python_thread_state);
    builtin_images_load();

    /* Attempt to free all data which is held by Blender side, since at this
     * point we know that we've got everything to render current view layer.
     */
    /* At the moment we only free if we are not doing multi-view
     * (or if we are rendering the last view). See T58142/D4239 for discussion.
     */
    if (view_index == num_views - 1) {
      free_blender_memory_if_possible();
    }

    /* Make sure all views have different noise patterns. - hardcoded value just to make it random
     */
    if (view_index != 0) {
      int seed = scene->integrator->get_seed();
      seed += hash_uint2(seed, hash_uint2(view_index * 0xdeadbeef, 0));
      scene->integrator->set_seed(seed);
    }

    /* Update number of samples per layer. */
    int samples = sync->get_layer_samples();
    bool bound_samples = sync->get_layer_bound_samples();
    int effective_layer_samples;

    if (samples != 0 && (!bound_samples || (samples < session_params.samples)))
      effective_layer_samples = samples;
    else
      effective_layer_samples = session_params.samples;

    /* Update tile manager if we're doing resumable render. */
    update_resumable_tile_manager(effective_layer_samples);

    /* Update session itself. */
    session->reset(buffer_params, effective_layer_samples);

    /* render */
    if (!b_engine.is_preview() && background && print_render_stats) {
      scene->enable_update_stats();
    }

    session->start();
    session->wait();

    if (!b_engine.is_preview() && background && print_render_stats) {
      RenderStats stats;
      session->collect_statistics(&stats);
      printf("Render statistics:\n%s\n", stats.full_report().c_str());
    }

    if (session->progress.get_cancel())
      break;
  }

  /* add metadata */
  stamp_view_layer_metadata(scene, b_rlay_name);

  /* free result without merging */
  b_engine.end_result(b_rr, true, true, false);

  double total_time, render_time;
  session->progress.get_time(total_time, render_time);
  VLOG(1) << "Total render time: " << total_time;
  VLOG(1) << "Render time (without synchronization): " << render_time;

  /* clear callback */
  session->write_render_tile_cb = function_null;
  session->update_render_tile_cb = function_null;
}

static PassType bake_type_to_pass(const string &bake_type_str, const int bake_filter)
{
  const char *bake_type = bake_type_str.c_str();

  /* data passes */
  if (strcmp(bake_type, "NORMAL") == 0) {
    return PASS_NORMAL;
  }
  else if (strcmp(bake_type, "UV") == 0) {
    return PASS_UV;
  }
  else if (strcmp(bake_type, "ROUGHNESS") == 0) {
    return PASS_ROUGHNESS;
  }
  else if (strcmp(bake_type, "EMIT") == 0) {
    return PASS_EMISSION;
  }
  /* light passes */
  else if (strcmp(bake_type, "AO") == 0) {
    return PASS_AO;
  }
  else if (strcmp(bake_type, "COMBINED") == 0) {
    return PASS_COMBINED;
  }
  else if (strcmp(bake_type, "SHADOW") == 0) {
    return PASS_SHADOW;
  }
  else if (strcmp(bake_type, "DIFFUSE") == 0) {
    /* TODO: arbitrary combinations. */
    if (bake_filter & BL::BakeSettings::pass_filter_DIRECT) {
      return PASS_DIFFUSE_DIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_INDIRECT) {
      return PASS_DIFFUSE_INDIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_COLOR) {
      return PASS_DIFFUSE_COLOR;
    }
  }
  else if (strcmp(bake_type, "GLOSSY") == 0) {
    /* TODO: arbitrary combinations. */
    if (bake_filter & BL::BakeSettings::pass_filter_DIRECT) {
      return PASS_GLOSSY_DIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_INDIRECT) {
      return PASS_GLOSSY_INDIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_COLOR) {
      return PASS_GLOSSY_COLOR;
    }
  }
  else if (strcmp(bake_type, "TRANSMISSION") == 0) {
    /* TODO: arbitrary combinations. */
    if (bake_filter & BL::BakeSettings::pass_filter_DIRECT) {
      return PASS_TRANSMISSION_DIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_INDIRECT) {
      return PASS_TRANSMISSION_INDIRECT;
    }
    else if (bake_filter & BL::BakeSettings::pass_filter_COLOR) {
      return PASS_TRANSMISSION_COLOR;
    }
  }
  /* extra */
  else if (strcmp(bake_type, "ENVIRONMENT") == 0) {
    return PASS_BACKGROUND;
  }

  return PASS_COMBINED;
}

void BlenderSession::bake(BL::Depsgraph &b_depsgraph_,
                          BL::Object &b_object,
                          const string &bake_type,
                          const int bake_filter,
                          const int bake_width,
                          const int bake_height)
{
  b_depsgraph = b_depsgraph_;

  /* Initialize bake manager, before we load the baking kernels. */
  scene->bake_manager->set(scene, b_object.name());

  /* Add render pass that we want to bake, and name it Combined so that it is
   * used as that on the Blender side. */
  const PassType pass_type = bake_type_to_pass(bake_type, bake_filter);
  Pass::add(pass_type, scene->passes, "Combined");

  session->read_render_tile_cb = [&]() { read_render_tile(); };
  session->write_render_tile_cb = [&]() { write_render_tile(); };

  if (!session->progress.get_cancel()) {
    /* Sync scene. */
    BL::Object b_camera_override(b_engine.camera_override());
    sync->sync_camera(b_render, b_camera_override, width, height, "");
    sync->sync_data(
        b_render, b_depsgraph, b_v3d, b_camera_override, width, height, &python_thread_state);
    builtin_images_load();
  }

  /* Object might have been disabled for rendering or excluded in some
   * other way, in that case Blender will report a warning afterwards. */
  bool object_found = false;
  foreach (Object *ob, scene->objects) {
    if (ob->name == b_object.name()) {
      object_found = true;
      break;
    }
  }

  if (object_found && !session->progress.get_cancel()) {
    /* Get session and buffer parameters. */
    SessionParams session_params = BlenderSync::get_session_params(
        b_engine, b_userpref, b_scene, background);

    BufferParams buffer_params;
    buffer_params.width = bake_width;
    buffer_params.height = bake_height;

    /* Update session. */
    session->reset(buffer_params, session_params.samples);

    session->progress.set_update_callback(
        function_bind(&BlenderSession::update_bake_progress, this));
  }

  /* Perform bake. Check cancel to avoid crash with incomplete scene data. */
  if (object_found && !session->progress.get_cancel()) {
    session->start();
    session->wait();
  }

  session->read_render_tile_cb = function_null;
  session->write_render_tile_cb = function_null;
}

void BlenderSession::write_render_result(BL::RenderLayer &b_rlay)
{
  const int2 tile_size = session->get_render_tile_size();
  vector<float> pixels(tile_size.x * tile_size.y * 4);

  /* Copy each pass. */
  for (BL::RenderPass &b_pass : b_rlay.passes) {
    if (!session->get_render_tile_pixels(b_pass.name(), b_pass.channels(), &pixels[0])) {
      memset(&pixels[0], 0, pixels.size() * sizeof(float));
    }

    b_pass.rect(&pixels[0]);
  }
}

void BlenderSession::update_render_result(BL::RenderLayer &b_rlay)
{
  const int2 tile_size = session->get_render_tile_size();
  vector<float> pixels(tile_size.x * tile_size.y * 4);

  /* Copy combined pass. */
  BL::RenderPass b_combined_pass(b_rlay.passes.find_by_name("Combined", b_rview_name.c_str()));
  if (session->get_render_tile_pixels("Combined", b_combined_pass.channels(), &pixels[0])) {
    b_combined_pass.rect(&pixels[0]);
  }
}

void BlenderSession::synchronize(BL::Depsgraph &b_depsgraph_)
{
  /* only used for viewport render */
  if (!b_v3d)
    return;

  /* on session/scene parameter changes, we recreate session entirely */
  SessionParams session_params = BlenderSync::get_session_params(
      b_engine, b_userpref, b_scene, background);
  SceneParams scene_params = BlenderSync::get_scene_params(b_scene, background);
  bool session_pause = BlenderSync::get_session_pause(b_scene, background);

  if (session->params.modified(session_params) || scene->params.modified(scene_params)) {
    free_session();
    create_session();
  }

  /* increase samples, but never decrease */
  session->set_samples(session_params.samples);
  session->set_pause(session_pause);

  /* copy recalc flags, outside of mutex so we can decide to do the real
   * synchronization at a later time to not block on running updates */
  sync->sync_recalc(b_depsgraph_, b_v3d);

  /* don't do synchronization if on pause */
  if (session_pause) {
    tag_update();
    return;
  }

  /* try to acquire mutex. if we don't want to or can't, come back later */
  if (!session->ready_to_reset() || !session->scene->mutex.try_lock()) {
    tag_update();
    return;
  }

  /* data and camera synchronize */
  b_depsgraph = b_depsgraph_;

  BL::Object b_camera_override(b_engine.camera_override());
  sync->sync_data(
      b_render, b_depsgraph, b_v3d, b_camera_override, width, height, &python_thread_state);

  if (b_rv3d)
    sync->sync_view(b_v3d, b_rv3d, width, height);
  else
    sync->sync_camera(b_render, b_camera_override, width, height, "");

  /* get buffer parameters */
  BufferParams buffer_params = BlenderSync::get_buffer_params(
      b_v3d, b_rv3d, scene->camera, width, height);

  /* reset if needed */
  if (scene->need_reset()) {
    session->reset(buffer_params, session_params.samples);

    /* After session reset, so device is not accessing image data anymore. */
    builtin_images_load();

    /* reset time */
    start_resize_time = 0.0;
  }

  /* unlock */
  session->scene->mutex.unlock();

  /* Start rendering thread, if it's not running already. Do this
   * after all scene data has been synced at least once. */
  session->start();
}

void BlenderSession::draw(int w, int h)
{
  /* pause in redraw in case update is not being called due to final render */
  session->set_pause(BlenderSync::get_session_pause(b_scene, background));

  /* before drawing, we verify camera and viewport size changes, because
   * we do not get update callbacks for those, we must detect them here */
  if (session->ready_to_reset()) {
    bool reset = false;

    /* if dimensions changed, reset */
    if (width != w || height != h) {
      if (start_resize_time == 0.0) {
        /* don't react immediately to resizes to avoid flickery resizing
         * of the viewport, and some window managers changing the window
         * size temporarily on unminimize */
        start_resize_time = time_dt();
        tag_redraw();
      }
      else if (time_dt() - start_resize_time < 0.2) {
        tag_redraw();
      }
      else {
        width = w;
        height = h;
        reset = true;
      }
    }

    /* try to acquire mutex. if we can't, come back later */
    if (!session->scene->mutex.try_lock()) {
      tag_update();
    }
    else {
      /* update camera from 3d view */

      sync->sync_view(b_v3d, b_rv3d, width, height);

      if (scene->camera->is_modified())
        reset = true;

      session->scene->mutex.unlock();
    }

    /* reset if requested */
    if (reset) {
      SessionParams session_params = BlenderSync::get_session_params(
          b_engine, b_userpref, b_scene, background);
      BufferParams buffer_params = BlenderSync::get_buffer_params(
          b_v3d, b_rv3d, scene->camera, width, height);
      bool session_pause = BlenderSync::get_session_pause(b_scene, background);

      if (session_pause == false) {
        session->reset(buffer_params, session_params.samples);
        start_resize_time = 0.0;
      }
    }
  }
  else {
    tag_update();
  }

  /* update status and progress for 3d view draw */
  update_status_progress();

  /* draw */
  session->draw();
}

void BlenderSession::get_status(string &status, string &substatus)
{
  session->progress.get_status(status, substatus);
}

void BlenderSession::get_progress(float &progress, double &total_time, double &render_time)
{
  session->progress.get_time(total_time, render_time);
  progress = session->progress.get_progress();
}

void BlenderSession::update_bake_progress()
{
  float progress = session->progress.get_progress();

  if (progress != last_progress) {
    b_engine.update_progress(progress);
    last_progress = progress;
  }
}

void BlenderSession::update_status_progress()
{
  string timestatus, status, substatus;
  string scene_status = "";
  float progress;
  double total_time, remaining_time = 0, render_time;
  float mem_used = (float)session->stats.mem_used / 1024.0f / 1024.0f;
  float mem_peak = (float)session->stats.mem_peak / 1024.0f / 1024.0f;

  get_status(status, substatus);
  get_progress(progress, total_time, render_time);

  if (progress > 0)
    remaining_time = (1.0 - (double)progress) * (render_time / (double)progress);

  if (background) {
    if (scene)
      scene_status += " | " + scene->name;
    if (b_rlay_name != "")
      scene_status += ", " + b_rlay_name;

    if (b_rview_name != "")
      scene_status += ", " + b_rview_name;

    if (remaining_time > 0) {
      timestatus += "Remaining:" + time_human_readable_from_seconds(remaining_time) + " | ";
    }

    timestatus += string_printf("Mem:%.2fM, Peak:%.2fM", (double)mem_used, (double)mem_peak);

    if (status.size() > 0)
      status = " | " + status;
    if (substatus.size() > 0)
      status += " | " + substatus;
  }

  double current_time = time_dt();
  /* When rendering in a window, redraw the status at least once per second to keep the elapsed
   * and remaining time up-to-date. For headless rendering, only report when something
   * significant changes to keep the console output readable. */
  if (status != last_status || (!headless && (current_time - last_status_time) > 1.0)) {
    b_engine.update_stats("", (timestatus + scene_status + status).c_str());
    b_engine.update_memory_stats(mem_used, mem_peak);
    last_status = status;
    last_status_time = current_time;
  }
  if (progress != last_progress) {
    b_engine.update_progress(progress);
    last_progress = progress;
  }

  if (session->progress.get_error()) {
    string error = session->progress.get_error_message();
    if (error != last_error) {
      /* TODO(sergey): Currently C++ RNA API doesn't let us to
       * use mnemonic name for the variable. Would be nice to
       * have this figured out.
       *
       * For until then, 1 << 5 means RPT_ERROR.
       */
      b_engine.report(1 << 5, error.c_str());
      b_engine.error_set(error.c_str());
      last_error = error;
    }
  }
}

void BlenderSession::tag_update()
{
  /* tell blender that we want to get another update callback */
  b_engine.tag_update();
}

void BlenderSession::tag_redraw()
{
  if (background) {
    /* update stats and progress, only for background here because
     * in 3d view we do it in draw for thread safety reasons */
    update_status_progress();

    /* offline render, redraw if timeout passed */
    if (time_dt() - last_redraw_time > 1.0) {
      b_engine.tag_redraw();
      last_redraw_time = time_dt();
    }
  }
  else {
    /* tell blender that we want to redraw */
    b_engine.tag_redraw();
  }
}

void BlenderSession::test_cancel()
{
  /* test if we need to cancel rendering */
  if (background)
    if (b_engine.test_break())
      session->progress.set_cancel("Cancelled");
}

void BlenderSession::update_resumable_tile_manager(int num_samples)
{
  const int num_resumable_chunks = BlenderSession::num_resumable_chunks,
            current_resumable_chunk = BlenderSession::current_resumable_chunk;
  if (num_resumable_chunks == 0) {
    return;
  }

  if (num_resumable_chunks > num_samples) {
    fprintf(stderr,
            "Cycles warning: more sample chunks (%d) than samples (%d), "
            "this will cause some samples to be included in multiple chunks.\n",
            num_resumable_chunks,
            num_samples);
  }

  const float num_samples_per_chunk = (float)num_samples / num_resumable_chunks;

  float range_start_sample, range_num_samples;
  if (current_resumable_chunk != 0) {
    /* Single chunk rendering. */
    range_start_sample = num_samples_per_chunk * (current_resumable_chunk - 1);
    range_num_samples = num_samples_per_chunk;
  }
  else {
    /* Ranged-chunks. */
    const int num_chunks = end_resumable_chunk - start_resumable_chunk + 1;
    range_start_sample = num_samples_per_chunk * (start_resumable_chunk - 1);
    range_num_samples = num_chunks * num_samples_per_chunk;
  }

  /* Round after doing the multiplications with num_chunks and num_samples_per_chunk
   * to allow for many small chunks. */
  int rounded_range_start_sample = (int)floorf(range_start_sample + 0.5f);
  int rounded_range_num_samples = max((int)floorf(range_num_samples + 0.5f), 1);

  /* Make sure we don't overshoot. */
  if (rounded_range_start_sample + rounded_range_num_samples > num_samples) {
    rounded_range_num_samples = num_samples - rounded_range_num_samples;
  }

  VLOG(1) << "Samples range start is " << range_start_sample << ", "
          << "number of samples to render is " << range_num_samples;

  scene->integrator->set_start_sample(rounded_range_start_sample);

  /* TODO(sergey): Need to be brought back. */
#if 0
  session->tile_manager.range_start_sample = rounded_range_start_sample;
  session->tile_manager.range_num_samples = rounded_range_num_samples;
#endif
}

void BlenderSession::free_blender_memory_if_possible()
{
  if (!background) {
    /* During interactive render we can not free anything: attempts to save
     * memory would cause things to be allocated and evaluated for every
     * updated sample.
     */
    return;
  }
  b_engine.free_blender_memory();
}

CCL_NAMESPACE_END
