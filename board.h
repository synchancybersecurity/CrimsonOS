#ifndef _CRIMSON_BOARD_H
#define _CRIMSON_BOARD_H

#include <crimson/types.h>

/* Board detection */
const char* board_get_name(void);
uint32_t board_get_ram_size(void);
uint32_t board_get_cpu_cores(void);

/* Platform-specific functions */
void board_init_early(void);
void board_init(void);

/* Device tree parsing */
void dtb_parse(void* dtb_addr);

#endif
