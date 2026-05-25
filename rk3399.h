/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
/*
 * Crimson OS - Rockchip RK3399 Hardware Definitions
 * Board: PinePhone Pro
 */

#ifndef _RK3399_H_
#define _RK3399_H_

#include <stdint.h>

#define RK3399_CRU_BASE         0xFF750000UL
#define RK3399_PMUCRU_BASE      0xFF760000UL
#define RK3399_GRF_BASE        0xFF770000UL
#define RK3399_PMU_GRF_BASE    0xFF320000UL

#define RK3399_GPIO0_BASE      0xFF720000UL
#define RK3399_GPIO1_BASE      0xFF730000UL
#define RK3399_GPIO2_BASE      0xFF780000UL
#define RK3399_GPIO3_BASE      0xFF790000UL
#define RK3399_GPIO4_BASE      0xFF7A0000UL

#define RK3399_I2C0_BASE       0xFF3C0000UL
#define RK3399_I2C1_BASE       0xFF110000UL
#define RK3399_I2C2_BASE       0xFF120000UL
#define RK3399_I2C3_BASE       0xFF130000UL
#define RK3399_I2C4_BASE       0xFF140000UL
#define RK3399_I2C5_BASE       0xFF150000UL
#define RK3399_I2C6_BASE       0xFF160000UL
#define RK3399_I2C7_BASE       0xFF170000UL
#define RK3399_I2C8_BASE       0xFF180000UL

#define RK3399_UART0_BASE      0xFF180000UL
#define RK3399_UART1_BASE      0xFF190000UL
#define RK3399_UART2_BASE      0xFF1A0000UL
#define RK3399_UART3_BASE      0xFF1B0000UL
#define RK3399_UART4_BASE      0xFF1C0000UL

#define RK3399_DSI0_BASE       0xFF960000UL
#define RK3399_DSI1_BASE       0xFF968000UL
#define RK3399_DPHY0_BASE      0xFF964000UL
#define RK3399_DPHY1_BASE      0xFF96C000UL

#define RK3399_VOPB_BASE       0xFF900000UL
#define RK3399_VOPL_BASE       0xFF8F0000UL
#define RK3399_EDP_BASE        0xFF970000UL
#define RK3399_HDMI_BASE       0xFF940000UL

#define RK3399_SDMMC_BASE      0xFE320000UL
#define RK3399_SDIO0_BASE      0xFE330000UL
#define RK3399_SDIO1_BASE      0xFE340000UL

#define RK3399_USB3_OTG_BASE   0xFE800000UL
#define RK3399_USB2_HOST0_BASE 0xFE380000UL
#define RK3399_USB2_HOST1_BASE 0xFE3A0000UL

#define RK3399_PCIE_BASE       0xF8000000UL

#define RK3399_GICD_BASE       0xFEE00000UL
#define RK3399_GICR_BASE       0xFEF00000UL

#define CRU_PLL_CON(id, n)     (RK3399_CRU_BASE + 0x000 + (id)*0x20 + (n)*4)
#define CRU_CLKSEL_CON(n)      (RK3399_CRU_BASE + 0x100 + (n)*4)
#define CRU_CLKGATE_CON(n)     (RK3399_CRU_BASE + 0x300 + (n)*4)
#define CRU_SOFTRST_CON(n)     (RK3399_CRU_BASE + 0x400 + (n)*4)

#define PMUCRU_PLL_CON(id, n)  (RK3399_PMUCRU_BASE + 0x000 + (id)*0x20 + (n)*4)
#define PMUCRU_CLKSEL_CON(n)   (RK3399_PMUCRU_BASE + 0x080 + (n)*4)
#define PMUCRU_CLKGATE_CON(n)  (RK3399_PMUCRU_BASE + 0x100 + (n)*4)
#define PMUCRU_SOFTRST_CON(n)  (RK3399_PMUCRU_BASE + 0x110 + (n)*4)

#define RK3399_PLL_APLLL       0
#define RK3399_PLL_APLLB       1
#define RK3399_PLL_DPLL        2
#define RK3399_PLL_CPLL        3
#define RK3399_PLL_GPLL        4
#define RK3399_PLL_NPLL        5
#define RK3399_PLL_VPLL        6
#define RK3399_PLL_PPLL        7

