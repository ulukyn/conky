/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2008 Markus Meissner
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 *
 * Author:
 * Fonic <fonic.maxxim@live.com>
 *
 * Things to do:
 * - Move decoding of GPU/MEM freqs to print_nvidia_value() using QUERY_SPECIAL
 *   so that all quirks are located there
 * - Implement nvs->print_type to allow control over how the value is printed
 *   (int, float, temperature...)
 *
 * Showcase (conky.conf):
 * --==| NVIDIA | ==--
 * GPU    ${nvidia gpufreq [target_id]}MHz (${nvidia gpufreqmin
 * [target_id]}-${nvidia gpufreqmax [target_id]}MHz) MEM    ${nvidia memfreq
 * [target_id]}MHz (${nvidia memfreqmin [target_id]}-${nvidia memfreqmax
 * [target_id]}MHz) MTR    ${nvidia mtrfreq [target_id]}MHz (${nvidia mtrfreqmin
 * [target_id]}-${nvidia mtrfreqmax [target_id]}MHz) PERF   Level ${nvidia
 * perflevel [target_id]} (${nvidia perflevelmin [target_id]}-${nvidia
 * perflevelmax [target_id]}), Mode: ${nvidia perfmode [target_id]} VRAM
 * ${nvidia memutil [target_id]}% (${nvidia memused [target_id]}MB/${nvidia
 * memtotal [target_id]}MB) LOAD   GPU ${nvidia gpuutil [target_id]}%, RAM
 * ${nvidia membwutil [target_id]}%, VIDEO ${nvidia videoutil [target_id]}%,
 * PCIe ${nvidia pcieutil [target_id]}% TEMP   GPU ${nvidia gputemp
 * [target_id]}°C (${nvidia gputempthreshold [target_id]}°C max.), SYS ${nvidia
 * ambienttemp [target_id]}°C FAN    ${nvidia fanspeed [target_id]} RPM
 * (${nvidia fanlevel [target_id]}%)
 *
 * Miscellaneous:
 * OPENGL ${nvidia imagequality [target_id]}
 * GPU    ${nvidia modelname [target_id]}
 * DRIVER ${nvidia driverversion [target_id]}
 *
 * --==| NVIDIA Bars |==--
 * LOAD  ${nvidiabar [height][,width] gpuutil [target_id]}
 * VRAM  ${nvidiabar [height][,width] memutil [target_id]}
 * RAM   ${nvidiabar [height][,width] membwutil [target_id]}
 * VIDEO ${nvidiabar [height][,width] videoutil [target_id]}
 * PCIe  ${nvidiabar [height][,width] pcieutil [target_id]}
 * Fan   ${nvidiabar [height][,width] fanlevel [target_id]}
 * TEMP  ${nvidiabar [height][,width] gputemp [target_id]}
 *
 * --==| NVIDIA Gauge |==--
 * LOAD  ${nvidiagauge [height][,width] gpuutil [target_id]}
 * VRAM  ${nvidiagauge [height][,width] memutil [target_id]}
 * RAM   ${nvidiagauge [height][,width] membwutil [target_id]}
 * VIDEO ${nvidiagauge [height][,width] videoutil [target_id]}
 * PCIe  ${nvidiagauge [height][,width] pcieutil [target_id]}
 * Fan   ${nvidiagauge [height][,width] fanlevel [target_id]}
 * TEMP  ${nvidiagauge [height][,width] gputemp [target_id]}
 *
 * --==| NVIDIA Graph |==-- (target_id is not optional in this case)
 * LOAD  ${nvidiagraph gpuutil [height][,width] [gradient color 1] [gradient
 * color 2] [scale] [-t] [-l] target_id} VRAM  ${nvidiagraph memutil
 * [height][,width] [gradient color 1] [gradient color 2] [scale] [-t] [-l]
 * target_id} RAM   ${nvidiagraph membwutil [height][,width] [gradient color 1]
 * [gradient color 2] [scale] [-t] [-l] target_id} VIDEO ${nvidiagraph videoutil
 * [height][,width] [gradient color 1] [gradient color 2] [scale] [-t] [-l]
 * target_id} PCIe  ${nvidiagraph pcieutil [height][,width] [gradient color 1]
 * [gradient color 2] [scale] [-t] [-l] target_id} Fan   ${nvidiagraph fanlevel
 * [height][,width] [gradient color 1] [gradient color 2] [scale] [-t] [-l]
 * target_id} TEMP  ${nvidiagraph gputemp [height][,width] [gradient color 1]
 * [gradient color 2] [scale] [-t] [-l] target_id}
 */

#include "nvidia.h"
#include <X11/Xlib.h>
#include "NVCtrl/NVCtrl.h"
#include "NVCtrl/NVCtrlLib.h"
#include "../../conky.h"
#include "../../logging.h"
#include "../../content/temphelper.h"

// Current implementation uses X11 specific system utils
#include "../../output/x11.h"

#include <memory>

// Separators for nvidia string parsing
// (sample: "perf=0, nvclock=324, nvclockmin=324, nvclockmax=324 ; perf=1,
// nvclock=549, nvclockmin=549, nvclockmax=549")
#define NV_KVPAIR_SEPARATORS ", ;"
#define NV_KEYVAL_SEPARATORS "="

