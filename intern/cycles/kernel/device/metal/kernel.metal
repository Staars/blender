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



#endif
