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

#ifdef WITH_METAL

#  include "device/device_intern.h"
#  include "device/metal/device_metal.h"

#  include <Cocoa/Cocoa.h>
#  include <Metal/Metal.h>
#  include <MetalPerformanceShaders/MetalPerformanceShaders.h>


#  include "util/util_debug.h"
#  include "util/util_logging.h"
#  include "util/util_md5.h"
#  include "util/util_path.h"
#  include "util/util_semaphore.h"
#  include "util/util_system.h"
#  include "util/util_time.h"

using std::cerr;
using std::endl;

CCL_NAMESPACE_BEGIN

MetalCache::Slot::ProgramEntry::ProgramEntry() : program(NULL), mutex(NULL)
{
}

MetalCache::Slot::ProgramEntry::ProgramEntry(const ProgramEntry &rhs)
    : program(rhs.program), mutex(NULL)
{
}

MetalCache::Slot::ProgramEntry::~ProgramEntry()
{
  delete mutex;
}

MetalCache::Slot::Slot() : context_mutex(NULL), context(NULL)
{
}

MetalCache::Slot::Slot(const Slot &rhs)
    : context_mutex(NULL), context(NULL), programs(rhs.programs)
{
}

MetalCache::Slot::~Slot()
{
  delete context_mutex;
}

MetalCache &MetalCache::global_instance()
{
  static MetalCache instance;
  return instance;
}

cl_context MetalCache::get_context(cl_platform_id platform,
                                    cl_device_id device,
                                    thread_scoped_lock &slot_locker)
{
  assert(platform != NULL);

  MetalCache &self = global_instance();

  thread_scoped_lock cache_lock(self.cache_lock);

  pair<CacheMap::iterator, bool> ins = self.cache.insert(
      CacheMap::value_type(PlatformDevicePair(platform, device), Slot()));

  Slot &slot = ins.first->second;

  /* create slot lock only while holding cache lock */
  if (!slot.context_mutex)
    slot.context_mutex = new thread_mutex;

  /* need to unlock cache before locking slot, to allow store to complete */
  cache_lock.unlock();

  /* lock the slot */
  slot_locker = thread_scoped_lock(*slot.context_mutex);

  /* If the thing isn't cached */
  if (slot.context == NULL) {
    /* return with the caller's lock holder holding the slot lock */
    return NULL;
  }

  /* the item was already cached, release the slot lock */
  slot_locker.unlock();

  cl_int ciErr = clRetainContext(slot.context);
  assert(ciErr == CL_SUCCESS);
  (void)ciErr;

  return slot.context;
}

cl_program MetalCache::get_program(cl_platform_id platform,
                                    cl_device_id device,
                                    ustring key,
                                    thread_scoped_lock &slot_locker)
{
  assert(platform != NULL);

  MetalCache &self = global_instance();

  thread_scoped_lock cache_lock(self.cache_lock);

  pair<CacheMap::iterator, bool> ins = self.cache.insert(
      CacheMap::value_type(PlatformDevicePair(platform, device), Slot()));

  Slot &slot = ins.first->second;

  pair<Slot::EntryMap::iterator, bool> ins2 = slot.programs.insert(
      Slot::EntryMap::value_type(key, Slot::ProgramEntry()));

  Slot::ProgramEntry &entry = ins2.first->second;

  /* create slot lock only while holding cache lock */
  if (!entry.mutex)
    entry.mutex = new thread_mutex;

  /* need to unlock cache before locking slot, to allow store to complete */
  cache_lock.unlock();

  /* lock the slot */
  slot_locker = thread_scoped_lock(*entry.mutex);

  /* If the thing isn't cached */
  if (entry.program == NULL) {
    /* return with the caller's lock holder holding the slot lock */
    return NULL;
  }

  /* the item was already cached, release the slot lock */
  slot_locker.unlock();

  cl_int ciErr = clRetainProgram(entry.program);
  assert(ciErr == CL_SUCCESS);
  (void)ciErr;

  return entry.program;
}