// Module arguments
const char *translate_module_argument[] = {
    "temp",  // Temperatures
    "gputemp", "threshold", "gputempthreshold", "ambient", "ambienttemp",

    "gpufreq",  // GPU frequency
    "gpufreqcur", "gpufreqmin", "gpufreqmax",

    "memfreq",  // Memory frequency
    "memfreqcur", "memfreqmin", "memfreqmax",

    "mtrfreq",  // Memory transfer rate frequency
    "mtrfreqcur", "mtrfreqmin", "mtrfreqmax",

    "perflevel",  // Performance levels
    "perflevelcur", "perflevelmin", "perflevelmax", "perfmode",

    "gpuutil",    // Load/utilization
    "membwutil",  // NOTE: this is the memory _bandwidth_ utilization, not the
                  // percentage of used/available memory!
    "videoutil", "pcieutil",

    "mem",  // RAM statistics
    "memused", "memfree", "memavail", "memmax", "memtotal", "memutil",
    "memperc",

    "fanspeed",  // Fan/cooler
    "fanlevel",

    "imagequality",  // Miscellaneous
    "modelname", "driverversion"};

// Enum for module arguments
typedef enum _ARG_ID {
  ARG_TEMP,
  ARG_GPU_TEMP,
  ARG_THRESHOLD,
  ARG_GPU_TEMP_THRESHOLD,
  ARG_AMBIENT,
  ARG_AMBIENT_TEMP,

  ARG_GPU_FREQ,
  ARG_GPU_FREQ_CUR,
  ARG_GPU_FREQ_MIN,
  ARG_GPU_FREQ_MAX,

  ARG_MEM_FREQ,
  ARG_MEM_FREQ_CUR,
  ARG_MEM_FREQ_MIN,
  ARG_MEM_FREQ_MAX,

  ARG_MTR_FREQ,
  ARG_MTR_FREQ_CUR,
  ARG_MTR_FREQ_MIN,
  ARG_MTR_FREQ_MAX,

  ARG_PERF_LEVEL,
  ARG_PERF_LEVEL_CUR,
  ARG_PERF_LEVEL_MIN,
  ARG_PERF_LEVEL_MAX,
  ARG_PERF_MODE,

  ARG_GPU_UTIL,
  ARG_MEM_BW_UTIL,
  ARG_VIDEO_UTIL,
  ARG_PCIE_UTIL,

  ARG_MEM,
  ARG_MEM_USED,
  ARG_MEM_FREE,
  ARG_MEM_AVAIL,
  ARG_MEM_MAX,
  ARG_MEM_TOTAL,
  ARG_MEM_UTIL,
  ARG_MEM_PERC,

  ARG_FAN_SPEED,
  ARG_FAN_LEVEL,

  ARG_IMAGEQUALITY,
  ARG_MODEL_NAME,
  ARG_DRIVER_VERSION,

  ARG_UNKNOWN
} ARG_ID;

// Nvidia query targets
const int translate_nvidia_target[] = {
    NV_CTRL_TARGET_TYPE_X_SCREEN,
    NV_CTRL_TARGET_TYPE_GPU,
    NV_CTRL_TARGET_TYPE_FRAMELOCK,
    NV_CTRL_TARGET_TYPE_VCSC,
    NV_CTRL_TARGET_TYPE_GVI,
    NV_CTRL_TARGET_TYPE_COOLER,
    NV_CTRL_TARGET_TYPE_THERMAL_SENSOR,
    NV_CTRL_TARGET_TYPE_3D_VISION_PRO_TRANSCEIVER,
    NV_CTRL_TARGET_TYPE_DISPLAY,
};

// Enum for nvidia query targets
typedef enum _TARGET_ID {
  TARGET_SCREEN,
  TARGET_GPU,
  TARGET_FRAMELOCK,
  TARGET_VCSC,
  TARGET_GVI,
  TARGET_COOLER,
  TARGET_THERMAL,
  TARGET_3DVISION,
  TARGET_DISPLAY
} TARGET_ID;

// Nvidia query attributes
const int translate_nvidia_attribute[] = {
    NV_CTRL_GPU_CORE_TEMPERATURE,
    NV_CTRL_GPU_CORE_THRESHOLD,
    NV_CTRL_AMBIENT_TEMPERATURE,

    NV_CTRL_GPU_CURRENT_CLOCK_FREQS,
    NV_CTRL_GPU_CURRENT_CLOCK_FREQS,
    NV_CTRL_STRING_PERFORMANCE_MODES,
    NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS,
    NV_CTRL_GPU_POWER_MIZER_MODE,

    NV_CTRL_STRING_GPU_UTILIZATION,

    NV_CTRL_USED_DEDICATED_GPU_MEMORY,
    0,
    NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY,  // NOTE: NV_CTRL_TOTAL_GPU_MEMORY would
                                         // be better, but returns KB instead of
                                         // MB
    0,

    NV_CTRL_THERMAL_COOLER_SPEED,
    NV_CTRL_THERMAL_COOLER_LEVEL,

    NV_CTRL_GPU_CURRENT_PERFORMANCE_LEVEL,
    NV_CTRL_IMAGE_SETTINGS,

    NV_CTRL_STRING_PRODUCT_NAME,
    NV_CTRL_STRING_NVIDIA_DRIVER_VERSION,
};