#define GRF_GPIO2A_IOMUX       (RK3399_GRF_BASE + 0xE000)
#define GRF_GPIO2B_IOMUX       (RK3399_GRF_BASE + 0xE004)
#define GRF_GPIO2C_IOMUX       (RK3399_GRF_BASE + 0xE008)
#define GRF_GPIO2D_IOMUX       (RK3399_GRF_BASE + 0xE00C)
#define GRF_GPIO3A_IOMUX       (RK3399_GRF_BASE + 0xE010)
#define GRF_GPIO3B_IOMUX       (RK3399_GRF_BASE + 0xE014)
#define GRF_GPIO3C_IOMUX       (RK3399_GRF_BASE + 0xE018)
#define GRF_GPIO3D_IOMUX       (RK3399_GRF_BASE + 0xE01C)
#define GRF_GPIO4A_IOMUX       (RK3399_GRF_BASE + 0xE020)
#define GRF_GPIO4B_IOMUX       (RK3399_GRF_BASE + 0xE024)
#define GRF_GPIO4C_IOMUX       (RK3399_GRF_BASE + 0xE028)
#define GRF_GPIO4D_IOMUX       (RK3399_GRF_BASE + 0xE02C)

#define PMU_GRF_GPIO0A_IOMUX   (RK3399_PMU_GRF_BASE + 0x0000)
#define PMU_GRF_GPIO0B_IOMUX   (RK3399_PMU_GRF_BASE + 0x0004)
#define PMU_GRF_GPIO0C_IOMUX   (RK3399_PMU_GRF_BASE + 0x0008)
#define PMU_GRF_GPIO0D_IOMUX   (RK3399_PMU_GRF_BASE + 0x000C)
#define PMU_GRF_GPIO1A_IOMUX   (RK3399_PMU_GRF_BASE + 0x0010)
#define PMU_GRF_GPIO1B_IOMUX   (RK3399_PMU_GRF_BASE + 0x0014)
#define PMU_GRF_GPIO1C_IOMUX   (RK3399_PMU_GRF_BASE + 0x0018)
#define PMU_GRF_GPIO1D_IOMUX   (RK3399_PMU_GRF_BASE + 0x001C)

#define GPIO_SWPORTA_DR(base)  ((base) + 0x0000)
#define GPIO_SWPORTA_DDR(base) ((base) + 0x0004)
#define GPIO_INTEN(base)       ((base) + 0x0030)
#define GPIO_INTMASK(base)     ((base) + 0x0034)
#define GPIO_INTTYPE_LEVEL(base) ((base) + 0x0038)
#define GPIO_INT_POLARITY(base) ((base) + 0x003C)
#define GPIO_INT_STATUS(base)  ((base) + 0x0040)
#define GPIO_INT_RAWSTATUS(base) ((base) + 0x0044)
#define GPIO_DEBOUNCE(base)    ((base) + 0x0048)
#define GPIO_PORTS_EOI(base)   ((base) + 0x004C)
#define GPIO_EXT_PORTA(base)   ((base) + 0x0050)
#define GPIO_LS_SYNC(base)     ((base) + 0x0060)

#define I2C_CON(base)          ((base) + 0x0000)
#define I2C_TAR(base)          ((base) + 0x0004)
#define I2C_SAR(base)          ((base) + 0x0008)
#define I2C_DATA_CMD(base)     ((base) + 0x0010)
#define I2C_SS_SCL_HCNT(base)  ((base) + 0x0014)
#define I2C_SS_SCL_LCNT(base)  ((base) + 0x0018)
#define I2C_FS_SCL_HCNT(base)  ((base) + 0x001C)
#define I2C_FS_SCL_LCNT(base)  ((base) + 0x0020)
#define I2C_HS_SCL_HCNT(base)  ((base) + 0x0024)
#define I2C_HS_SCL_LCNT(base)  ((base) + 0x0028)
#define I2C_INTR_STAT(base)    ((base) + 0x002C)
#define I2C_INTR_MASK(base)    ((base) + 0x0030)
#define I2C_RAW_INTR_STAT(base) ((base) + 0x0034)
#define I2C_RX_TL(base)        ((base) + 0x0038)
#define I2C_TX_TL(base)        ((base) + 0x003C)
#define I2C_CLR_INTR(base)     ((base) + 0x0040)
#define I2C_CLR_RX_UNDER(base) ((base) + 0x0044)
#define I2C_CLR_RX_OVER(base)  ((base) + 0x0048)
#define I2C_CLR_TX_OVER(base)  ((base) + 0x004C)
#define I2C_CLR_RD_REQ(base)   ((base) + 0x0050)
#define I2C_CLR_TX_ABRT(base)  ((base) + 0x0054)
#define I2C_CLR_RX_DONE(base)  ((base) + 0x0058)
#define I2C_CLR_ACTIVITY(base) ((base) + 0x005C)
#define I2C_CLR_STOP_DET(base) ((base) + 0x0060)
#define I2C_CLR_START_DET(base) ((base) + 0x0064)
#define I2C_CLR_GEN_CALL(base) ((base) + 0x0068)
#define I2C_ENABLE(base)       ((base) + 0x006C)
#define I2C_STATUS(base)       ((base) + 0x0070)
#define I2C_TXFLR(base)        ((base) + 0x0074)
#define I2C_RXFLR(base)        ((base) + 0x0078)
#define I2C_SDA_HOLD(base)     ((base) + 0x007C)
#define I2C_TX_ABRT_SOURCE(base) ((base) + 0x0080)
#define I2C_DMA_CR(base)       ((base) + 0x0088)
#define I2C_DMA_TDLR(base)     ((base) + 0x008C)
#define I2C_DMA_RDLR(base)     ((base) + 0x0090)
#define I2C_COMP_PARAM1(base)  ((base) + 0x00F4)
#define I2C_COMP_VERSION(base) ((base) + 0x00F8)
#define I2C_COMP_TYPE(base)    ((base) + 0x00FC)