void MetalCache::store_context(cl_platform_id platform,
                                cl_device_id device,
                                cl_context context,
                                thread_scoped_lock &slot_locker)
{
  assert(platform != NULL);
  assert(device != NULL);
  assert(context != NULL);

  MetalCache &self = global_instance();

  thread_scoped_lock cache_lock(self.cache_lock);
  CacheMap::iterator i = self.cache.find(PlatformDevicePair(platform, device));
  cache_lock.unlock();

  Slot &slot = i->second;

  /* sanity check */
  assert(i != self.cache.end());
  assert(slot.context == NULL);

  slot.context = context;

  /* unlock the slot */
  slot_locker.unlock();

  /* increment reference count in Metal.
   * The caller is going to release the object when done with it. */
  cl_int ciErr = clRetainContext(context);
  assert(ciErr == CL_SUCCESS);
  (void)ciErr;
}

void MetalCache::store_program(cl_platform_id platform,
                                cl_device_id device,
                                cl_program program,
                                ustring key,
                                thread_scoped_lock &slot_locker)
{
  assert(platform != NULL);
  assert(device != NULL);
  assert(program != NULL);

  MetalCache &self = global_instance();

  thread_scoped_lock cache_lock(self.cache_lock);

  CacheMap::iterator i = self.cache.find(PlatformDevicePair(platform, device));
  assert(i != self.cache.end());
  Slot &slot = i->second;

  Slot::EntryMap::iterator i2 = slot.programs.find(key);
  assert(i2 != slot.programs.end());
  Slot::ProgramEntry &entry = i2->second;

  assert(entry.program == NULL);

  cache_lock.unlock();

  entry.program = program;

  /* unlock the slot */
  slot_locker.unlock();

  /* Increment reference count in Metal.
   * The caller is going to release the object when done with it.
   */
  cl_int ciErr = clRetainProgram(program);
  assert(ciErr == CL_SUCCESS);
  (void)ciErr;
}

string MetalCache::get_kernel_md5()
{
  MetalCache &self = global_instance();
  thread_scoped_lock lock(self.kernel_md5_lock);

  if (self.kernel_md5.empty()) {
    self.kernel_md5 = path_files_md5_hash(path_get("source"));
  }
  return self.kernel_md5;
}

static string get_program_source(const string &kernel_file)
{
  string source = "#include \"kernel/kernels/metal/" + kernel_file + "\"\n";
  /* We compile kernels consisting of many files. unfortunately Metal
   * kernel caches do not seem to recognize changes in included files.
   * so we force recompile on changes by adding the md5 hash of all files.
   */
  source = path_source_replace_includes(source, path_get("source"));
  source += "\n// " + util_md5_string(source) + "\n";
  return source;
}

MetalDevice::MetalProgram::MetalProgram(MetalDevice *device,
                                           const string &program_name,
                                           const string &kernel_file,
                                           const string &kernel_build_options,
                                           bool use_stdout)
    : device(device),
      program_name(program_name),
      kernel_file(kernel_file),
      kernel_build_options(kernel_build_options),
      use_stdout(use_stdout)
{
  loaded = false;
  needs_compiling = true;
  program = NULL;
}

MetalDevice::MetalProgram::~MetalProgram()
{
  release();
}

void MetalDevice::MetalProgram::release()
{
  for (map<ustring, cl_kernel>::iterator kernel = kernels.begin(); kernel != kernels.end();
       ++kernel) {
    if (kernel->second) {
      clReleaseKernel(kernel->second);
      kernel->second = NULL;
    }
  }
  if (program) {
    clReleaseProgram(program);
    program = NULL;
  }
}

void MetalDevice::MetalProgram::add_log(const string &msg, bool debug)
{
  if (!use_stdout) {
    log += msg + "\n";
  }
  else if (!debug) {
    printf("%s\n", msg.c_str());
    fflush(stdout);
  }
  else {
    VLOG(2) << msg;
  }
}