// Enum for nvidia query attributes
typedef enum _ATTR_ID {
  ATTR_GPU_TEMP,
  ATTR_GPU_TEMP_THRESHOLD,
  ATTR_AMBIENT_TEMP,

  ATTR_GPU_FREQ,
  ATTR_MEM_FREQ,
  ATTR_PERFMODES_STRING,
  ATTR_FREQS_STRING,
  ATTR_PERF_MODE,

  ATTR_UTILS_STRING,

  ATTR_MEM_USED,
  ATTR_MEM_FREE,
  ATTR_MEM_TOTAL,
  ATTR_MEM_UTIL,

  ATTR_FAN_SPEED,
  ATTR_FAN_LEVEL,

  ATTR_PERF_LEVEL,
  ATTR_IMAGE_QUALITY,

  ATTR_MODEL_NAME,
  ATTR_DRIVER_VERSION,
} ATTR_ID;

// Enum for query type
typedef enum _QUERY_ID {
  QUERY_VALUE,
  QUERY_STRING,
  QUERY_STRING_VALUE,
  QUERY_SPECIAL
} QUERY_ID;

// Enum for string token search mode
typedef enum _SEARCH_ID {
  SEARCH_FIRST,
  SEARCH_LAST,
  SEARCH_MIN,
  SEARCH_MAX
} SEARCH_ID;

// Translate special_type into command string
const char *translate_nvidia_special_type[] = {
    "nvidia",       // NONSPECIAL
    "",             // HORIZONTAL_LINE
    "",             // STIPPLED_HR
    "nvidiabar",    // BAR
    "",             // FG
    "",             // BG
    "",             // OUTLINE
    "",             // ALIGNR
    "",             // ALIGNC
    "nvidiagague",  // GAUGE
    "nvidiagraph",  // GRAPH
    "",             // OFFSET
    "",             // VOFFSET
    "",             // VOFFSET_FONT
    "",             // SAVE_FONT_HEIGHT
    "",             // GET_FONT_HEIGHT
    "",             // SAVE_COORDINATES
    "",             // SAVE_POSITION
    "",             // GET_SAVE_COORDINATES
    "",             // FONT
    "",             // GOTO
    ""              // TAB
};

// Global struct to keep track of queries
class nvidia_s {
 public:
  nvidia_s()
      : command(0),
        arg(0),
        query(QUERY_VALUE),
        target(TARGET_SCREEN),
        attribute(ATTR_GPU_TEMP),
        token(0),
        search(SEARCH_FIRST),
        target_id(0),
        is_percentage(false) {}
  const char *command;
  const char *arg;
  QUERY_ID query;
  TARGET_ID target;
  ATTR_ID attribute;
  char *token;
  SEARCH_ID search;
  //  added new field for GPU id
  int target_id;
  bool is_percentage;
};

// Cache by value
struct nvidia_c_value {
  int memtotal = -1;
  int gputempthreshold = -1;
};

// Cache by string
struct nvidia_c_string {
  int nvclockmin = -1;
  int nvclockmax = -1;
  int memclockmin = -1;
  int memclockmax = -1;
  int memTransferRatemin = -1;
  int memTransferRatemax = -1;
  int perfmin = -1;
  int perfmax = -1;
};

// Maximum number of GPU connected:
// For cache default value: choosed a model of direct access to array instead of
// list for speed improvement value based on the incoming quad Naples tech
// having 256 PCIe lanes available
const int MAXNUMGPU = 64;

namespace {

// Deleter for nv display to use with std::unique_ptr
void close_nvdisplay(Display *dp) { XCloseDisplay(dp); }

using unique_display_t = std::unique_ptr<Display, decltype(&close_nvdisplay)>;

class nvidia_display_setting
    : public conky::simple_config_setting<std::string> {
  typedef conky::simple_config_setting<std::string> Base;

 protected:
  virtual void lua_setter(lua::state &l, bool init);
  virtual void cleanup(lua::state &l);

  std::string nvdisplay;

 public:
  nvidia_display_setting() : Base("nvidia_display", std::string(), false) {}
  virtual unique_display_t get_nvdisplay();
};

void nvidia_display_setting::lua_setter(lua::state &l, bool init) {
  lua::stack_sentry s(l, -2);

  Base::lua_setter(l, init);

  nvdisplay = do_convert(l, -1).first;

  ++s;
}  // namespace

unique_display_t nvidia_display_setting::get_nvdisplay() {
  if (!nvdisplay.empty()) {
    unique_display_t nvd(XOpenDisplay(nvdisplay.c_str()), &close_nvdisplay);
    if (!nvd) {
      NORM_ERR(nullptr, NULL, "can't open nvidia display: %s",
               XDisplayName(nvdisplay.c_str()));
    }
    return nvd;
  }
  return unique_display_t(nullptr, &close_nvdisplay);
}  // namespace

void nvidia_display_setting::cleanup(lua::state &l) {
  lua::stack_sentry s(l, -1);

  l.pop();
}

nvidia_display_setting nvidia_display;
}  // namespace

