#pragma once

#include <aarch64/intrinsic.h>
#include <driver/clock.h>
#include <driver/gpio.h>
#include <driver/interrupt.h>
#include <driver/mbox.h>
#include <driver/disk.h>
#include <driver/uart.h>
#include <kernel/init.h>
#include <lib/printk.h>
#include <lib/buf.h>
#include <lib/defines.h>
#include <lib/sem.h>
#include <lib/spinlock.h>
#include <lib/string.h>

/* EMMC registers */
#define EMMC_ARG2 ((volatile u32 *)(MMIO_BASE + 0x00300000))
#define EMMC_BLKSIZECNT ((volatile u32 *)(MMIO_BASE + 0x00300004))
#define EMMC_ARG1 ((volatile u32 *)(MMIO_BASE + 0x00300008))
#define EMMC_CMDTM ((volatile u32 *)(MMIO_BASE + 0x0030000C))
#define EMMC_RESP0 ((volatile u32 *)(MMIO_BASE + 0x00300010))
#define EMMC_RESP1 ((volatile u32 *)(MMIO_BASE + 0x00300014))
#define EMMC_RESP2 ((volatile u32 *)(MMIO_BASE + 0x00300018))
#define EMMC_RESP3 ((volatile u32 *)(MMIO_BASE + 0x0030001C))
#define EMMC_DATA ((volatile u32 *)(MMIO_BASE + 0x00300020))
#define EMMC_STATUS ((volatile u32 *)(MMIO_BASE + 0x00300024))
#define EMMC_CONTROL0 ((volatile u32 *)(MMIO_BASE + 0x00300028))
#define EMMC_CONTROL1 ((volatile u32 *)(MMIO_BASE + 0x0030002C))
#define EMMC_INTERRUPT ((volatile u32 *)(MMIO_BASE + 0x00300030))
#define EMMC_IRPT_MASK ((volatile u32 *)(MMIO_BASE + 0x00300034))
#define EMMC_IRPT_EN ((volatile u32 *)(MMIO_BASE + 0x00300038))
#define EMMC_CONTROL2 ((volatile u32 *)(MMIO_BASE + 0x0030003C))
#define EMMC_SLOTISR_VER ((volatile u32 *)(MMIO_BASE + 0x003000fc))

/* EMMC command flags */
#define CMD_TYPE_NORMAL 0x00000000
#define CMD_TYPE_SUSPEND 0x00400000
#define CMD_TYPE_RESUME 0x00800000
#define CMD_TYPE_ABORT 0x00c00000
#define CMD_IS_DATA 0x00200000
#define CMD_IXCHK_EN 0x00100000
#define CMD_CRCCHK_EN 0x00080000
#define CMD_RSPNS_NO 0x00000000
#define CMD_RSPNS_136 0x00010000
#define CMD_RSPNS_48 0x00020000
#define CMD_RSPNS_48B 0x00030000
#define TM_MULTI_BLOCK 0x00000020
#define TM_DAT_DIR_HC 0x00000000
#define TM_DAT_DIR_CH 0x00000010
#define TM_AUTO_CMD23 0x00000008
#define TM_AUTO_CMD12 0x00000004
#define TM_BLKCNT_EN 0x00000002
#define TM_MULTI_DATA (CMD_IS_DATA | TM_MULTI_BLOCK | TM_BLKCNT_EN)