void MetalDevice::MetalProgram::add_error(const string &msg)
{
  if (use_stdout) {
    fprintf(stderr, "%s\n", msg.c_str());
  }
  if (error_msg == "") {
    error_msg += "\n";
  }
  error_msg += msg;
}

void MetalDevice::MetalProgram::add_kernel(ustring name)
{
  if (!kernels.count(name)) {
    kernels[name] = NULL;
  }
}

bool MetalDevice::MetalProgram::build_kernel(const string *debug_src)
{
  string build_options;
  build_options = device->kernel_build_options(debug_src) + kernel_build_options;

  VLOG(1) << "Build options passed to clBuildProgram: '" << build_options << "'.";
//  cl_int ciErr = clBuildProgram(program, 0, NULL, build_options.c_str(), NULL, NULL);

  /* show warnings even if build is successful */
  size_t ret_val_size = 0;

//  clGetProgramBuildInfo(program, device->cdDevice, CL_PROGRAM_BUILD_LOG, 0, NULL, &ret_val_size);
//
//  if (ciErr != CL_SUCCESS) {
//    add_error(string("Metal build failed with error ") + clewErrorString(ciErr) +
//              ", errors in console.");
//  }

  if (ret_val_size > 1) {
    vector<char> build_log(ret_val_size + 1);
//    clGetProgramBuildInfo(
//        program, device->cdDevice, CL_PROGRAM_BUILD_LOG, ret_val_size, &build_log[0], NULL);

    build_log[ret_val_size] = '\0';
    /* Skip meaningless empty output from the NVidia compiler. */
    if (!(ret_val_size == 2 && build_log[0] == '\n')) {
      add_log(string("Metal program ") + program_name + " build output: " + string(&build_log[0]),
              true);
    }
  }

  return true;
//  return (ciErr == CL_SUCCESS);
}

bool MetalDevice::MetalProgram::compile_kernel(const string *debug_src)
{
  string source = get_program_source(kernel_file);

  if (debug_src) {
    path_write_text(*debug_src, source);
  }

  size_t source_len = source.size();
  const char *source_str = source.c_str();
  cl_int ciErr;

  program = clCreateProgramWithSource(device->cxContext, 1, &source_str, &source_len, &ciErr);

  if (ciErr != CL_SUCCESS) {
    add_error(string("Metal program creation failed: ") + clewErrorString(ciErr));
    return false;
  }

  double starttime = time_dt();
  add_log(string("Cycles: compiling Metal program ") + program_name + "...", false);
  add_log(string("Build flags: ") + kernel_build_options, true);

  if (!build_kernel(debug_src))
    return false;

  double elapsed = time_dt() - starttime;
  add_log(
      string_printf("Kernel compilation of %s finished in %.2lfs.", program_name.c_str(), elapsed),
      false);

  return true;
}

static void escape_python_string(string &str)
{
  /* Escape string to be passed as a Python raw string with '' quotes'. */
  string_replace(str, "'", "\'");
}

static int metal_compile_process_limit()
{
  /* Limit number of concurrent processes compiling, with a heuristic based
   * on total physical RAM and estimate of memory usage needed when compiling
   * with all Cycles features enabled.
   *
   * This is somewhat arbitrary as we don't know the actual available RAM or
   * how much the kernel compilation will needed depending on the features, but
   * better than not limiting at all. */
  static const int64_t GB = 1024LL * 1024LL * 1024LL;
  static const int64_t process_memory = 2 * GB;
  static const int64_t base_memory = 2 * GB;
  static const int64_t system_memory = system_physical_ram();
  static const int64_t process_limit = (system_memory - base_memory) / process_memory;

  return max((int)process_limit, 1);
}