#define I2C_CON_MASTER_MODE    (1U << 0)
#define I2C_CON_SPEED_STD      (1U << 1)
#define I2C_CON_SPEED_FAST     (2U << 1)
#define I2C_CON_SPEED_HIGH     (3U << 1)
#define I2C_CON_10BITADDR_SLAVE (1U << 3)
#define I2C_CON_10BITADDR_MASTER (1U << 4)
#define I2C_CON_RESTART_EN     (1U << 5)
#define I2C_CON_SLAVE_DISABLE  (1U << 6)
#define I2C_CON_TX_EMPTY_CTRL  (1U << 8)
#define I2C_CON_RX_FIFO_FULL_HLD_CTRL (1U << 9)

#define I2C_DATA_CMD_READ      (1U << 8)
#define I2C_DATA_CMD_STOP      (1U << 9)
#define I2C_DATA_CMD_RESTART   (1U << 10)

#define I2C_ENABLE_ENABLE      (1U << 0)
#define I2C_ENABLE_ABORT       (1U << 1)

#define I2C_STATUS_ACTIVITY    (1U << 0)
#define I2C_STATUS_TFNF        (1U << 1)
#define I2C_STATUS_TFE         (1U << 2)
#define I2C_STATUS_RFNE        (1U << 3)
#define I2C_STATUS_RFF         (1U << 4)
#define I2C_STATUS_MST_ACTIVITY (1U << 5)
#define I2C_STATUS_SLV_ACTIVITY (1U << 6)

#define I2C_INTR_RX_UNDER      (1U << 0)
#define I2C_INTR_RX_OVER       (1U << 1)
#define I2C_INTR_RX_FULL       (1U << 2)
#define I2C_INTR_TX_OVER       (1U << 3)
#define I2C_INTR_TX_EMPTY      (1U << 4)
#define I2C_INTR_RD_REQ        (1U << 5)
#define I2C_INTR_TX_ABRT       (1U << 6)
#define I2C_INTR_RX_DONE       (1U << 7)
#define I2C_INTR_ACTIVITY      (1U << 8)
#define I2C_INTR_STOP_DET      (1U << 9)
#define I2C_INTR_START_DET     (1U << 10)
#define I2C_INTR_GEN_CALL      (1U << 11)
#define I2C_INTR_RESTART_DET   (1U << 12)
#define I2C_INTR_SCL_STUCK_AT_LOW (1U << 13)

#define UART_RBR(base)         ((base) + 0x0000)
#define UART_THR(base)         ((base) + 0x0000)
#define UART_DLL(base)         ((base) + 0x0000)
#define UART_DLH(base)         ((base) + 0x0004)
#define UART_IER(base)         ((base) + 0x0004)
#define UART_IIR(base)         ((base) + 0x0008)
#define UART_FCR(base)         ((base) + 0x0008)
#define UART_LCR(base)         ((base) + 0x000C)
#define UART_MCR(base)         ((base) + 0x0010)
#define UART_LSR(base)         ((base) + 0x0014)
#define UART_MSR(base)         ((base) + 0x0018)
#define UART_SCR(base)         ((base) + 0x001C)