/* INTERRUPT register settings */
#define INT_AUTO_ERROR 0x01000000
#define INT_DATA_END_ERR 0x00400000
#define INT_DATA_CRC_ERR 0x00200000
#define INT_DATA_TIMEOUT 0x00100000
#define INT_INDEX_ERROR 0x00080000
#define INT_END_ERROR 0x00040000
#define INT_CRC_ERROR 0x00020000
#define INT_CMD_TIMEOUT 0x00010000
#define INT_ERR 0x00008000
#define INT_ENDBOOT 0x00004000
#define INT_BOOTACK 0x00002000
#define INT_RETUNE 0x00001000
#define INT_CARD 0x00000100
#define INT_READ_RDY 0x00000020
#define INT_WRITE_RDY 0x00000010
#define INT_BLOCK_GAP 0x00000004
#define INT_DATA_DONE 0x00000002
#define INT_CMD_DONE 0x00000001
#define INT_ERROR_MASK                                                         \
    (INT_CRC_ERROR | INT_END_ERROR | INT_INDEX_ERROR | INT_DATA_TIMEOUT |      \
     INT_DATA_CRC_ERR | INT_DATA_END_ERR | INT_ERR | INT_AUTO_ERROR)
#define INT_ALL_MASK                                                           \
    (INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY |             \
     INT_ERROR_MASK)

/* CONTROL register settings */
#define C0_SPI_MODE_EN 0x00100000
#define C0_HCTL_HS_EN 0x00000004
#define C0_HCTL_DWITDH 0x00000002

#define C1_SRST_DATA 0x04000000
#define C1_SRST_CMD 0x02000000
#define C1_SRST_HC 0x01000000
#define C1_TOUNIT_DIS 0x000f0000
#define C1_TOUNIT_MAX 0x000e0000
#define C1_CLK_GENSEL 0x00000020
#define C1_CLK_EN 0x00000004
#define C1_CLK_STABLE 0x00000002
#define C1_CLK_INTLEN 0x00000001

#define FREQ_SETUP 400000    // 400 Khz
#define FREQ_NORMAL 25000000 // 25 Mhz

/* CONTROL2 values */
#define C2_VDD_18 0x00080000
#define C2_UHSMODE 0x00070000
#define C2_UHS_SDR12 0x00000000
#define C2_UHS_SDR25 0x00010000
#define C2_UHS_SDR50 0x00020000
#define C2_UHS_SDR104 0x00030000
#define C2_UHS_DDR50 0x00040000

/* SLOTISR_VER values */
#define HOST_SPEC_NUM 0x00ff0000
#define HOST_SPEC_NUM_SHIFT 16
#define HOST_SPEC_V3 2
#define HOST_SPEC_V2 1
#define HOST_SPEC_V1 0

/* STATUS register settings */
#define SR_DAT_LEVEL1 0x1e000000
#define SR_CMD_LEVEL 0x01000000
#define SR_DAT_LEVEL0 0x00f00000
#define SR_DAT3 0x00800000
#define SR_DAT2 0x00400000
#define SR_DAT1 0x00200000
#define SR_DAT0 0x00100000
#define SR_WRITE_PROT 0x00080000      // From SDHC spec v2, BCM says reserved
#define SR_READ_AVAILABLE 0x00000800  // ???? undocumented
#define SR_WRITE_AVAILABLE 0x00000400 // ???? undocumented
#define SR_READ_TRANSFER 0x00000200
#define SR_WRITE_TRANSFER 0x00000100
#define SR_DAT_ACTIVE 0x00000004
#define SR_DAT_INHIBIT 0x00000002
#define SR_CMD_INHIBIT 0x00000001

/*
 * Arguments for specific commands.
 *
 * TODO: What's the correct voltage window for the RPi SD interface?
 * 2.7v-3.6v (given by 0x00ff8000) or something narrower?
 * TODO: For now, don't offer to switch voltage.
 */
#define ACMD41_HCS 0x40000000
#define ACMD41_SDXC_POWER 0x10000000
#define ACMD41_S18R 0x01000000
#define ACMD41_VOLTAGE 0x00ff8000
#define ACMD41_ARG_HC                                                          \
    (ACMD41_HCS | ACMD41_SDXC_POWER | ACMD41_VOLTAGE | ACMD41_S18R)
#define ACMD41_ARG_SC (ACMD41_VOLTAGE | ACMD41_S18R)