bool MetalDevice::MetalProgram::compile_separate(const string &clbin)
{
  /* Construct arguments. */
  vector<string> args;
  args.push_back("--background");
  args.push_back("--factory-startup");
  args.push_back("--python-expr");

  int device_platform_id = device->device_num;
  string device_name = device->device_name;
  string platform_name = device->platform_name;
  string build_options = device->kernel_build_options(NULL) + kernel_build_options;
  string kernel_file_escaped = kernel_file;
  string clbin_escaped = clbin;

  escape_python_string(device_name);
  escape_python_string(platform_name);
  escape_python_string(build_options);
  escape_python_string(kernel_file_escaped);
  escape_python_string(clbin_escaped);

  args.push_back(string_printf(
      "import _cycles; _cycles.metal_compile(r'%d', r'%s', r'%s', r'%s', r'%s', r'%s')",
      device_platform_id,
      device_name.c_str(),
      platform_name.c_str(),
      build_options.c_str(),
      kernel_file_escaped.c_str(),
      clbin_escaped.c_str()));

  /* Limit number of concurrent processes compiling. */
  static thread_counting_semaphore semaphore(metal_compile_process_limit());
  semaphore.acquire();

  /* Compile. */
  const double starttime = time_dt();
  add_log(string("Cycles: compiling Metal program ") + program_name + "...", false);
  add_log(string("Build flags: ") + kernel_build_options, true);
  const bool success = system_call_self(args);
  const double elapsed = time_dt() - starttime;

  semaphore.release();

  if (!success || !path_exists(clbin)) {
    return false;
  }

  add_log(
      string_printf("Kernel compilation of %s finished in %.2lfs.", program_name.c_str(), elapsed),
      false);

  return load_binary(clbin);
}

/* Compile metal kernel. This method is called from the _cycles Python
 * module compile kernels. Parameters must match function above. */
bool device_metal_compile_kernel(const vector<string> &parameters)
{
  int device_platform_id = std::stoi(parameters[0]);
  const string &device_name = parameters[1];
  const string &platform_name = parameters[2];
  const string &build_options = parameters[3];
  const string &kernel_file = parameters[4];
  const string &binary_path = parameters[5];

  if (clewInit() != CLEW_SUCCESS) {
    return false;
  }

  vector<MetalPlatformDevice> usable_devices;
  MetalInfo::get_usable_devices(&usable_devices);
  if (device_platform_id >= usable_devices.size()) {
    return false;
  }

  MetalPlatformDevice &platform_device = usable_devices[device_platform_id];
  if (platform_device.platform_name != platform_name ||
      platform_device.device_name != device_name) {
    return false;
  }

//  cl_platform_id platform = platform_device.platform_id;
//  cl_device_id device = platform_device.device_id;
//  const cl_context_properties context_props[] = {
//      CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0, 0};
//
  cl_int err;
//  cl_context context = clCreateContext(context_props, 1, &device, NULL, NULL, &err);
//  if (err != CL_SUCCESS) {
//    return false;
//  }

//  string source = get_program_source(kernel_file);
//  size_t source_len = source.size();
//  const char *source_str = source.c_str();
//  cl_program program = clCreateProgramWithSource(context, 1, &source_str, &source_len, &err);
  bool result = false;
//
//  if (err == CL_SUCCESS) {
//    err = clBuildProgram(program, 0, NULL, build_options.c_str(), NULL, NULL);
//
//    if (err == CL_SUCCESS) {
//      size_t size = 0;
//      clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), &size, NULL);
//      if (size > 0) {
//        vector<uint8_t> binary(size);
//        uint8_t *bytes = &binary[0];
//        clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(uint8_t *), &bytes, NULL);
//        result = path_write_binary(binary_path, binary);
//      }
//    }
//    clReleaseProgram(program);
//  }
//
//  clReleaseContext(context);

  return result;
}