// Evaluate module parameters and prepare query
int set_nvidia_query(struct text_object *obj, const char *arg,
                     text_node_t special_type) {
  nvidia_s *nvs;
  int aid;
  int ilen;

  // Initialize global struct
  nvs = new nvidia_s();
  obj->data.opaque = nvs;

  // Added new parameter parsing GPU_ID as 0,1,2,..
  // if no GPU_ID parameter then default to 0
  nvs->target_id = 0;
  char *strbuf = strdup(arg);
  char *p = strrchr(strbuf, ' ');
  if (p && *(p + 1)) {
    nvs->target_id = atoi(p + 1);
    if ((nvs->target_id > 0) || !strcmp(p + 1, "0")) {
      ilen = strlen(strbuf);
      ilen = ilen - strlen(p);
      strbuf[ilen] = 0;
      arg = strbuf;
    }
  }

  // If the value is negative it is set to 0
  if (nvs->target_id < 0) nvs->target_id = 0;

  // Extract arguments for nvidiabar, etc, and run set_nvidia_query
  switch (special_type) {
    case text_node_t::BAR:
      arg = scan_bar(obj, arg, 100);
      break;
    case text_node_t::GRAPH: {
      auto [buf, skip] = scan_command(arg);
      scan_graph(obj, arg + skip, 100, FALSE);
      arg = buf;
    } break;
    case text_node_t::GAUGE:
      arg = scan_gauge(obj, arg, 100);
      break;
    default:
      break;
  }

  // Return error if no argument
  // (sometimes scan_graph gets excited and eats the whole string!
  if (!arg) {
    free_and_zero(strbuf);
    return 1;
  }

  // Translate parameter to id
  for (aid = 0; aid < ARG_UNKNOWN; aid++) {
    if (strcmp(arg, translate_module_argument[aid]) == 0) break;
  }

  // free the string buffer after arg is not anymore needed
  if (strbuf != nullptr) free_and_zero(strbuf);

  // Save pointers to the arg and command strings for debugging and printing
  nvs->arg = translate_module_argument[aid];
  nvs->command = translate_nvidia_special_type[*special_type];

  // Evaluate parameter
  switch (aid) {
    case ARG_TEMP:  // GPU temperature
    case ARG_GPU_TEMP:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_GPU_TEMP;
      break;
    case ARG_THRESHOLD:  // GPU temperature threshold
    case ARG_GPU_TEMP_THRESHOLD:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_GPU_TEMP_THRESHOLD;
      break;
    case ARG_AMBIENT:  // Ambient temperature
    case ARG_AMBIENT_TEMP:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_AMBIENT_TEMP;
      break;

    case ARG_GPU_FREQ:  // Current GPU clock
    case ARG_GPU_FREQ_CUR:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_GPU_FREQ;
      break;
    case ARG_GPU_FREQ_MIN:  // Minimum GPU clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"nvclockmin";
      nvs->search = SEARCH_MIN;
      break;
    case ARG_GPU_FREQ_MAX:  // Maximum GPU clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"nvclockmax";
      nvs->search = SEARCH_MAX;
      break;

    case ARG_MEM_FREQ:  // Current memory clock
    case ARG_MEM_FREQ_CUR:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MEM_FREQ;
      break;
    case ARG_MEM_FREQ_MIN:  // Minimum memory clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"memclockmin";
      nvs->search = SEARCH_MIN;
      break;
    case ARG_MEM_FREQ_MAX:  // Maximum memory clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"memclockmax";
      nvs->search = SEARCH_MAX;
      break;

    case ARG_MTR_FREQ:  // Current memory transfer rate clock
    case ARG_MTR_FREQ_CUR:
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_FREQS_STRING;
      nvs->token = (char *)"memTransferRate";
      nvs->search = SEARCH_FIRST;
      break;
    case ARG_MTR_FREQ_MIN:  // Minimum memory transfer rate clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"memTransferRatemin";
      nvs->search = SEARCH_MIN;
      break;
    case ARG_MTR_FREQ_MAX:  // Maximum memory transfer rate clock
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"memTransferRatemax";
      nvs->search = SEARCH_MAX;
      break;

    case ARG_PERF_LEVEL:  // Current performance level
    case ARG_PERF_LEVEL_CUR:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERF_LEVEL;
      break;
    case ARG_PERF_LEVEL_MIN:  // Lowest performance level
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"perf";
      nvs->search = SEARCH_MIN;
      break;
    case ARG_PERF_LEVEL_MAX:  // Highest performance level
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERFMODES_STRING;
      nvs->token = (char *)"perf";
      nvs->search = SEARCH_MAX;
      break;
    case ARG_PERF_MODE:  // Performance mode
      nvs->query = QUERY_SPECIAL;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_PERF_MODE;
      break;

    case ARG_GPU_UTIL:  // GPU utilization %
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_UTILS_STRING;
      nvs->token = (char *)"graphics";
      nvs->search = SEARCH_FIRST;
      nvs->is_percentage = true;
      break;
    case ARG_MEM_BW_UTIL:  // Memory bandwidth utilization %
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_UTILS_STRING;
      nvs->token = (char *)"memory";
      nvs->search = SEARCH_FIRST;
      nvs->is_percentage = true;
      break;
    case ARG_VIDEO_UTIL:  // Video engine utilization %
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_UTILS_STRING;
      nvs->token = (char *)"video";
      nvs->search = SEARCH_FIRST;
      nvs->is_percentage = true;
      break;
    case ARG_PCIE_UTIL:  // PCIe bandwidth utilization %
      nvs->query = QUERY_STRING_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_UTILS_STRING;
      nvs->token = (char *)"PCIe";
      nvs->search = SEARCH_FIRST;
      nvs->is_percentage = true;
      break;

    case ARG_MEM:  // Amount of used memory
    case ARG_MEM_USED:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MEM_USED;
      break;
    case ARG_MEM_FREE:  // Amount of free memory
    case ARG_MEM_AVAIL:
      nvs->query = QUERY_SPECIAL;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MEM_FREE;
      break;
    case ARG_MEM_MAX:  // Total amount of memory
    case ARG_MEM_TOTAL:
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MEM_TOTAL;
      break;
    case ARG_MEM_UTIL:  // Memory utilization %
    case ARG_MEM_PERC:
      nvs->query = QUERY_SPECIAL;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MEM_UTIL;
      nvs->is_percentage = true;
      break;

    case ARG_FAN_SPEED:  // Fan speed
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_COOLER;
      nvs->attribute = ATTR_FAN_SPEED;
      break;
    case ARG_FAN_LEVEL:  // Fan level %
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_COOLER;
      nvs->attribute = ATTR_FAN_LEVEL;
      nvs->is_percentage = true;
      break;

    case ARG_IMAGEQUALITY:  // Image quality
      nvs->query = QUERY_VALUE;
      nvs->target = TARGET_SCREEN;
      nvs->attribute = ATTR_IMAGE_QUALITY;
      break;

    case ARG_MODEL_NAME:
      nvs->query = QUERY_STRING;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_MODEL_NAME;
      break;

    case ARG_DRIVER_VERSION:
      nvs->query = QUERY_STRING;
      nvs->target = TARGET_GPU;
      nvs->attribute = ATTR_DRIVER_VERSION;
      break;

    default:  // Unknown/invalid argument
      // Error printed by core.cc
      return 1;
  }
  return 0;
}

