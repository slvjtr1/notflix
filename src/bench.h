#pragma once
#include <stdbool.h>
void bench_begin(void);
void bench_tick(void);
void bench_frame(void);
void bench_cancel(void);
bool bench_active(void);