bool MetalDevice::MetalProgram::load_binary(const string &clbin, const string *debug_src)
{
//  /* read binary into memory */
//  vector<uint8_t> binary;
//
//  if (!path_read_binary(clbin, binary)) {
//    add_error(string_printf("Metal failed to read cached binary %s.", clbin.c_str()));
//    return false;
//  }
//
//  /* create program */
//  cl_int status, ciErr;
//  size_t size = binary.size();
//  const uint8_t *bytes = &binary[0];
//
//  program = clCreateProgramWithBinary(
//      device->cxContext, 1, &device->cdDevice, &size, &bytes, &status, &ciErr);
//
//  if (status != CL_SUCCESS || ciErr != CL_SUCCESS) {
//    add_error(string("Metal failed create program from cached binary ") + clbin + ": " +
//              clewErrorString(status) + " " + clewErrorString(ciErr));
//    return false;
//  }
//
//  if (!build_kernel(debug_src))
//    return false;

  return true;
}

bool MetalDevice::MetalProgram::save_binary(const string &clbin)
{
  size_t size = 0;
  clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), &size, NULL);

  if (!size)
    return false;

  vector<uint8_t> binary(size);
  uint8_t *bytes = &binary[0];

  clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(uint8_t *), &bytes, NULL);

  return path_write_binary(clbin, binary);
}

bool MetalDevice::MetalProgram::load()
{
  loaded = false;
  string device_md5 = device->device_md5_hash(kernel_build_options);

  /* Try to use cached kernel. */
  thread_scoped_lock cache_locker;
  ustring cache_key(program_name + device_md5);
  program = device->load_cached_kernel(cache_key, cache_locker);
  if (!program) {
    add_log(string("Metal program ") + program_name + " not found in cache.", true);

    /* need to create source to get md5 */
    string source = get_program_source(kernel_file);

    string basename = "cycles_kernel_" + program_name + "_" + device_md5 + "_" +
                      util_md5_string(source);
    basename = path_cache_get(path_join("kernels", basename));
    string clbin = basename + ".clbin";

    /* If binary kernel exists already, try use it. */
    if (path_exists(clbin) && load_binary(clbin)) {
      /* Kernel loaded from binary, nothing to do. */
      add_log(string("Loaded program from ") + clbin + ".", true);

      /* Cache the program. */
      device->store_cached_kernel(program, cache_key, cache_locker);
    }
    else {
      add_log(string("Metal program ") + program_name + " not found on disk.", true);
      cache_locker.unlock();
    }
  }

  if (program) {
    create_kernels();
    loaded = true;
    needs_compiling = false;
  }

  return loaded;
}

void MetalDevice::MetalProgram::compile()
{
  assert(device);

  string device_md5 = device->device_md5_hash(kernel_build_options);

  /* Try to use cached kernel. */
  thread_scoped_lock cache_locker;
  ustring cache_key(program_name + device_md5);
  program = device->load_cached_kernel(cache_key, cache_locker);

  if (!program) {

    add_log(string("Metal program ") + program_name + " not found in cache.", true);

    /* need to create source to get md5 */
    string source = get_program_source(kernel_file);

    string basename = "cycles_kernel_" + program_name + "_" + device_md5 + "_" +
                      util_md5_string(source);
    basename = path_cache_get(path_join("kernels", basename));
    string clbin = basename + ".clbin";

    /* path to preprocessed source for debugging */
    string clsrc, *debug_src = NULL;

    if (MetalInfo::use_debug()) {
      clsrc = basename + ".cl";
      debug_src = &clsrc;
    }

    if (DebugFlags().running_inside_blender && compile_separate(clbin)) {
      add_log(string("Built and loaded program from ") + clbin + ".", true);
      loaded = true;
    }
    else {
      if (DebugFlags().running_inside_blender) {
        add_log(string("Separate-process building of ") + clbin +
                    " failed, will fall back to regular building.",
                true);
      }

      /* If does not exist or loading binary failed, compile kernel. */
      if (!compile_kernel(debug_src)) {
        needs_compiling = false;
        return;
      }

      /* Save binary for reuse. */
      if (!save_binary(clbin)) {
        add_log(string("Saving compiled Metal kernel to ") + clbin + " failed!", true);
      }
    }

    /* Cache the program. */
    device->store_cached_kernel(program, cache_key, cache_locker);
  }

  create_kernels();
  needs_compiling = false;
  loaded = true;
}