#define UART_LSR_DR            (1U << 0)
#define UART_LSR_OE            (1U << 1)
#define UART_LSR_PE            (1U << 2)
#define UART_LSR_FE            (1U << 3)
#define UART_LSR_BI            (1U << 4)
#define UART_LSR_THRE          (1U << 5)
#define UART_LSR_TEMT          (1U << 6)
#define UART_LSR_RFE           (1U << 7)

#define UART_LCR_WLS_8         0x03
#define UART_LCR_STOP_2        (1U << 2)
#define UART_LCR_PARITY_EN     (1U << 3)
#define UART_LCR_PARITY_ODD    (1U << 4)
#define UART_LCR_DLAB          (1U << 7)

#define DSI_VERSION(base)      ((base) + 0x0000)
#define DSI_PWR_UP(base)       ((base) + 0x0004)
#define DSI_CLKMGR_CFG(base)   ((base) + 0x0008)
#define DSI_DPI_VCID(base)     ((base) + 0x000C)
#define DSI_DPI_COLOR_CODING(base) ((base) + 0x0010)
#define DSI_DPI_CFG_POL(base)  ((base) + 0x0014)
#define DSI_DPI_LP_CMD_TIM(base) ((base) + 0x0018)
#define DSI_PCKHDL_CFG(base)   ((base) + 0x002C)
#define DSI_GEN_VCID(base)     ((base) + 0x0030)
#define DSI_MODE_CFG(base)     ((base) + 0x0034)
#define DSI_VID_MODE_CFG(base) ((base) + 0x0038)
#define DSI_VID_PKT_SIZE(base) ((base) + 0x003C)
#define DSI_VID_NUM_CHUNKS(base) ((base) + 0x0040)
#define DSI_VID_NULL_SIZE(base) ((base) + 0x0044)
#define DSI_VID_HSA_TIME(base) ((base) + 0x0048)
#define DSI_VID_HBP_TIME(base) ((base) + 0x004C)
#define DSI_VID_HLINE_TIME(base) ((base) + 0x0050)
#define DSI_VID_VSA_LINES(base) ((base) + 0x0054)
#define DSI_VID_VBP_LINES(base) ((base) + 0x0058)
#define DSI_VID_VFP_LINES(base) ((base) + 0x005C)
#define DSI_VID_VACTIVE_LINES(base) ((base) + 0x0060)
#define DSI_EDPI_CMD_SIZE(base) ((base) + 0x0064)
#define DSI_CMD_MODE_CFG(base) ((base) + 0x0068)
#define DSI_GEN_HDR(base)      ((base) + 0x006C)
#define DSI_GEN_PLD_DATA(base) ((base) + 0x0070)
#define DSI_CMD_PKT_STATUS(base) ((base) + 0x0074)
#define DSI_TO_CNT_CFG(base)   ((base) + 0x0078)
#define DSI_HS_RD_TO_CNT(base) ((base) + 0x007C)
#define DSI_LP_RD_TO_CNT(base) ((base) + 0x0080)
#define DSI_HS_WR_TO_CNT(base) ((base) + 0x0084)
#define DSI_LP_WR_TO_CNT(base) ((base) + 0x0088)
#define DSI_BTA_TO_CNT(base)   ((base) + 0x008C)
#define DSI_SDF_3D(base)       ((base) + 0x0090)
#define DSI_LPCLK_CTRL(base)   ((base) + 0x0094)
#define DSI_PHY_TMR_LPCLK(base) ((base) + 0x0098)
#define DSI_PHY_TMR_CFG(base)  ((base) + 0x009C)
#define DSI_PHY_RSTZ(base)     ((base) + 0x00A0)
#define DSI_PHY_IF_CFG(base)   ((base) + 0x00A4)
#define DSI_PHY_ULPS_CTRL(base) ((base) + 0x00A8)
#define DSI_PHY_TX_TRIGGERS(base) ((base) + 0x00AC)
#define DSI_PHY_STATUS(base)   ((base) + 0x00B0)
#define DSI_PHY_TST_CTRL0(base) ((base) + 0x00B4)
#define DSI_PHY_TST_CTRL1(base) ((base) + 0x00B8)
#define DSI_INT_ST0(base)      ((base) + 0x00BC)
#define DSI_INT_ST1(base)      ((base) + 0x00C0)
#define DSI_INT_MSK0(base)     ((base) + 0x00C4)
#define DSI_INT_MSK1(base)     ((base) + 0x00C8)