/* R1 (Status) values */
#define ST_OUT_OF_RANGE 0x80000000      // 31   E
#define ST_ADDRESS_ERROR 0x40000000     // 30   E
#define ST_BLOCK_LEN_ERROR 0x20000000   // 29   E
#define ST_ERASE_SEQ_ERROR 0x10000000   // 28   E
#define ST_ERASE_PARAM_ERROR 0x08000000 // 27   E
#define ST_WP_VIOLATION 0x04000000      // 26   E
#define ST_CARD_IS_LOCKED 0x02000000    // 25   E
#define ST_LOCK_UNLOCK_FAIL 0x01000000  // 24   E
#define ST_COM_CRC_ERROR 0x00800000     // 23   E
#define ST_ILLEGAL_COMMAND 0x00400000   // 22   E
#define ST_CARD_ECC_FAILED 0x00200000   // 21   E
#define ST_CC_ERROR 0x00100000          // 20   E
#define ST_ERROR 0x00080000             // 19   E
#define ST_CSD_OVERWRITE 0x00010000     // 16   E
#define ST_WP_ERASE_SKIP 0x00008000     // 15   E
#define ST_CARD_ECC_DISABLED 0x00004000 // 14   E
#define ST_ERASE_RESET 0x00002000       // 13   E
#define ST_CARD_STATE 0x00001e00        // 12:9
#define ST_READY_FOR_DATA 0x00000100    // 8
#define ST_APP_CMD 0x00000020           // 5
#define ST_AKE_SEQ_ERROR 0x00000004     // 3    E

#define R1_CARD_STATE_SHIFT 9
#define R1_ERRORS_MASK 0xfff9c004 // All above bits which indicate errors.

/* R3 (ACMD41 APP_SEND_OP_COND) */
#define R3_COMPLETE 0x80000000
#define R3_CCS 0x40000000
#define R3_S18A 0x01000000

/* R6 (CMD3 SEND_REL_ADDR) */
#define R6_RCA_MASK 0xffff0000
#define R6_ERR_MASK 0x0000e000
#define R6_STATE_MASK 0x00001e00

/* Card state values as they appear in the status register. */
#define CS_IDLE 0  // 0x00000000
#define CS_READY 1 // 0x00000200
#define CS_IDENT 2 // 0x00000400
#define CS_STBY 3  // 0x00000600
#define CS_TRAN 4  // 0x00000800
#define CS_DATA 5  // 0x00000a00
#define CS_RCV 6   // 0x00000c00
#define CS_PRG 7   // 0x00000e00
#define CS_DIS 8   // 0x00001000

/*
 * Response types.
 *
 * Note that on the PI, the index and CRC are dropped, leaving 32 bits in RESP0.
 */
#define RESP_NO 0 // No response
#define RESP_R1 1 // 48  RESP0    contains card status
#define RESP_R1b                                                               \
    11 // 48  RESP0    contains card status, data line indicates busy
#define RESP_R2I                                                               \
    2 // 136 RESP0..3 contains 128 bit CID shifted down by 8 bits as no CRC
#define RESP_R2S                                                               \
    12 // 136 RESP0..3 contains 128 bit CSD shifted down by 8 bits as no CRC
#define RESP_R3 3 // 48  RESP0    contains OCR register
#define RESP_R6 6 // 48  RESP0    contains RCA and status bits 23,22,19,12:0
#define RESP_R7 7 // 48  RESP0    contains voltage acceptance and check pattern

#define RCA_NO 1
#define RCA_YES 2

typedef struct EMMCCommand {
    const char *name;
    u32 code;
    u8 resp;
    u8 rca;
    int delay;
} EMMCCommand;

