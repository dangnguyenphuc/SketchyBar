#include "stats.h"
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <mach/mach.h>
#include <sys/sysctl.h>

static mach_port_t g_host = MACH_PORT_NULL;
static host_cpu_load_info_data_t g_previous_load;
static bool g_has_previous_load = false;

static mach_port_t stats_host() {
  // mach_host_self() hands out a new port right on every call.
  if (g_host == MACH_PORT_NULL) g_host = mach_host_self();
  return g_host;
}

static int stats_battery() {
  CFTypeRef info = IOPSCopyPowerSourcesInfo();
  if (!info) return -1;

  int percent = -1;
  CFArrayRef sources = IOPSCopyPowerSourcesList(info);
  for (CFIndex i = 0; sources && i < CFArrayGetCount(sources); i++) {
    CFDictionaryRef description
      = IOPSGetPowerSourceDescription(info,
                                      CFArrayGetValueAtIndex(sources, i));
    if (!description) continue;

    CFStringRef type = CFDictionaryGetValue(description, CFSTR(kIOPSTypeKey));
    if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType))) continue;

    CFNumberRef current_ref
      = CFDictionaryGetValue(description, CFSTR(kIOPSCurrentCapacityKey));
    CFNumberRef max_ref
      = CFDictionaryGetValue(description, CFSTR(kIOPSMaxCapacityKey));
    int current = 0, max = 0;
    if (current_ref && max_ref
        && CFNumberGetValue(current_ref, kCFNumberIntType, &current)
        && CFNumberGetValue(max_ref, kCFNumberIntType, &max)
        && max > 0                                                 ) {
      percent = (int)round(100.0 * current / max);
    }
    break;
  }

  if (sources) CFRelease(sources);
  CFRelease(info);
  return percent;
}

static int stats_cpu() {
  host_cpu_load_info_data_t load;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(stats_host(),
                      HOST_CPU_LOAD_INFO,
                      (host_info_t)&load,
                      &count             ) != KERN_SUCCESS) {
    return -1;
  }

  int percent = -1;
  if (g_has_previous_load) {
    uint64_t busy = 0, total = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) {
      uint64_t ticks = load.cpu_ticks[i] - g_previous_load.cpu_ticks[i];
      total += ticks;
      if (i != CPU_STATE_IDLE) busy += ticks;
    }
    if (total > 0) percent = (int)round(100.0 * busy / total);
  }

  g_previous_load = load;
  g_has_previous_load = true;
  return percent;
}

// Same notion of "used" as Activity Monitor's Memory Used: app memory (minus
// purgeable caches) + wired + compressed.
static int stats_ram() {
  vm_statistics64_data_t vm;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(stats_host(),
                        HOST_VM_INFO64,
                        (host_info64_t)&vm,
                        &count             ) != KERN_SUCCESS) {
    return -1;
  }

  vm_size_t page_size = 0;
  uint64_t memory = 0;
  size_t length = sizeof(memory);
  if (host_page_size(stats_host(), &page_size) != KERN_SUCCESS
      || sysctlbyname("hw.memsize", &memory, &length, NULL, 0) != 0
      || memory == 0                                               ) {
    return -1;
  }

  uint64_t app = vm.internal_page_count > vm.purgeable_count
                 ? vm.internal_page_count - vm.purgeable_count
                 : 0;
  uint64_t used = (app + vm.wire_count + vm.compressor_page_count)
                  * (uint64_t)page_size;
  return (int)min(round(100.0 * used / memory), 100.0);
}

void system_stats_sample(struct system_stats* stats) {
  stats->battery = stats_battery();
  stats->cpu = stats_cpu();
  stats->ram = stats_ram();
}

void system_stats_sample_load(struct system_stats* stats) {
  stats->cpu = stats_cpu();
  stats->ram = stats_ram();
}

void system_stats_sample_battery(struct system_stats* stats) {
  stats->battery = stats_battery();
}

void system_stats_history_push(struct system_stats_history* history,
                               const struct system_stats* stats     ) {
  if (stats->cpu < 0) return;

  if (history->count == SYSTEM_STATS_HISTORY) {
    memmove(history->cpu, history->cpu + 1, SYSTEM_STATS_HISTORY - 1);
    memmove(history->ram, history->ram + 1, SYSTEM_STATS_HISTORY - 1);
    history->count--;
  }
  history->cpu[history->count] = stats->cpu;
  history->ram[history->count] = max(stats->ram, 0);
  history->count++;
}