#define VOP_REG_CFG_DONE(base) ((base) + 0x0000)
#define VOP_VERSION(base)      ((base) + 0x0004)
#define VOP_SYS_CTRL(base)     ((base) + 0x0008)
#define VOP_DSP_CTRL0(base)    ((base) + 0x0010)
#define VOP_DSP_CTRL1(base)    ((base) + 0x0014)
#define VOP_DSP_BG(base)       ((base) + 0x0018)
#define VOP_MCU_CTRL(base)     ((base) + 0x001C)
#define VOP_INTR_CTRL0(base)   ((base) + 0x0020)
#define VOP_INTR_CTRL1(base)   ((base) + 0x0024)
#define VOP_WIN0_CTRL0(base)   ((base) + 0x0030)
#define VOP_WIN0_CTRL1(base)   ((base) + 0x0034)
#define VOP_WIN0_COLOR_KEY(base) ((base) + 0x0038)
#define VOP_WIN0_VIR(base)     ((base) + 0x003C)
#define VOP_WIN0_YRGB_MST(base) ((base) + 0x0040)
#define VOP_WIN0_CBR_MST(base) ((base) + 0x0044)
#define VOP_WIN0_ACT_INFO(base) ((base) + 0x0048)
#define VOP_WIN0_DSP_INFO(base) ((base) + 0x004C)
#define VOP_WIN0_DSP_ST(base)  ((base) + 0x0050)
#define VOP_DSP_HTOTAL_HS_END(base) ((base) + 0x01A0)
#define VOP_DSP_HACT_ST_END(base) ((base) + 0x01A4)
#define VOP_DSP_VTOTAL_VS_END(base) ((base) + 0x01A8)
#define VOP_DSP_VACT_ST_END(base) ((base) + 0x01AC)
#define VOP_DSP_VS_ST_END_F1(base) ((base) + 0x01B0)
#define VOP_DSP_VACT_ST_END_F1(base) ((base) + 0x01B4)
#define VOP_PWM_CTRL(base)     ((base) + 0x01C0)
#define VOP_PWM_PERIOD(base)   ((base) + 0x01C4)
#define VOP_PWM_DUTY(base)     ((base) + 0x01C8)
#define VOP_PWM_CNT(base)      ((base) + 0x01CC)

#define GICD_CTLR              (RK3399_GICD_BASE + 0x0000)
#define GICD_TYPER             (RK3399_GICD_BASE + 0x0004)
#define GICD_IIDR              (RK3399_GICD_BASE + 0x0008)
#define GICD_STATUSR           (RK3399_GICD_BASE + 0x0010)
#define GICD_SETSPI_NSR        (RK3399_GICD_BASE + 0x0040)
#define GICD_CLRSPI_NSR        (RK3399_GICD_BASE + 0x0048)
#define GICD_SEIR              (RK3399_GICD_BASE + 0x0068)
#define GICD_IGROUPR(n)        (RK3399_GICD_BASE + 0x0080 + (n)*4)
#define GICD_ISENABLER(n)      (RK3399_GICD_BASE + 0x0100 + (n)*4)
#define GICD_ICENABLER(n)      (RK3399_GICD_BASE + 0x0180 + (n)*4)
#define GICD_ISPENDR(n)        (RK3399_GICD_BASE + 0x0200 + (n)*4)
#define GICD_ICPENDR(n)        (RK3399_GICD_BASE + 0x0280 + (n)*4)
#define GICD_ISACTIVER(n)      (RK3399_GICD_BASE + 0x0300 + (n)*4)
#define GICD_ICACTIVER(n)      (RK3399_GICD_BASE + 0x0380 + (n)*4)
#define GICD_IPRIORITYR(n)     (RK3399_GICD_BASE + 0x0400 + (n)*4)
#define GICD_ITARGETSR(n)      (RK3399_GICD_BASE + 0x0800 + (n)*4)
#define GICD_ICFGR(n)          (RK3399_GICD_BASE + 0x0C00 + (n)*4)
#define GICD_IGRPMODR(n)       (RK3399_GICD_BASE + 0x0D00 + (n)*4)
#define GICD_NSACR(n)          (RK3399_GICD_BASE + 0x0E00 + (n)*4)
#define GICD_SGIR              (RK3399_GICD_BASE + 0x0F00)
#define GICD_CPENDSGIR(n)      (RK3399_GICD_BASE + 0x0F10 + (n)*4)
#define GICD_SPENDSGIR(n)      (RK3399_GICD_BASE + 0x0F20 + (n)*4)
#define GICD_IROUTER(n)        (RK3399_GICD_BASE + 0x6000 + (n)*8)