void MetalDevice::MetalProgram::create_kernels()
{
  for (map<ustring, cl_kernel>::iterator kernel = kernels.begin(); kernel != kernels.end();
       ++kernel) {
    assert(kernel->second == NULL);
    cl_int ciErr;
    string name = "kernel_ocl_" + kernel->first.string();
    kernel->second = clCreateKernel(program, name.c_str(), &ciErr);
    if (device->metal_error(ciErr)) {
      add_error(string("Error getting kernel ") + name + " from program " + program_name + ": " +
                clewErrorString(ciErr));
      return;
    }
  }
}

bool MetalDevice::MetalProgram::wait_for_availability()
{
  add_log(string("Waiting for availability of ") + program_name + ".", true);
  while (needs_compiling) {
    time_sleep(0.1);
  }
  return loaded;
}

void MetalDevice::MetalProgram::report_error()
{
  /* If loaded is true, there was no error. */
  if (loaded)
    return;
  /* if use_stdout is true, the error was already reported. */
  if (use_stdout)
    return;

  cerr << error_msg << endl;
  if (!compile_output.empty()) {
    cerr << "Metal kernel build output for " << program_name << ":" << endl;
    cerr << compile_output << endl;
  }
}

cl_kernel MetalDevice::MetalProgram::operator()()
{
  assert(kernels.size() == 1);
  return kernels.begin()->second;
}

cl_kernel MetalDevice::MetalProgram::operator()(ustring name)
{
  assert(kernels.count(name));
  return kernels[name];
}

bool MetalInfo::use_debug()
{
//  return DebugFlags().metal.debug;
  return false;
}

void MetalInfo::get_usable_devices(vector<MetalPlatformDevice> *usable_devices)
{
  static bool first_time = true;
#  define FIRST_VLOG(severity) \
    if (first_time) \
    VLOG(severity)

  VLOG(1) << "STAARS:  MTLCopyAllDevices";
  NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
  
  if (devices.count == 0) {
    FIRST_VLOG(2) << "No Metal platforms were found.";
    first_time = false;
    return;
  }
  
  usable_devices->clear();
  
  for (id device in devices) {
    VLOG(1) << "device: " << [device name].UTF8String;
    VLOG(1) << "device: " << [device isHeadless];
    VLOG(1) << "device: " << [device registryID];
    VLOG(1) << "device: " << [device locationNumber];
    if (MPSSupportsMTLDevice(device)){
      VLOG(1) << "MPS supported!!";

      string platform_name = "APPLE";
      string readable_device_name = [device name].UTF8String;
      readable_device_name += " GPU";
      string hardware_id = string_printf("ID_%d", [device registryID]);
      string device_extensions = platform_name;
      
      cl_device_type device_type = CL_DEVICE_TYPE_GPU;
      
      usable_devices->push_back(MetalPlatformDevice(platform_name,
                                                     device_type,
                                                     readable_device_name,
                                                     hardware_id,
                                                     device_extensions));
    }
  }

  first_time = false;
}



//int MetalInfo::mem_sub_ptr_alignment(cl_device_id device_id)
//{
//  int base_align_bits;
//  if (clGetDeviceInfo(
//          device_id, CL_DEVICE_MEM_BASE_ADDR_ALIGN, sizeof(int), &base_align_bits, NULL) ==
//      CL_SUCCESS) {
//    return base_align_bits / 8;
//  }
//  return 1;
//}

CCL_NAMESPACE_END

#endif
