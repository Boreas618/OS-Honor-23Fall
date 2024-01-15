#include <driver/sd.h>

SDDescriptor sdd;

static int sd_host_version = 0;
static int sd_debug = 0;
static int sd_base_clock;

const char *SD_TYPE_NAME[] = {"Unknown", "MMC", "Type 1", "Type 2 SC",
                              "Type 2 HC"};

/* Command table. */
EMMCCommand sd_command_table[] = {
    {"GO_IDLE_STATE", 0x00000000 | CMD_RSPNS_NO, RESP_NO, RCA_NO, 0},
    {"ALL_SEND_CID", 0x02000000 | CMD_RSPNS_136, RESP_R2I, RCA_NO, 0},
    {"SEND_REL_ADDR", 0x03000000 | CMD_RSPNS_48, RESP_R6, RCA_NO, 0},
    {"SET_DSR", 0x04000000 | CMD_RSPNS_NO, RESP_NO, RCA_NO, 0},
    {"SWITCH_FUNC", 0x06000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"CARD_SELECT", 0x07000000 | CMD_RSPNS_48B, RESP_R1b, RCA_YES, 0},
    {"SEND_IF_COND", 0x08000000 | CMD_RSPNS_48, RESP_R7, RCA_NO, 100},
    {"SEND_CSD", 0x09000000 | CMD_RSPNS_136, RESP_R2S, RCA_YES, 0},
    {"SEND_CID", 0x0A000000 | CMD_RSPNS_136, RESP_R2I, RCA_YES, 0},
    {"VOLT_SWITCH", 0x0B000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"STOP_TRANS", 0x0C000000 | CMD_RSPNS_48B, RESP_R1b, RCA_NO, 0},
    {"SEND_STATUS", 0x0D000000 | CMD_RSPNS_48, RESP_R1, RCA_YES, 0},
    {"GO_INACTIVE", 0x0F000000 | CMD_RSPNS_NO, RESP_NO, RCA_YES, 0},
    {"SET_BLOCKLEN", 0x10000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"READ_SINGLE", 0x11000000 | CMD_RSPNS_48 | CMD_IS_DATA | TM_DAT_DIR_CH,
     RESP_R1, RCA_NO, 0},
    {"READ_MULTI", 0x12000000 | CMD_RSPNS_48 | TM_MULTI_DATA | TM_DAT_DIR_CH,
     RESP_R1, RCA_NO, 0},
    {"SEND_TUNING", 0x13000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SPEED_CLASS", 0x14000000 | CMD_RSPNS_48B, RESP_R1b, RCA_NO, 0},
    {"SET_BLOCKCNT", 0x17000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"WRITE_SINGLE", 0x18000000 | CMD_RSPNS_48 | CMD_IS_DATA | TM_DAT_DIR_HC,
     RESP_R1, RCA_NO, 0},
    {"WRITE_MULTI", 0x19000000 | CMD_RSPNS_48 | TM_MULTI_DATA | TM_DAT_DIR_HC,
     RESP_R1, RCA_NO, 0},
    {"PROGRAM_CSD", 0x1B000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SET_WRITE_PR", 0x1C000000 | CMD_RSPNS_48B, RESP_R1b, RCA_NO, 0},
    {"CLR_WRITE_PR", 0x1D000000 | CMD_RSPNS_48B, RESP_R1b, RCA_NO, 0},
    {"SND_WRITE_PR", 0x1E000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"ERASE_WR_ST", 0x20000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"ERASE_WR_END", 0x21000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"ERASE", 0x26000000 | CMD_RSPNS_48B, RESP_R1b, RCA_NO, 0},
    {"LOCK_UNLOCK", 0x2A000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"APP_CMD", 0x37000000 | CMD_RSPNS_NO, RESP_NO, RCA_NO, 100},
    {"APP_CMD", 0x37000000 | CMD_RSPNS_48, RESP_R1, RCA_YES, 0},
    {"GEN_CMD", 0x38000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},

    // APP commands must be prefixed by an APP_CMD.
    {"SET_BUS_WIDTH", 0x06000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SD_STATUS", 0x0D000000 | CMD_RSPNS_48, RESP_R1, RCA_YES, 0}, // RCA???
    {"SEND_NUM_WRBL", 0x16000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SEND_NUM_ERS", 0x17000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SD_SENDOPCOND", 0x29000000 | CMD_RSPNS_48, RESP_R3, RCA_NO, 1000},
    {"SET_CLR_DET", 0x2A000000 | CMD_RSPNS_48, RESP_R1, RCA_NO, 0},
    {"SEND_SCR", 0x33000000 | CMD_RSPNS_48 | CMD_IS_DATA | TM_DAT_DIR_CH,
     RESP_R1, RCA_NO, 0},
};

static int sd_debug_response(int resp) {
    printk("- EMMC: Command %s resp %x: %x %x %x %x\n", sdd.last_cmd->name,
           resp, *EMMC_RESP3, *EMMC_RESP2, *EMMC_RESP1, *EMMC_RESP0);
    printk("- EMMC: Status: %x, control1: %x, interrupt: %x\n", *EMMC_STATUS,
           *EMMC_CONTROL1, *EMMC_INTERRUPT);
    return resp;
}

void sd_delayus(u32 c) {
    /* Delay 3 times longer on rpi3. */
    delay_us(c * 3);
}

/* Wait for interrupt. */
int sd_wait_for_interrupt(unsigned int mask) {
    /* Wait up to 1 second for the interrupt. */
    int count = 1000000;
    int wait_mask = (int)(mask | INT_ERROR_MASK);
    int ival;

    /* Wait for the specified interrupt or any error. */
    while (count--) {
        if ((*EMMC_INTERRUPT & (u32)wait_mask))
            break;
        sd_delayus(1);
    }

    ival = (int)(*EMMC_INTERRUPT);

    /* Check for success. */
    if (count <= 0 || (ival & INT_CMD_TIMEOUT) || (ival & INT_DATA_TIMEOUT)) {

        arch_dsb_sy();

        /* Clear the interrupt register completely. */
        *EMMC_INTERRUPT = (u32)ival;
        return SD_TIMEOUT;
    }

    if (ival & INT_ERROR_MASK) {
        printk("* EMMC: Error while waiting for interrupt: %x %x %x\n",
               *EMMC_STATUS, ival, *EMMC_RESP0);

        arch_dsb_sy();

        /* Clear the interrupt register completely. */
        *EMMC_INTERRUPT = (u32)ival;
        return SD_ERROR;
    }

    arch_dsb_sy();

    /* Clear the interrupt we were waiting for, leaving any other (non-error)
     * interrupts. */
    *EMMC_INTERRUPT = mask;

    return SD_OK;
}

/* Wait for any command that may be in progress. */
static int sd_wait_for_command() {
    /* Check for status indicating a command in progress. */
    int count = 1000000;
    while (count--) {
        if (!(*EMMC_STATUS & SR_CMD_INHIBIT) ||
            (*EMMC_INTERRUPT & INT_ERROR_MASK))
            break;
        sd_delayus(1);
    }

    if (count <= 0 || (*EMMC_INTERRUPT & INT_ERROR_MASK)) {
        printk("* EMMC: Wait for command aborted: %x %x %x\n", *EMMC_STATUS,
               *EMMC_INTERRUPT, *EMMC_RESP0);
        return SD_BUSY;
    }

    return SD_OK;
}

/* Wait for any data that may be in progress. */
int sd_wait_for_data() {
    /*
     * Check for status indicating data transfer in progress.
     * Spec indicates a maximum wait of 500ms.
     * For now this is done by waiting for the DAT_INHIBIT flag to go from the
     * status register, or until an error is flagged in the interrupt register.
     */
    int count = 0;
    while (++count < 500000) {
        if (!(*EMMC_STATUS & SR_DAT_INHIBIT) ||
            (*EMMC_INTERRUPT & INT_ERROR_MASK))
            break;
        sd_delayus(1);
    }

    if (count >= 500000 || (*EMMC_INTERRUPT & INT_ERROR_MASK)) {
        printk("* EMMC: Wait for data aborted: %x %x %x\n", *EMMC_STATUS,
               *EMMC_INTERRUPT, *EMMC_RESP0);
        return SD_BUSY;
    }

    return SD_OK;
}

/* Send command and handle response. */
static int sd_send_commandP(EMMCCommand *cmd, int arg) {
    /* Check for command in progress */
    if (sd_wait_for_command() != 0)
        return SD_BUSY;

    if (sd_debug)
        printk("- EMMC: Sending command %s code %x arg %x\n", cmd->name,
               cmd->code, arg);

    sdd.last_cmd = cmd;
    sdd.last_arg = (u32)arg;

    int result;

    /* Clear interrupt flags.  This is done by setting the ones that are
     * currently set. */
    *EMMC_INTERRUPT = *EMMC_INTERRUPT;

    /*
     * Set the argument and the command code.
     * Some commands require a delay before reading the response.
     */
    *EMMC_ARG1 = (u32)arg;

    *EMMC_CMDTM = cmd->code;

    arch_dsb_sy();

    if (cmd->delay)
        sd_delayus((u32)cmd->delay);

    /* Wait until command complete interrupt. */
    if ((result = sd_wait_for_interrupt(INT_CMD_DONE)))
        return result;

    arch_dsb_sy();

    /* Get response from RESP0. */
    int resp0 = (int)*EMMC_RESP0;

    arch_dsb_sy();

    /* Handle response types. */
    switch (cmd->resp) {
        /* No response. */
    case RESP_NO:
        return SD_OK;

        /*
         * RESP0 contains card status, no other data from the RESP*
         * registers. Return value non-zero if any error flag in the status
         * value.
         */
    case RESP_R1:
    case RESP_R1b:
        sdd.status = (u32)resp0;
        /*
         * Store the card state.  Note that this is the state the card was
         * in before the command was accepted, not the new state.
         */
        sdd.card_state = (resp0 & ST_CARD_STATE) >> R1_CARD_STATE_SHIFT;
        return resp0 & (int)R1_ERRORS_MASK;

        /*
         * RESP0..3 contains 128 bit CID or CSD shifted down by 8 bits as no CRC
         * Note: highest bits are in RESP3.
         */
    case RESP_R2I:
    case RESP_R2S:
        sdd.status = 0;
        unsigned int *data = cmd->resp == RESP_R2I ? sdd.cid : sdd.cSD;
        data[0] = *EMMC_RESP3;
        data[1] = *EMMC_RESP2;
        data[2] = *EMMC_RESP1;
        data[3] = (u32)resp0;
        return SD_OK;

        /* RESP0 contains OCR register */
    case RESP_R3:
        sdd.status = 0;
        sdd.ocr = (u32)resp0;
        return SD_OK;

        /* RESP0 contains RCA and status bits 23,22,19,12:0 */
    case RESP_R6:
        sdd.rca = (u32)resp0 & R6_RCA_MASK;
        sdd.status =
            (u32)((resp0 & 0x00001fff)) // 12:0 map directly to status 12:0
            | (u32)((resp0 & 0x00002000) << 6) // 13 maps to status 19 ERROR
            | (u32)((resp0 & 0x00004000)
                    << 8) // 14 maps to status 22 ILLEGAL_COMMAND
            | (u32)((resp0 & 0x00008000)
                    << 8); // 15 maps to status 23 COM_CRC_ERROR
        /*
         * Store the card state.
         * Note that this is the state the card was in before the command was
         * accepted, not the new state.
         */
        sdd.card_state = (resp0 & ST_CARD_STATE) >> R1_CARD_STATE_SHIFT;
        return (int)(sdd.status & R1_ERRORS_MASK);

        /* RESP0 contains voltage acceptance and check pattern, which should
         * match the argument. */
    case RESP_R7:
        sdd.status = 0;
        return resp0 == arg ? SD_OK : SD_ERROR;
    default:
        printk("Unexpected response.\n");
        PANIC();
    }

    return SD_ERROR;
}

/* Send APP_CMD. */
static int sd_send_app_cmd() {
    int resp;

    /*
     * If no RCA, send the APP_CMD and don't look for a response.
     * If there is an RCA, include that in APP_CMD and check card accepted it.
     */
    if (!sdd.rca)
        sd_send_commandP(&sd_command_table[IX_APP_CMD], 0x00000000);
    else {
        if ((resp = sd_send_commandP(&sd_command_table[IX_APP_CMD_RCA],
                                     (int)sdd.rca)))
            return sd_debug_response(resp);
        /* Debug - check that status indicates APP_CMD accepted. */
        if (!(sdd.status & ST_APP_CMD))
            return SD_ERROR;
    }

    return SD_OK;
}

/*
 * Send a command with no argument.
 * RCA automatically added if required.
 * APP_CMD sent automatically if required.
 */
int sd_send_command(int index) {
    /* Issue APP_CMD if needed. */
    int resp;

    if (index >= IX_APP_CMD_START && (resp = sd_send_app_cmd()))
        return sd_debug_response(resp);

    /* Get the command and set RCA if required. */
    EMMCCommand *cmd = &sd_command_table[index];
    int arg = 0;
    if (cmd->rca == RCA_YES)
        arg = (int)sdd.rca;

    if ((resp = sd_send_commandP(cmd, arg)))
        return resp;

    /* Check that APP_CMD was correctly interpreted. */
    if (index >= IX_APP_CMD_START && sdd.rca && !(sdd.status & ST_APP_CMD))
        return SD_ERROR_APP_CMD;

    return resp;
}

/*
 * Send a command with a specific argument.
 * APP_CMD sent automatically if required.
 */
int sd_send_command_arg(int index, int arg) {
    /* Issue APP_CMD if needed. */
    int resp;
    if (index >= IX_APP_CMD_START && (resp = sd_send_app_cmd()))
        return sd_debug_response(resp);

    /* Get the command and pass the argument through. */
    if ((resp = sd_send_commandP(&sd_command_table[index], arg)))
        return resp;

    /* Check that APP_CMD was correctly interpreted. */
    if (index >= IX_APP_CMD_START && sdd.rca && !(sdd.status & ST_APP_CMD))
        return SD_ERROR_APP_CMD;

    return resp;
}

/* Read card's SCR. */
static int sd_read_scr() {
    /*
     * SEND_SCR command is like a READ_SINGLE but for a block of 8 bytes.
     * Ensure that any data operation has completed before reading the block.
     */
    if (sd_wait_for_data())
        return SD_TIMEOUT;

    arch_dsb_sy();

    /* Set BLKSIZECNT to 1 block of 8 bytes, send SEND_SCR command */
    *EMMC_BLKSIZECNT = (1 << 16) | 8;

    int resp;

    if ((resp = sd_send_command(IX_SEND_SCR)))
        return sd_debug_response(resp);

    /* Wait for READ_RDY interrupt. */
    if ((resp = sd_wait_for_interrupt(INT_READ_RDY))) {
        printk("* ERROR EMMC: Timeout waiting for ready to read\n");
        return sd_debug_response(resp);
    }

    /* Allow maximum of 100ms for the read operation. */
    int num_read = 0;
    int count = 100000;

    while (num_read < 2) {
        if (*EMMC_STATUS & SR_READ_AVAILABLE)
            sdd.scr[num_read++] = *EMMC_DATA;
        else {
            sd_delayus(1);
            if (--count == 0)
                break;
        }
    }

    /* If SCR not fully read, the operation timed out. */
    if (num_read != 2) {
        printk("* ERROR EMMC: SEND_SCR ERR: %x %x %x\n", *EMMC_STATUS,
               *EMMC_INTERRUPT, *EMMC_RESP0);
        printk("* EMMC: Reading SCR, only read %d words\n", num_read);
        return SD_TIMEOUT;
    }

    /* Parse out the SCR.  Only interested in values in scr[0], scr[1] is mfr
     * specific. */
    if (sdd.scr[0] & SCR_SD_BUS_WIDTH_4)
        sdd.support |= SD_SUPP_BUS_WIDTH_4;
    if (sdd.scr[0] & SCR_SD_BUS_WIDTH_1)
        sdd.support |= SD_SUPP_BUS_WIDTH_1;
    if (sdd.scr[0] & SCR_CMD_SUPP_SET_BLKCNT)
        sdd.support |= SD_SUPP_SET_BLOCK_COUNT;
    if (sdd.scr[0] & SCR_CMD_SUPP_SPEED_CLASS)
        sdd.support |= SD_SUPP_SPEED_CLASS;

    return SD_OK;
}

int fls_long(unsigned long x) {
    int r = 32;
    if (!x)
        return 0;
    if (!(x & 0xffff0000u)) {
        x <<= 16;
        r -= 16;
    }
    if (!(x & 0xff000000u)) {
        x <<= 8;
        r -= 8;
    }
    if (!(x & 0xf0000000u)) {
        x <<= 4;
        r -= 4;
    }
    if (!(x & 0xc0000000u)) {
        x <<= 2;
        r -= 2;
    }
    if (!(x & 0x80000000u)) {
        x <<= 1;
        r -= 1;
    }
    return r;
}

/*
 * Get the clock divider for the given requested frequency.
 * This is calculated relative to the SD base clock.
 */
static u32 sd_get_clk_divider(u32 freq) {
    u32 divisor;
    /* Pi SD frequency is always 41.66667Mhz on baremetal */
    u32 closest = 41666666 / freq;

    /* Get the raw shiftcount */
    u32 shiftcount = (u32)fls_long(closest - 1);

    /* Note the offset of shift by 1 (look at the spec) */
    if (shiftcount > 0)
        shiftcount--;

    /* It's only 8 bits maximum on HOST_SPEC_V2 */
    if (shiftcount > 7)
        shiftcount = 7;

    /* Version 3 take closest while version 2 take power 2*/
    if (sd_host_version > HOST_SPEC_V2)
        divisor = closest;
    else
        divisor = (1u << shiftcount);

    if (divisor <= 2) {
        divisor = 2;
        shiftcount = 0;
    }

    u32 hi = 0;
    if (sd_host_version > HOST_SPEC_V2)
        hi = (divisor & 0x300) >> 2;
    u32 lo = (divisor & 0x0ff);
    u32 cdiv = (lo << 8) + hi;
    return cdiv;
}

/* Set the SD clock to the given frequency. */
static int sd_set_clk(int freq) {
    /* Wait for any pending inhibit bits. */
    int count = 100000;
    while ((*EMMC_STATUS & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) && --count)
        sd_delayus(1);

    if (count <= 0) {
        printk("* EMMC ERROR: Set clock: timeout waiting for inhibit flags. "
               "Status %08x.\n",
               *EMMC_STATUS);
        return SD_ERROR_CLOCK;
    }

    arch_dsb_sy();

    /* Switch clock off. */
    *EMMC_CONTROL1 &= (u32)(~C1_CLK_EN);
    sd_delayus(10);

    arch_dsb_sy();

    /* Request the new clock setting and enable the clock. */
    int cdiv = (int)sd_get_clk_divider((u32)freq);
    *EMMC_CONTROL1 = (*EMMC_CONTROL1 & 0xffff003f) | (u32)cdiv;
    sd_delayus(10);

    arch_dsb_sy();

    /* Enable the clock. */
    *EMMC_CONTROL1 |= C1_CLK_EN;
    sd_delayus(10);

    /* Wait for clock to be stable. */
    count = 10000;
    while (!(*EMMC_CONTROL1 & C1_CLK_STABLE) && count--)
        sd_delayus(10);

    if (count <= 0) {
        printk("* EMMC: ERROR: failed to get stable clock.\n");
        return SD_ERROR_CLOCK;
    }

    return SD_OK;
}

/* Reset card. */
static int sd_reset_card(int resetType) {
    int resp, count;

    /* Send reset host controller and wait for complete. */
    *EMMC_CONTROL0 = 0;

    arch_dsb_sy();

    *EMMC_CONTROL1 |= (u32)resetType;

    sd_delayus(10);

    arch_dsb_sy();

    count = 10000;
    while (count--) {
        if (!(*EMMC_CONTROL1 & (u32)resetType))
            break;
        sd_delayus(10);
    }

    if (count <= 0) {
        printk("* EMMC: ERROR: failed to reset.\n");
        return SD_ERROR_RESET;
    }

    arch_dsb_sy();

    /* Enable internal clock and set data timeout. */
    *EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;
    sd_delayus(10);

    arch_dsb_sy();

    /* Set clock to setup frequency. */
    if ((resp = sd_set_clk(FREQ_SETUP)))
        return resp;

    arch_dsb_sy();

    /* Enable interrupts for command completion values. */
    *EMMC_IRPT_EN = 0xffffffff & (u32)(~INT_CMD_DONE) & (~(u32)INT_WRITE_RDY);
    *EMMC_IRPT_MASK = 0xffffffff;

    arch_dsb_sy();

    /* Reset card registers. */
    sdd.rca = 0;
    sdd.ocr = 0;
    sdd.last_arg = 0;
    sdd.last_cmd = 0;
    sdd.status = 0;
    sdd.type = 0;
    sdd.uhsi = 0;

    /* Send GO_IDLE_STATE. */
    resp = sd_send_command(IX_GO_IDLE_STATE);

    return resp;
}

/*
 * lib routine for APP_SEND_OP_COND.
 * This is used for both SC and HC cards based on the parameter.
 */
static int sd_app_send_op_cond(int arg) {
    /*
     * Send APP_SEND_OP_COND with the given argument (for SC or HC cards).
     * Note: The host shall set ACMD41 timeout more than 1 second to abort
     * repeat of issuing ACMD41
     */
    int resp, count;
    if ((resp = sd_send_command_arg(IX_APP_SEND_OP_COND, arg)) &&
        resp != SD_TIMEOUT) {
        printk("* EMMC: ACMD41 returned non-timeout error %d\n", resp);
        return resp;
    }

    count = 6;
    while (!(sdd.ocr & R3_COMPLETE) && count--) {
        printk("- EMMC: Retrying ACMD SEND_OP_COND status %x\n", *EMMC_STATUS);
        delay(50000);
        if ((resp = sd_send_command_arg(IX_APP_SEND_OP_COND, arg)) &&
            resp != SD_TIMEOUT) {
            printk("* EMMC: ACMD41 returned non-timeout error %d\n", resp);
            return resp;
        }
    }

    /* Return timeout error if still not busy. */
    if (!(sdd.ocr & R3_COMPLETE))
        return SD_TIMEOUT;

    /* Check that at least one voltage value was returned. */
    if (!(sdd.ocr & ACMD41_VOLTAGE))
        return SD_ERROR_VOLTAGE;

    return SD_OK;
}

/* Switch voltage to 1.8v where the card supports it. */
static int sd_switch_voltage() {
    printk("- EMMC: Pi does not support switch voltage, fixed at 3.3volt\n");
    return SD_OK;
}

/* Routine to initialize GPIO registers. */
static void sd_init_gpio() {
    u32 r;
    /* GPIO_CD */
    r = device_get_u32(GPFSEL4);
    r &= (u32)(~(7 << (7 * 3)));
    device_put_u32(GPFSEL4, r);
    device_put_u32(GPPUD, 2);
    delay(150);
    device_put_u32(GPPUDCLK1, 1 << 15);
    delay(150);
    device_put_u32(GPPUD, 0);
    device_put_u32(GPPUDCLK1, 0);
    r = device_get_u32(GPHEN1);
    r |= 1 << 15;
    device_put_u32(GPHEN1, r);

    /* GPIO_CLK, GPIO_CMD */
    r = device_get_u32(GPFSEL4);
    r |= (7 << (8 * 3)) | (7 << (9 * 3));
    device_put_u32(GPFSEL4, r);
    device_put_u32(GPPUD, 2);
    delay(150);
    device_put_u32(GPPUDCLK1, (1 << 16) | (1 << 17));
    delay(150);
    device_put_u32(GPPUD, 0);
    device_put_u32(GPPUDCLK1, 0);

    /* GPIO_DAT0, GPIO_DAT1, GPIO_DAT2, GPIO_DAT3 */
    r = device_get_u32(GPFSEL5);
    r |= (7 << (0 * 3)) | (7 << (1 * 3)) | (7 << (2 * 3)) | (7 << (3 * 3));
    device_put_u32(GPFSEL5, r);
    device_put_u32(GPPUD, 2);
    delay(150);
    device_put_u32(GPPUDCLK1, (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21));
    delay(150);
    device_put_u32(GPPUD, 0);
    device_put_u32(GPPUDCLK1, 0);
}

/* Get the base clock speed. */
int sd_get_base_clk() {
    sd_base_clock = mbox_get_clock_rate(MBX_PROP_CLOCK_EMMC);
    if (sd_base_clock == -1) {
        printk("* EMMC: Error, failed to get base clock from mailbox\n");
        return SD_ERROR;
    }
    return SD_OK;
}

/* Parse CSD. */
void sd_parse_csd() {
    int ccd_version = sdd.cSD[0] & CSD0_VERSION;

    /* For now just work out the size. */
    if (ccd_version == CSD0_V1) {
        int csize =
            (int)(((sdd.cSD[1] & CSD1V1_C_SIZEH) << CSD1V1_C_SIZEH_SHIFT) +
                  ((sdd.cSD[2] & CSD2V1_C_SIZEL) >> CSD2V1_C_SIZEL_SHIFT));
        int mult = 1 << (((sdd.cSD[2] & CSD2V1_C_SIZE_MULT) >>
                          CSD2V1_C_SIZE_MULT_SHIFT) +
                         2);

        i64 block_size = 1 << ((sdd.cSD[1] & CSD1VN_READ_BL_LEN) >>
                               CSD1VN_READ_BL_LEN_SHIFT);
        i64 num_blocks = (csize + 1LL) * mult;

        sdd.capacity = (u64)(num_blocks * block_size);
    } else {
        i64 csize = (sdd.cSD[2] & CSD2V2_C_SIZE) >> CSD2V2_C_SIZE_SHIFT;
        sdd.capacity = (u64)((csize + 1LL) * 512LL * 1024LL);
    }

    /* Get other attributes of the card. */
    sdd.file_format = sdd.cSD[3] & CSD3VN_FILE_FORMAT;
}

/*
 * Initialize SD card.
 * Returns zero if initialization was successful, non-zero otherwise.
 */
int sd_init() {
    /* Ensure we've initialized GPIO. */
    if (!sdd.init)
        sd_init_gpio();

    int card_absent = 0;

    int card_ejected = device_get_u32(GPEDS1) & (1 << (47 - 32));
    int old_cid[4];

    /*
     * No card present, nothing can be done.
     * Only log the fact that the card is absent the first time we discover it.
     */
    if (card_absent) {
        sdd.init = 0;
        int was_absent = sdd.absent;
        sdd.absent = 1;
        if (!was_absent)
            printk("* EMMC: no SD card detected");
        return SD_CARD_ABSENT;
    }

    /*
     * If initialized before, check status of the card.
     * Card present, but removed since last call to init, then need to
     * go back through init, and indicate that in the return value.
     * In this case we would expect INT_CARD_INSERT to be set.
     * Clear the insert and remove interrupts
     */
    sdd.absent = 0;
    if (card_ejected && sdd.init) {
        sdd.init = 0;
        memmove(old_cid, sdd.cid, sizeof(int) * 4);
    } else if (!sdd.init)
        memset(old_cid, 0, sizeof(int) * 4);

    /* If already initialized and card not replaced, nothing to do. */
    if (sdd.init)
        return SD_OK;

    sd_host_version =
        (*EMMC_SLOTISR_VER & HOST_SPEC_NUM) >> HOST_SPEC_NUM_SHIFT;

    int resp;

    /* Get base clock speed. */
    if ((resp = sd_get_base_clk()))
        return resp;

    /* Reset the card. */
    if ((resp = sd_reset_card(C1_SRST_HC)))
        return resp;

    arch_dsb_sy();

    /* Send SEND_IF_COND,0x000001AA (CMD8) voltage range 0x1 check pattern 0xAA.
     */
    resp = sd_send_command_arg(IX_SEND_IF_COND, 0x000001AA);

    if (resp == SD_OK) {
        /*
         * Card responded with voltage and check pattern.
         * Resolve voltage and check for high capacity card.
         */
        delay(50);
        if ((resp = sd_app_send_op_cond(ACMD41_ARG_HC)))
            return sd_debug_response(resp);

        /* Check for high or standard capacity. */
        if (sdd.ocr & R3_CCS)
            sdd.type = SD_TYPE_2_HC;
        else
            sdd.type = SD_TYPE_2_SC;

    } else if (resp == SD_BUSY)
        return resp;
    else {
        /* If voltage range and check pattern don't match, look for older card.
         */
        printk("- no response to SEND_IF_COND, treat as an old card.\n");

        /* If there appears to be a command in progress, reset the card. */
        if ((*EMMC_STATUS & SR_CMD_INHIBIT) &&
            (resp = sd_reset_card(C1_SRST_HC)))
            return resp;

        /* Resolve voltage. */
        if ((resp = sd_app_send_op_cond(ACMD41_ARG_SC)))
            return sd_debug_response(resp);

        sdd.type = SD_TYPE_1;
    }

    /*
     * If the switch to 1.8A is accepted, then we need to send a CMD11.
     *
     * CMD11: Completion of voltage switch sequence is checked by high level of
     * DAT[3:0]. Any bit of DAT[3:0] can be checked depends on ability of the
     * host.
     *
     * Appears for PI its any/all bits.
     */
    if ((sdd.ocr & R3_S18A) && (resp = sd_switch_voltage()))
        return resp;

    /* Send ALL_SEND_CID (CMD2) */
    if ((resp = sd_send_command(IX_ALL_SEND_CID)))
        return sd_debug_response(resp);

    /* Send SEND_REL_ADDR (CMD3) */
    if ((resp = sd_send_command(IX_SEND_REL_ADDR)))
        return sd_debug_response(resp);

    /*
     * From now on the card should be in standby state.
     * Actually cards seem to respond in identify state at this point.
     */

    /* Send SEND_CSD (CMD9) and parse the result. */
    if ((resp = sd_send_command(IX_SEND_CSD)))
        return sd_debug_response(resp);

    sd_parse_csd();

    if (sdd.file_format != CSD3VN_FILE_FORMAT_DOSFAT &&
        sdd.file_format != CSD3VN_FILE_FORMAT_HDD) {
        printk("* EMMC: Error, unrecognised file format %02x\n",
               sdd.file_format);
        return SD_ERROR;
    }

    /* At this point, set the clock to full speed. */
    if ((resp = sd_set_clk(FREQ_NORMAL)))
        return sd_debug_response(resp);

    /* Send CARD_SELECT  (CMD7) */
    if ((resp = sd_send_command(IX_CARD_SELECT)))
        return sd_debug_response(resp);

    /*
     * Get the SCR as well.
     * Need to do this before sending ACMD6 so that allowed bus widths are
     * known.
     */
    if ((resp = sd_read_scr()))
        return sd_debug_response(resp);

    /*
     * Send APP_SET_BUS_WIDTH (ACMD6)
     * supported, set 4 bit bus width and update the CONTROL0 register.
     */
    if (sdd.support & SD_SUPP_BUS_WIDTH_4) {
        if ((resp = sd_send_command_arg(IX_SET_BUS_WIDTH, (int)sdd.rca | 2)))
            return sd_debug_response(resp);
        *EMMC_CONTROL0 |= C0_HCTL_DWITDH;
    }

    /* Send SET_BLOCKLEN (CMD16) */
    if ((resp = sd_send_command_arg(IX_SET_BLOCKLEN, 512)))
        return sd_debug_response(resp);

    /* Initialisation complete. */
    sdd.init = 1;

    /* Return value indicates whether the card was reinserted or replaced. */
    if (memcmp(old_cid, sdd.cid, sizeof(int) * 4) == 0)
        return SD_CARD_REINSERTED;

    return SD_CARD_CHANGED;
}