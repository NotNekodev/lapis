#ifndef _SMP_H
#define _SMP_H 1

#include <stdint.h>

void smp_prepare(void);
void smp_start_aps(void);
uint32_t smp_get_cpu_count(void);
void init_bsp_cpu(void);

#endif // _SMP_H