/* Command indexes in the command table. */
#define IX_GO_IDLE_STATE 0
#define IX_ALL_SEND_CID 1
#define IX_SEND_REL_ADDR 2
#define IX_SET_DSR 3
#define IX_SWITCH_FUNC 4
#define IX_CARD_SELECT 5
#define IX_SEND_IF_COND 6
#define IX_SEND_CSD 7
#define IX_SEND_CID 8
#define IX_VOLTAGE_SWITCH 9
#define IX_STOP_TRANS 10
#define IX_SEND_STATUS 11
#define IX_GO_INACTIVE 12
#define IX_SET_BLOCKLEN 13
#define IX_READ_SINGLE 14
#define IX_READ_MULTI 15
#define IX_SEND_TUNING 16
#define IX_SPEED_CLASS 17
#define IX_SET_BLOCKCNT 18
#define IX_WRITE_SINGLE 19
#define IX_WRITE_MULTI 20
#define IX_PROGRAM_CSD 21
#define IX_SET_WRITE_PR 22
#define IX_CLR_WRITE_PR 23
#define IX_SND_WRITE_PR 24
#define IX_ERASE_WR_ST 25
#define IX_ERASE_WR_END 26
#define IX_ERASE 27
#define IX_LOCK_UNLOCK 28
#define IX_APP_CMD 29
#define IX_APP_CMD_RCA 30 // APP_CMD used once we have the RCA.
#define IX_GEN_CMD 31

/* Commands hereafter require APP_CMD. */
#define IX_APP_CMD_START 32
#define IX_SET_BUS_WIDTH 32
#define IX_SD_STATUS 33
#define IX_SEND_NUM_WRBL 34
#define IX_SEND_NUM_ERS 35
#define IX_APP_SEND_OP_COND 36
#define IX_SET_CLR_DET 37
#define IX_SEND_SCR 38

/*
 * CSD flags
 *
 * Note: all flags are shifted down by 8 bits as the CRC is not included.
 * Most flags are lib:
 * - in V1 the size is 12 bits with a 3 bit multiplier.
 * - in V1 currents for read and write are specified.
 * - in V2 the size is 22 bits, no multiplier, no currents.
 */
#define CSD0_VERSION 0x00c00000
#define CSD0_V1 0x00000000
#define CSD0_V2 0x00400000

/* CSD Version 1 and 2 flags. */
#define CSD1VN_TRAN_SPEED 0xff000000

#define CSD1VN_CCC 0x00fff000
#define CSD1VN_READ_BL_LEN 0x00000f00
#define CSD1VN_READ_BL_LEN_SHIFT 8
#define CSD1VN_READ_BL_PARTIAL 0x00000080
#define CSD1VN_WRITE_BLK_MISALIGN 0x00000040
#define CSD1VN_READ_BLK_MISALIGN 0x00000020
#define CSD1VN_DSR_IMP 0x00000010

#define CSD2VN_ERASE_BLK_EN 0x00000040
#define CSD2VN_ERASE_SECTOR_SIZEH 0x0000003f
#define CSD3VN_ERASE_SECTOR_SIZEL 0x80000000

#define CSD3VN_WP_GRP_SIZE 0x7f000000

#define CSD3VN_WP_GRP_ENABLE 0x00800000
#define CSD3VN_R2W_FACTOR 0x001c0000
#define CSD3VN_WRITE_BL_LEN 0x0003c000
#define CSD3VN_WRITE_BL_LEN_SHIFT 14
#define CSD3VN_WRITE_BL_PARTIAL 0x00002000
#define CSD3VN_FILE_FORMAT_GROUP 0x00000080
#define CSD3VN_COPY 0x00000040
#define CSD3VN_PERM_WRITE_PROT 0x00000020
#define CSD3VN_TEMP_WRITE_PROT 0x00000010
#define CSD3VN_FILE_FORMAT 0x0000000c
#define CSD3VN_FILE_FORMAT_HDD 0x00000000
#define CSD3VN_FILE_FORMAT_DOSFAT 0x00000004
#define CSD3VN_FILE_FORMAT_UFF 0x00000008
#define CSD3VN_FILE_FORMAT_UNKNOWN 0x0000000c