#define GICR_CTLR(cpu)         (RK3399_GICR_BASE + (cpu)*0x20000 + 0x0000)
#define GICR_IIDR(cpu)         (RK3399_GICR_BASE + (cpu)*0x20000 + 0x0004)
#define GICR_TYPER(cpu)        (RK3399_GICR_BASE + (cpu)*0x20000 + 0x0008)
#define GICR_STATUSR(cpu)      (RK3399_GICR_BASE + (cpu)*0x20000 + 0x0010)
#define GICR_WAKER(cpu)        (RK3399_GICR_BASE + (cpu)*0x20000 + 0x0014)
#define GICR_IGROUPR0(cpu)     (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0080)
#define GICR_ISENABLER0(cpu)   (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0100)
#define GICR_ICENABLER0(cpu)   (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0180)
#define GICR_ISPENDR0(cpu)     (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0200)
#define GICR_ICPENDR0(cpu)     (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0280)
#define GICR_ISACTIVER0(cpu)   (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0300)
#define GICR_ICACTIVER0(cpu)   (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0380)
#define GICR_IPRIORITYR(cpu, n) (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0400 + (n)*4)
#define GICR_ICFGR0(cpu)       (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0C00)
#define GICR_ICFGR1(cpu)       (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0C04)
#define GICR_IGRPMODR0(cpu)    (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0D00)
#define GICR_NSACR0(cpu)       (RK3399_GICR_BASE + (cpu)*0x20000 + 0x10000 + 0x0E00)

#define reg_read32(addr)       (*(volatile uint32_t*)(addr))
#define reg_write32(addr, val) (*(volatile uint32_t*)(addr) = (val))
#define reg_set32(addr, mask)  reg_write32((addr), reg_read32(addr) | (mask))
#define reg_clr32(addr, mask)  reg_write32((addr), reg_read32(addr) & ~(mask))
#define reg_clrset32(addr, clear, set) reg_write32((addr), (reg_read32(addr) & ~(clear)) | (set))

#define PP_PRO_TOUCH_I2C_BASE      RK3399_I2C5_BASE
#define PP_PRO_MODEM_UART_BASE     RK3399_UART2_BASE
#define PP_PRO_WIFI_SDIO_BASE      RK3399_SDIO0_BASE
#define PP_PRO_DSI_BASE            RK3399_DSI0_BASE
#define PP_PRO_DPHY_BASE           RK3399_DPHY0_BASE
#define PP_PRO_VOP_BASE            RK3399_VOPB_BASE
#define PP_PRO_PMIC_I2C_BASE       RK3399_I2C0_BASE

#define PP_PRO_MODEM_PWRKEY_BIT    (1U << 22)
#define PP_PRO_MODEM_RESET_BIT     (1U << 26)
#define PP_PRO_MODEM_STATUS_BIT    (1U << 29)

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

#define RK808_I2C_ADDR           0x1B
#define RK808_CHIP_NAME          0x00
#define RK808_CHIP_VER           0x01
#define RK808_BUCK1_ON_VSEL      0x2F
#define RK808_BUCK2_ON_VSEL      0x33
#define RK808_BUCK4_ON_VSEL      0x3B
#define RK808_LDO1_ON_VSEL       0x43
#define RK808_LDO2_ON_VSEL       0x45
#define RK808_LDO3_ON_VSEL       0x47
#define RK808_LDO4_ON_VSEL       0x49
#define RK808_LDO5_ON_VSEL       0x4B
#define RK808_LDO6_ON_VSEL       0x4D
#define RK808_LDO7_ON_VSEL       0x4F
#define RK808_LDO8_ON_VSEL       0x51
#define RK808_DEVCTRL            0x01
#define RK808_INT_STS_MSK1       0x4C
#define RK808_INT_STS_MSK2       0x4D
#define RK808_DCDC_EN_REG        0x23
#define RK808_LDO_EN_REG         0x24
#define RK808_SLEEP_SET_OFF_REG1 0x25
#define RK808_SLEEP_SET