// Return the amount of targets present or raise error)
static inline int get_nvidia_target_count(Display *dpy, TARGET_ID tid) {
  int num_tgts;
  if (!XNVCTRLQueryTargetCount(dpy, translate_nvidia_target[tid], &num_tgts)) {
    num_tgts = -1;
  }

  if (num_tgts < 1 && tid == TARGET_GPU) {
    // Print error and exit if there's no NVIDIA's GPU
    NORM_ERR(nullptr, NULL,
             "%s:"
             "\n          Trying to query Nvidia target failed (using the "
             "proprietary drivers)."
             "\n          Are you sure they are installed correctly and a "
             "Nvidia GPU is in use?"
             "\n          (display: %d,Nvidia target_count: %d)",
             __func__, dpy, num_tgts);
  }

  return num_tgts;
}

static int cache_nvidia_value(TARGET_ID tid, ATTR_ID aid, Display *dpy,
                              int *value, int gid, const char *arg) {
  static nvidia_c_value ac_value[MAXNUMGPU];

  if (aid == ATTR_MEM_TOTAL) {
    if (ac_value[gid].memtotal < 0) {
      if (!dpy || !XNVCTRLQueryTargetAttribute(
                      dpy, translate_nvidia_target[tid], gid, 0,
                      translate_nvidia_attribute[aid], value)) {
        NORM_ERR(
            "%s: Something went wrong running nvidia query (arg: %s tid: %d, "
            "aid: %d)",
            __func__, arg, tid, aid);
        return -1;
      }
      ac_value[gid].memtotal = *value;
    } else {
      *value = ac_value[gid].memtotal;
    }
  } else if (aid == ATTR_GPU_TEMP_THRESHOLD) {
    if (ac_value[gid].gputempthreshold < 0) {
      if (!dpy || !XNVCTRLQueryTargetAttribute(
                      dpy, translate_nvidia_target[tid], gid, 0,
                      translate_nvidia_attribute[aid], value)) {
        NORM_ERR(
            "%s: Something went wrong running nvidia query (arg: %s, tid: "
            "%d, aid: %d)",
            __func__, arg, tid, aid);
        return -1;
      }
      ac_value[gid].gputempthreshold = *value;
    } else {
      *value = ac_value[gid].gputempthreshold;
    }
  }

  return 0;
}

// Retrieve attribute value via nvidia interface
static int get_nvidia_value(TARGET_ID tid, ATTR_ID aid, int gid,
                            const char *arg) {
  auto nvdpy = nvidia_display.get_nvdisplay();
  Display *dpy = nvdpy ? nvdpy.get() : display;
  int value;

  // Check if the aid is cacheable
  if (aid == ATTR_MEM_TOTAL || aid == ATTR_GPU_TEMP_THRESHOLD) {
    if (cache_nvidia_value(tid, aid, dpy, &value, gid, arg)) { return -1; }
    // If not, then query it
  } else {
    if (!dpy ||
        !XNVCTRLQueryTargetAttribute(dpy, translate_nvidia_target[tid], gid, 0,
                                     translate_nvidia_attribute[aid], &value)) {
      NORM_ERR(
          "%s: Something went wrong running nvidia query (arg: %s, tid: %d, "
          "aid: %d)",
          __func__, arg, tid, aid);
      return -1;
    }
  }

  // Unpack clock values (see NVCtrl.h for details)
  if (aid == ATTR_GPU_FREQ) return value >> 16;
  if (aid == ATTR_MEM_FREQ) return value & 0xFFFF;

  // Return value
  return value;
}

