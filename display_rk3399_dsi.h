/*
 * Crimson OS - Rockchip RK3399 MIPI DSI Display Pipeline
 * Board: PinePhone Pro
 * Panel: Xingbangda XBD599 (5.99", 1440x720, MIPI DSI, 4 lanes)
 * SoC: Rockchip RK3399 (NOT Allwinner A64)
 */

#ifndef _DISPLAY_RK3399_DSI_H_
#define _DISPLAY_RK3399_DSI_H_

#include <stdint.h>
#include "rk3399.h"

#define PANEL_WIDTH              1440
#define PANEL_HEIGHT             720
#define PANEL_LANES              4
#define PANEL_REFRESH_HZ         60
#define PANEL_LANE_RATE_MBPS     500

#define PANEL_HSA                8
#define PANEL_HBP                20
#define PANEL_HFP                20
#define PANEL_HACTIVE_BC         1080
#define PANEL_HLINE_TIME         (PANEL_HSA + PANEL_HBP + PANEL_HACTIVE_BC + PANEL_HFP)

#define PANEL_VSA                4
#define PANEL_VBP                12
#define PANEL_VFP                18

#define DCS_NOP                  0x00
#define DCS_SWRESET              0x01
#define DCS_SLPOUT               0x11
#define DCS_DISPON               0x29
#define DCS_DISPOFF              0x28
#define DCS_SLPIN                0x10
#define DCS_CASET                0x2A
#define DCS_PASET                0x2B

int  display_rk3399_dsi_init(void);
void display_rk3399_dsi_shutdown(void);
int  display_rk3399_vop_init(void);
void display_rk3399_vop_enable(void);
void display_rk3399_vop_disable(void);
int  display_rk3399_dphy_init(void);
void display_rk3399_dsi_send_cmd(uint8_t type, const uint8_t* payload, uint8_t len);
void display_rk3399_xbd599_init_sequence(void);
void display_rk3399_cru_enable_display_clocks(void);
void display_rk3399_grf_set_dsi_pinmux(void);

#endif /* _DISPLAY_RK3399_DSI_H_ */
