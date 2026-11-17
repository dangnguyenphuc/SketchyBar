#pragma once
#include "misc/helpers.h"

// Battery charge, CPU load and memory use, in percent (-1: not available).
// CPU load is the difference to the previous sample.
struct system_stats {
  int battery;
  int cpu;
  int ram;
};

// Recent CPU/RAM values for the island's System tab, oldest first (one entry
// per sample).
#define SYSTEM_STATS_HISTORY 60

struct system_stats_history {
  int count;
  signed char cpu[SYSTEM_STATS_HISTORY];
  signed char ram[SYSTEM_STATS_HISTORY];
};

void system_stats_sample(struct system_stats* stats);
// Only CPU and RAM (cheap: two kernel calls), or only the battery.
void system_stats_sample_load(struct system_stats* stats);
void system_stats_sample_battery(struct system_stats* stats);
// Skips samples without a CPU value (the first one after a reset).
void system_stats_history_push(struct system_stats_history* history,
                               const struct system_stats* stats     );