// Retrieve attribute string via nvidia interface
static char *get_nvidia_string(TARGET_ID tid, ATTR_ID aid, int gid,
                               const char *arg) {
  auto nvdpy = nvidia_display.get_nvdisplay();
  Display *dpy = nvdpy ? nvdpy.get() : display;
  char *str;

  // Query nvidia interface
  if (!dpy || !XNVCTRLQueryTargetStringAttribute(
                  dpy, translate_nvidia_target[tid], gid, 0,
                  translate_nvidia_attribute[aid], &str)) {
    NORM_ERR(
        "%s: Something went wrong running nvidia string query (arg, tid: %d, "
        "aid: "
        "%d, GPU %d)",
        __func__, arg, tid, aid, gid);
    return nullptr;
  }
  return str;
}

void cache_nvidia_string_value_update(nvidia_c_string *ac_string, char *token,
                                      SEARCH_ID search, int *value, int gid) {
  if (strcmp(token, (char *)"nvclockmin") == 0 &&
      ac_string[gid].nvclockmin < 0) {
    ac_string[gid].nvclockmin = *value;
  } else if (strcmp(token, (char *)"nvclockmax") == 0 &&
             ac_string[gid].nvclockmax < 0) {
    ac_string[gid].nvclockmax = *value;
  } else if (strcmp(token, (char *)"memclockmin") == 0 &&
             ac_string[gid].memclockmin < 0) {
    ac_string[gid].memclockmin = *value;
  } else if (strcmp(token, (char *)"memclockmax") == 0 &&
             ac_string[gid].memclockmax < 0) {
    ac_string[gid].memclockmax = *value;
  } else if (strcmp(token, (char *)"memTransferRatemin") == 0 &&
             ac_string[gid].memTransferRatemin < 0) {
    ac_string[gid].memTransferRatemin = *value;
  } else if (strcmp(token, (char *)"memTransferRatemax") == 0 &&
             ac_string[gid].memTransferRatemax < 0) {
    ac_string[gid].memTransferRatemax = *value;

  } else if (strcmp(token, (char *)"perf") == 0) {
    if (search == SEARCH_MIN &&
        ac_string[gid].perfmin < 0) {
      ac_string[gid].perfmin = *value;
    } else if (search == SEARCH_MAX &&
               ac_string[gid].perfmax < 0) {
      ac_string[gid].perfmax = *value;
    }
  }
}

void cache_nvidia_string_value_noupdate(nvidia_c_string *ac_string, char *token,
                                        SEARCH_ID search, int *value, int gid) {
  if (strcmp(token, (char *)"nvclockmin") == 0) {
    *value = ac_string[gid].nvclockmin;
  } else if (strcmp(token, (char *)"nvclockmax") == 0) {
    *value = ac_string[gid].nvclockmax;
  } else if (strcmp(token, (char *)"memclockmin") == 0) {
    *value = ac_string[gid].memclockmin;
  } else if (strcmp(token, (char *)"memclockmax") == 0) {
    *value = ac_string[gid].memclockmax;
  } else if (strcmp(token, (char *)"memTransferRatemin") == 0) {
    *value = ac_string[gid].memTransferRatemin;
  } else if (strcmp(token, (char *)"memTransferRatemax") == 0) {
    *value = ac_string[gid].memTransferRatemax;

  } else if (strcmp(token, (char *)"perf") == 0) {
    if (search == SEARCH_MIN) {
      *value = ac_string[gid].perfmin;
    } else if (search == SEARCH_MAX) {
      *value = ac_string[gid].perfmax;
    }
  }
}

static int cache_nvidia_string_value(TARGET_ID tid, ATTR_ID aid, char *token,
                                     SEARCH_ID search, int *value, int update,
                                     int gid) {
  static nvidia_c_string ac_string[MAXNUMGPU];
  (void)tid;
  (void)aid;

  if (update) {
    cache_nvidia_string_value_update(ac_string, token, search, value, gid);
  } else {
    cache_nvidia_string_value_noupdate(ac_string, token, search, value, gid);
  }

  return 0;
}

// Retrieve token value from nvidia string
static int get_nvidia_string_value(TARGET_ID tid, ATTR_ID aid, char *token,
                                   SEARCH_ID search, int gid, const char *arg) {
  char *str;
  char *kvp;
  char *key;
  char *val;
  char *saveptr1;
  char *saveptr2;
  int temp;
  int value = -1;

  // Checks if the value is cacheable and is already loaded
  cache_nvidia_string_value(tid, aid, token, search, &value, 0, gid);
  if (value != -1) { return value; }

  // Get string via nvidia interface
  str = get_nvidia_string(tid, aid, gid, arg);

  // Split string into 'key=value' substrings, split substring
  // into key and value, from value, check if token was found,
  // convert value to int, evaluate value according to specified
  // token search mode
  kvp = strtok_r(str, NV_KVPAIR_SEPARATORS, &saveptr1);
  while (kvp) {
    key = strtok_r(kvp, NV_KEYVAL_SEPARATORS, &saveptr2);
    val = strtok_r(nullptr, NV_KEYVAL_SEPARATORS, &saveptr2);
    if (key && val && (strcmp(token, key) == 0)) {
      temp = (int)strtol(val, nullptr, 0);
      if (search == SEARCH_FIRST) {
        value = temp;
        break;
      } else if (search == SEARCH_LAST) {
        value = temp;
      } else if (search == SEARCH_MIN) {
        if ((value == -1) || (temp < value)) value = temp;
      } else if (search == SEARCH_MAX) {
        if (temp > value) value = temp;
      } else {
        value = -1;
        break;
      }
    }
    kvp = strtok_r(nullptr, NV_KVPAIR_SEPARATORS, &saveptr1);
  }

  // This call updated the cache for the cacheable values
  cache_nvidia_string_value(tid, aid, token, search, &value, 1, gid);

  // Free string, return value
  free_and_zero(str);
  return value;
}