/* CSD Version 1 flags. */
#define CSD1V1_C_SIZEH 0x00000003
#define CSD1V1_C_SIZEH_SHIFT 10

#define CSD2V1_C_SIZEL 0xffc00000
#define CSD2V1_C_SIZEL_SHIFT 22
#define CSD2V1_VDD_R_CURR_MIN 0x00380000
#define CSD2V1_VDD_R_CURR_MAX 0x00070000
#define CSD2V1_VDD_W_CURR_MIN 0x0000e000
#define CSD2V1_VDD_W_CURR_MAX 0x00001c00
#define CSD2V1_C_SIZE_MULT 0x00000380
#define CSD2V1_C_SIZE_MULT_SHIFT 7

/* CSD Version 2 flags. */
#define CSD2V2_C_SIZE 0x3fffff00
#define CSD2V2_C_SIZE_SHIFT 8

/*
 * SCR flags
 *
 * NOTE: SCR is big-endian, so flags appear byte-wise reversed from the spec.
 */
#define SCR_STRUCTURE 0x000000f0
#define SCR_STRUCTURE_V1 0x00000000

#define SCR_SD_SPEC 0x0000000f
#define SCR_SD_SPEC_1_101 0x00000000
#define SCR_SD_SPEC_11 0x00000001
#define SCR_SD_SPEC_2_3 0x00000002

#define SCR_DATA_AFTER_ERASE 0x00008000

#define SCR_SD_SECURITY 0x00007000
#define SCR_SD_SEC_NONE 0x00000000
#define SCR_SD_SEC_NOT_USED 0x00001000
#define SCR_SD_SEC_101 0x00002000 // SDSC
#define SCR_SD_SEC_2 0x00003000   // SDHC
#define SCR_SD_SEC_3 0x00004000   // SDXC

#define SCR_SD_BUS_WIDTHS 0x00000f00
#define SCR_SD_BUS_WIDTH_1 0x00000100
#define SCR_SD_BUS_WIDTH_4 0x00000400

#define SCR_SD_SPEC3 0x00800000
#define SCR_SD_SPEC_2 0x00000000
#define SCR_SD_SPEC_3 0x00100000

#define SCR_EX_SECURITY 0x00780000

#define SCR_CMD_SUPPORT 0x03000000
#define SCR_CMD_SUPP_SET_BLKCNT 0x02000000
#define SCR_CMD_SUPP_SPEED_CLASS 0x01000000

/* SD card types. */
#define SD_TYPE_MMC 1
#define SD_TYPE_1 2
#define SD_TYPE_2_SC 3
#define SD_TYPE_2_HC 4

extern const char *SD_TYPE_NAME[];

/* SD card functions supported values. */
#define SD_SUPP_SET_BLOCK_COUNT 0x80000000
#define SD_SUPP_SPEED_CLASS 0x40000000
#define SD_SUPP_BUS_WIDTH_4 0x20000000
#define SD_SUPP_BUS_WIDTH_1 0x10000000

#define MBX_PROP_CLOCK_EMMC 1

/* SD card descriptor. */
typedef struct SDDescriptor {
    /* Static information about the SD Card. */
    u64 capacity;
    u32 cid[4];
    u32 cSD[4];
    u32 scr[2];
    u32 ocr;
    u32 support;
    u32 file_format;
    u8 type;
    u8 uhsi;
    u8 init;
    u8 absent;

    // Dynamic information.
    u32 rca;
    u32 card_state;
    u32 status;

    EMMCCommand *last_cmd;
    u32 last_arg;
} SDDescriptor;

extern SDDescriptor sdd;

extern EMMCCommand sd_command_table[];

void sd_delayus(u32 cnt);
int sd_init();
int sd_send_command(int index);
int sd_send_command_arg(int index, int arg);
int sd_wait_for_interrupt(u32 mask);
int sd_wait_for_data();
int fls_long(unsigned long x);