bool validate_target_id(Display *dpy, int target_id, ATTR_ID attribute) {
  // num_GPU and num_COOLER calculated only once based on the physical target
  static int num_GPU = get_nvidia_target_count(dpy, TARGET_GPU) - 1;
  static int num_COOLER = get_nvidia_target_count(dpy, TARGET_COOLER) - 1;

  if (target_id < 0) return false;
  switch (attribute) {
    case ATTR_FAN_LEVEL:
    case ATTR_FAN_SPEED:
      if (target_id > num_COOLER) return false;
      break;
    default:
      if (target_id > num_GPU) return false;
      break;
  }
  return true;
}

// Perform query and print result
void print_nvidia_value(struct text_object *obj, char *p,
                        unsigned int p_max_size) {
  nvidia_s *nvs = static_cast<nvidia_s *>(obj->data.opaque);
  int value;
  int temp1;
  int temp2;
  int result;
  char *str;
  int event_base;
  int error_base;

  auto nvdpy = nvidia_display.get_nvdisplay();
  Display *dpy = nvdpy ? nvdpy.get() : display;

  if (!dpy) {
    NORM_ERR("%s: no display set (try setting nvidia_display)", __func__);
    return;
  }

  if (!XNVCTRLQueryExtension(dpy, &event_base, &error_base)) {
    NORM_ERR("%s: NV-CONTROL X extension not present", __func__);
    return;
  }

  // Assume failure
  value = -1;
  str = nullptr;

  // Perform query if the query exists and isnt stupid
  if (nvs != nullptr &&
      validate_target_id(dpy, nvs->target_id, nvs->attribute)) {
    // Execute switch by query type
    switch (nvs->query) {
      case QUERY_VALUE:
        value = get_nvidia_value(nvs->target, nvs->attribute, nvs->target_id,
                                 nvs->arg);
        break;
      case QUERY_STRING:
        str = get_nvidia_string(nvs->target, nvs->attribute, nvs->target_id,
                                nvs->arg);
        break;
      case QUERY_STRING_VALUE:
        value = get_nvidia_string_value(nvs->target, nvs->attribute, nvs->token,
                                        nvs->search, nvs->target_id, nvs->arg);
        break;
      case QUERY_SPECIAL:
        switch (nvs->attribute) {
          case ATTR_PERF_MODE:
            temp1 = get_nvidia_value(nvs->target, nvs->attribute,
                                     nvs->target_id, nvs->arg);
            switch (temp1) {
              case NV_CTRL_GPU_POWER_MIZER_MODE_ADAPTIVE:
                result = asprintf(&str, "Adaptive");
                break;
              case NV_CTRL_GPU_POWER_MIZER_MODE_PREFER_MAXIMUM_PERFORMANCE:
                result = asprintf(&str, "Max. Perf.");
                break;
              case NV_CTRL_GPU_POWER_MIZER_MODE_AUTO:
                result = asprintf(&str, "Auto");
                break;
              case NV_CTRL_GPU_POWER_MIZER_MODE_PREFER_CONSISTENT_PERFORMANCE:
                result = asprintf(&str, "Consistent");
                break;
              default:
                result = asprintf(&str, "Unknown (%d)", value);
                break;
            }
            if (result < 0) { str = nullptr; }
            break;
          case ATTR_MEM_FREE:
            temp1 = get_nvidia_value(nvs->target, ATTR_MEM_USED, nvs->target_id,
                                     nvs->arg);
            temp2 = get_nvidia_value(nvs->target, ATTR_MEM_TOTAL,
                                     nvs->target_id, nvs->arg);
            value = temp2 - temp1;
            break;
          case ATTR_MEM_UTIL:
            temp1 = get_nvidia_value(nvs->target, ATTR_MEM_USED, nvs->target_id,
                                     nvs->arg);
            temp2 = get_nvidia_value(nvs->target, ATTR_MEM_TOTAL,
                                     nvs->target_id, nvs->arg);
            value = ((float)temp1 * 100 / (float)temp2) + 0.5;
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }

  // Print result
  if (value != -1) {
    if (nvs->is_percentage) {
      percent_print(p, p_max_size, value);
    } else {
      snprintf(p, p_max_size, "%d", value);
    }
  } else if (str != nullptr) {
    snprintf(p, p_max_size, "%s", str);
    free_and_zero(str);
  } else {
    snprintf(p, p_max_size, "%s", "N/A");
  }
}

double get_nvidia_barval(struct text_object *obj) {
  nvidia_s *nvs = static_cast<nvidia_s *>(obj->data.opaque);
  int temp1;
  int temp2;
  double value;
  int event_base;
  int error_base;

  auto nvdpy = nvidia_display.get_nvdisplay();
  Display *dpy = nvdpy ? nvdpy.get() : display;

  if (!dpy) {
    NORM_ERR("%s: no display set (try setting nvidia_display)", __func__);
    return 0;
  }

  if (!XNVCTRLQueryExtension(dpy, &event_base, &error_base)) {
    NORM_ERR("%s: NV-CONTROL X extension not present", __func__);
    return 0;
  }

  // Assume failure
  value = 0;

  // Convert query_result to a percentage using ((val-min)÷(max-min)×100)+0.5 if
  // needed.
  if (nvs != nullptr &&
      validate_target_id(dpy, nvs->target_id, nvs->attribute)) {
    switch (nvs->attribute) {
      case ATTR_UTILS_STRING:  // one of the percentage utils (gpuutil,
                               // membwutil, videoutil and pcieutil)
        value =
            get_nvidia_string_value(nvs->target, ATTR_UTILS_STRING, nvs->token,
                                    nvs->search, nvs->target_id, nvs->arg);
        break;
      case ATTR_MEM_UTIL:  // memutil
      case ATTR_MEM_USED:
        temp1 = get_nvidia_value(nvs->target, ATTR_MEM_USED, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_value(nvs->target, ATTR_MEM_TOTAL, nvs->target_id,
                                 nvs->arg);
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_MEM_FREE:  // memfree
        temp1 = get_nvidia_value(nvs->target, ATTR_MEM_USED, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_value(nvs->target, ATTR_MEM_TOTAL, nvs->target_id,
                                 nvs->arg);
        value = temp2 - temp1;
        break;
      case ATTR_FAN_SPEED:  // fanspeed: Warn user we are using fanlevel
        NORM_ERR(
            "%s: invalid argument specified: '%s' (using 'fanlevel' instead).",
            nvs->command, nvs->arg);
        /* falls through */
      case ATTR_FAN_LEVEL:  // fanlevel
        value = get_nvidia_value(nvs->target, ATTR_FAN_LEVEL, nvs->target_id,
                                 nvs->arg);
        break;
      case ATTR_GPU_TEMP:  // gputemp (calculate out of gputempthreshold)
        temp1 = get_nvidia_value(nvs->target, ATTR_GPU_TEMP, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_value(nvs->target, ATTR_GPU_TEMP_THRESHOLD,
                                 nvs->target_id, nvs->arg);
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_AMBIENT_TEMP:  // ambienttemp (calculate out of gputempthreshold
                               // for consistency)
        temp1 = get_nvidia_value(nvs->target, ATTR_AMBIENT_TEMP, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_value(nvs->target, ATTR_GPU_TEMP_THRESHOLD,
                                 nvs->target_id, nvs->arg);
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_GPU_FREQ:  // gpufreq (calculate out of gpufreqmax)
        temp1 = get_nvidia_value(nvs->target, ATTR_GPU_FREQ, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_string_value(nvs->target, ATTR_PERFMODES_STRING,
                                        (char *)"nvclockmax", SEARCH_MAX,
                                        nvs->target_id, nvs->arg);
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_MEM_FREQ:  // memfreq (calculate out of memfreqmax)
        temp1 = get_nvidia_value(nvs->target, ATTR_MEM_FREQ, nvs->target_id,
                                 nvs->arg);
        temp2 = get_nvidia_string_value(nvs->target, ATTR_PERFMODES_STRING,
                                        (char *)"memclockmax", SEARCH_MAX,
                                        nvs->target_id, nvs->arg);
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_FREQS_STRING:  // mtrfreq (calculate out of memfreqmax)
        if (strcmp(nvs->token, "memTransferRate") != 0) {
          // Just in case error for silly devs
          CRIT_ERR(
              "%s: attribute is 'ATTR_FREQS_STRING' but token is not "
              "\"memTransferRate\" (arg: '%s')",
              nvs->command, nvs->arg);
          return 0;
        }
        temp1 =
            get_nvidia_string_value(nvs->target, ATTR_FREQS_STRING, nvs->token,
                                    SEARCH_MAX, nvs->target_id, nvs->arg);
        temp2 = get_nvidia_string_value(nvs->target, ATTR_PERFMODES_STRING,
                                        (char *)"memTransferRatemax",
                                        SEARCH_MAX, nvs->target_id, nvs->arg);
        if (temp1 > temp2) temp1 = temp2;  // extra safe here
        value = ((float)temp1 * 100 / (float)temp2) + 0.5;
        break;
      case ATTR_IMAGE_QUALITY:  // imagequality
        value = get_nvidia_value(nvs->target, ATTR_IMAGE_QUALITY,
                                 nvs->target_id, nvs->arg);
        break;

      default:  // Throw error if unsupported args are used
        CRIT_ERR("%s: invalid argument specified: '%s'", nvs->command,
                 nvs->arg);
    }
  }

  // Return the percentage
  return value;
}

// Cleanup
void free_nvidia(struct text_object *obj) {
  nvidia_s *nvs = static_cast<nvidia_s *>(obj->data.opaque);
  delete nvs;
  obj->data.opaque = nullptr;
}
