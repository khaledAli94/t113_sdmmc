#include<sdhc.h>
#include<gpio.h>
#include<uart.h>
#include<ccu.h>

static struct mmc_descriptor_t dma_desc __attribute__((aligned(4)));;

/* Wait for a bit to clear or set */
/* Returns 0 on success, -1 on timeout */
int wait_bit(volatile unsigned int *reg, unsigned int mask, int set, int timeout) {
    while (timeout--) {
        if (set) { if ((*reg & mask) == mask) return 0; }
        else     { if ((*reg & mask) == 0) return 0; }
    }
    return -1;
}

/* Update Clock Command Sequence */
int smhc_update_clock(void) {
    /* Send command to update clock: Start | Prog Clk | Wait Pre Over */
    unsigned int cmd = SMHC_CMD_START | SMHC_CMD_PROG_CLK | SMHC_CMD_WAIT_PRE_OVER;
    SMHC0->CMD = cmd;
    
    /* Wait for Command Complete */
    if (wait_bit(&SMHC0->RINTSTS, SMHC_RINT_CMD_DONE, 1, 0xFFFFF) != 0) return -1;
    
    /* Clear Interrupt */
    SMHC0->RINTSTS = SMHC_RINT_CMD_DONE;
    return 0;
}

/* Send Command API */
int smhc_send_cmd(unsigned int cmd_idx, unsigned int arg, unsigned int flags) {  
    /* 1. Clear Raw Interrupts */
    SMHC0->RINTSTS = 0xFFFFFFFF;
    
    /* 2. Write Argument */
    SMHC0->CMDARG = arg;
    
    /* 3. Construct Command Value */
    unsigned int cmd_val = SMHC_CMD_START | (cmd_idx & 0x3F) | flags;
    SMHC0->CMD = cmd_val;
    
    /* 4. Wait for Completion */
    int timeout = 0xFFFFFF;
    while (timeout--) {
        unsigned int status = SMHC0->RINTSTS;
        
        if (status & SMHC_RINT_ERR_MASK) {
            /* Error occurred */
            SMHC0->RINTSTS = status; /* Write 1 to clear */
            return -2;
        }
        
        if (status & SMHC_RINT_CMD_DONE) {
            SMHC0->RINTSTS = SMHC_RINT_CMD_DONE;
            return 0;
        }
    }
    return -1; /* Timeout */
}

/* Set Output Clock Divider */
int smhc_set_clock(unsigned int div) {    
    /* Disable clock first */
    SMHC0->CLKDIV &= ~(1 << 16); /* CCLK_ENB = 0 */
    smhc_update_clock();
    
    /* Set Divider */
    SMHC0->CLKDIV = (div & 0xFF) | (1 << 16); /* Set Div and Enable */
    return smhc_update_clock();
}

int smhc_card_set_width(unsigned int rca, int width) {
    int ret;
    
    /* 1. Send CMD55 (Application Command) */
    /* Note: We need the RCA (Relative Card Address) for CMD55 */
    ret = smhc_send_cmd(CMD55, rca << 16, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);
    if (ret != 0) return ret;

    /* 2. Send ACMD6 (Set Bus Width) */
    /* Argument: 0 = 1-bit, 2 = 4-bit */
    unsigned int arg = (width == 4) ? 2 : 0;
    
    ret = smhc_send_cmd(ACMD6, arg, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);
    return ret;
}

int smhc_init(void) {
    /* 1. Hardware Init */
    smhc_gpio_init();
    smhc_clk_init();

    /* 2. Reset Controller */
    SMHC0->CTRL = SMHC_CTRL_SOFT_RST;
    if (wait_bit(&SMHC0->CTRL, SMHC_CTRL_SOFT_RST, 0, 0xFFFFF) != 0) return -1;
    
    /* Reset FIFO and DMA */
    SMHC0->CTRL |= SMHC_CTRL_FIFO_AC_MOD | (SMHC_CTRL_FIFO_RST | SMHC_CTRL_DMA_RST);

    /* Wait for FIFO reset to clear (optional but good practice) */
    while (SMHC0->CTRL & SMHC_CTRL_FIFO_RST);
    
    /* 3. Interrupt Mask */
    SMHC0->INTMASK = 0; /* Mask all for polled mode, or enable relevant ones */
    SMHC0->CTRL |= SMHC_CTRL_INT_ENB; /* Global Enable */

    /* 4. Clock Setup (Low Speed for Init < 400kHz) */
    /* Src=24MHz. Div=60 => 400kHz. */
    smhc_set_clock(60); 
    
    /* 5. Bus Width 1-bit */
    SMHC0->CTYPE = 0; /* 1-bit */
    
    /* 6. Card Identification (Simplified Flow) */
    
    /* CMD0: Go Idle */
    smhc_send_cmd(CMD0, 0, SMHC_CMD_SEND_INIT); 
    
    /* CMD8: Voltage check */
    if (smhc_send_cmd(CMD8, 0x1AA, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC) != 0) return -2;

    /* ACMD41: Voltage Window */
    int retries = 2000;
    while (retries--) {
        smhc_send_cmd(CMD55, 0, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);

        /* Arg: 0x40FF8000 -> HCS(Bit 30) | Voltage 3.2-3.4V (Bits 23-8) */
        smhc_send_cmd(ACMD41, 0x40FF8000, SMHC_CMD_RESP_EXP); /* HCS | Voltage */
        /* Check Bit 31 (Busy Bit) of Response */
        if (SMHC0->RESP0 & (1U << 31)) break; /* Ready bit */
        volatile int d = 1000; while(d--);
    }
    
    if (retries <= 0) return -3; /* Card Timeout */
    
    /* CMD2: All CID */
    smhc_send_cmd(CMD2, 0, SMHC_CMD_RESP_EXP | SMHC_CMD_LONG_RESP | SMHC_CMD_CHECK_CRC);
    
    /* CMD3: RCA */
    smhc_send_cmd(CMD3, 0, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);
    unsigned int rca = (SMHC0->RESP0 >> 16) & 0xFFFF;
    
    /* CMD7: Select Card */
    smhc_send_cmd(CMD7, rca << 16, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);
    
    /* Switch to High Speed (e.g., 24MHz full HOSC) */
    /* A. Tell SD Card to use 4-bit bus */
    if (smhc_card_set_width(rca, 4) != 0) {
        /* If card fails 4-bit, you could fallback to 1-bit here */
        uprintf("[WARN] Card refused 4-bit mode. Staying at 1-bit.\r\n");
        return -4;
    }
    else {
        /* 2. Tell Controller to switch to 4-bit mode */
        SMHC0->CTYPE = 1; 
        uprintf("[INFO] 4-Bit Mode Enabled.\r\n");
    }

    /* 7. Switch to High Speed (50 MHz) & 4-Bit */
    /* A. Tell SD Card to use 4-bit bus (ACMD6) */
    if (smhc_card_set_width(rca, 4) != 0) {
        uprintf("[WARN] Card refused 4-bit. Staying at 1-bit.\r\n");
        /* If 4-bit fails, we continue in 1-bit mode */
    } 
    else {
        /* B. Tell Controller to switch to 4-bit */
        SMHC0->CTYPE = 1; 
        uprintf("[INFO] 4-Bit Mode Enabled.\r\n");
    }

    /* C. Switch Clock Source to PLL_PERI (50MHz) */
    /* This function overwrites the Source/M/N bits, so no need for set_clock(0) before */
    smhc_set_pll_clock();
    
    uprintf("[INFO] Switched to High Speed (50MHz)\r\n");
    return 0;
}

/* dma*/
#if 0
/* Read Blocks API  */
int smhc_read_blocks(unsigned int sector, unsigned int count, void *buffer) {
    unsigned int byte_cnt = count * 512;
    
    /* 1. DMA Reset */
    SMHC0->CTRL |= SMHC_CTRL_DMA_RST;
    wait_bit(&SMHC0->CTRL, SMHC_CTRL_DMA_RST, 0, 0xFFFF);
    
    SMHC0->IDMAC = (1 << 7) | (1 << 1); /* IDMAC_EN | FIX_BURST */
    SMHC0->IDIE = (1 << 0) | (1 << 1);  /* Enable TX/RX Interrupts */
    
    /* 2. Configure FIFO */
    /* FIFO_DEPTH=1024 words? T113 usually 256 words (1KB). 
       RX_TL (Receive Trigger) = 15. TX_TL = 15. Burst=4.
       0x10000000 (Burst 1 transfer) | (15<<16) | 15.
    */
    SMHC0->FIFOTH = (1 << 28) | (15 << 16) | (15 << 0);
    
    /* 3. Descriptor Setup */
    /* DES_OWN (Bit 31) = 1: DMA owns it.
       DES_CH  (Bit 4)  = 1: Use 'next_desc' for chaining.
       DES_LD  (Bit 3)  = 1: Last descriptor in transfer.
       DES_FD  (Bit 2)  = 1: First descriptor in transfer.
    */
    dma_desc.config = DES_OWN | DES_CH | DES_LD | DES_FD;
    dma_desc.buf_size = byte_cnt;
    dma_desc.buf_addr = (unsigned int)buffer;
    dma_desc.next_desc = 0; /* No next descriptor, but set 0 to be safe */
    
    /* CRITICAL FIX 1: Flush Cache 
       If the Descriptor stays in CPU Cache, DMA (which reads RAM) sees Zeros. 
       Use 'dsb' (Data Sync Barrier) to ensure write completes. */
    __asm__ volatile("dsb"); 
    __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(&dma_desc)); /* Clean D-Cache line */
    SMHC0->DLBA = (unsigned int)&dma_desc;
    
    /* 4. CMD Setup */
    SMHC0->BLKSIZ = 512;
    SMHC0->BYTCNT = byte_cnt;
    
    unsigned int cmd = (count > 1) ? CMD18 : CMD17;
    unsigned int flags = SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC | 
                         SMHC_CMD_DATA_EXP | SMHC_CMD_WAIT_PRE_OVER;
    if (count > 1) flags |= SMHC_CMD_STOP_AUTO;
    
    if (smhc_send_cmd(cmd, sector, flags) != 0) return -1;
    
    /* 5. Wait for DMA RX Complete */
    /* Poll IDST[1] (RX_INT) */
    if (wait_bit(&SMHC0->IDST, SMHC_IDST_RX_INT, 1, 0xFFFFFF) != 0) return -2;
    SMHC0->IDST = SMHC_IDST_RX_INT; /* Clear */
    
    /* 6. Wait for Data Transfer Complete */
    if (wait_bit(&SMHC0->RINTSTS, SMHC_RINT_DATA_DONE, 1, 0xFFFFF) != 0) return -3;
    SMHC0->RINTSTS = SMHC_RINT_DATA_DONE;
    
    return 0;
}
#endif

/* Adapted from Allwinner H3 Working Driver (cpu polling) */
#if 1
/* Matches signature in sdhc.h: (sector, count, buf) */
int smhc_read_blocks(unsigned int sector, unsigned int count, void *buf) {
    unsigned *buf_u32 = (unsigned *)buf;
    /* 512 bytes per block / 4 bytes per word = 128 words */
    unsigned total_words = 128 * count;
    
    /* 1. Setup Command */
    /* FIX: For your 2GB card (SDSC), use Byte Addressing (sector * 512) */
    unsigned arg = sector * 512; 
    
    /* Use CMD18 for multiple blocks, CMD17 for single */
    unsigned cmd = (count > 1) ? CMD18 : CMD17;
    
    /* Send Command */
    smhc_send_cmd(cmd, arg, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC | SMHC_CMD_DATA_EXP);

    /* 2. Read Loop  */
    /* Check FIFO status for EVERY word */
    for (int i = 0; i < total_words; i++) {
        unsigned timeout = 0xFFFFF;
        
        /* Wait while FIFO is Empty  */
        /* STATUS Bit 2 = FIFO Empty. We loop while it is 1. */
        while (SMHC0->STATUS & SMHC_STATUS_FIFO_EMPTY && timeout--) {
            /* Wait */
        }

        if (timeout == 0) {
            uprintf("[FAIL] FIFO Timeout at word %d\r\n", i);
            return -1;
        }

        /* FIFO has data! Read it. */
        buf_u32[i] = SMHC0->FIFO;
    }

    /* If multi-block, send CMD12 (STOP_TRANSMISSION) */
    if (count > 1) {

        /* Send CMD12 */
        smhc_send_cmd(CMD12, 0, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);

        /* Wait for DATA_OVER */
        unsigned timeout = 0xFFFFF;
        while (!(SMHC0->RINTSTS & SMHC_RINT_DATA_DONE) && timeout--) {
            /* wait */
        }

        if (timeout == 0) {
            uprintf("[FAIL] CMD12 timeout\r\n");
            return -1;
        }
    }

    /* Check for any error bits */
    if (SMHC0->RINTSTS & SMHC_RINT_ERR_MASK) {
        uprintf("[FAIL] Read error: RINTSTS=0x%x\r\n", SMHC0->RINTSTS);
        SMHC0->RINTSTS = 0xFFFFFFFF;
        return -1;
    }

    /* Clear all interrupts */
    SMHC0->RINTSTS = 0xFFFFFFFF;

    return 0;
}

#endif 

int smhc_read_block(unsigned int sector, void *buffer) {
    return smhc_read_blocks(sector, 1, buffer);
}

/* Write Blocks API cpu polling */
int smhc_write_blocks(unsigned int sector, unsigned int count, void *buf) {
    unsigned int *buf_u32 = (unsigned int *)buf;
    unsigned int total_words = 128 * count;

    /* 1. Setup Command */
    /* FIX: Byte Addressing for SDSC (2GB) Cards */
    unsigned int arg = sector * 512;
    unsigned int cmd_idx = (count > 1) ? 25 : 24;

    /* Ensure SMHC_CMD_WRITE (Bit 10) is defined in your header! */
    /* If missing: #define SMHC_CMD_WRITE (1U << 10) */
    unsigned int flags = SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC | SMHC_CMD_DATA_EXP | SMHC_CMD_WRITE;

    if (smhc_send_cmd(cmd_idx, arg, flags) != 0) return -1;

    /* 2. Write Loop (CPU Polling) */
    for (int i = 0; i < total_words; i++) {
        unsigned int timeout = 0xFFFFF;

        /* Wait while FIFO is FULL (Bit 3) */
        while ((SMHC0->STATUS & (1 << 3)) && timeout--) { }

        if (timeout == 0) return -2;

        /* Write Data */
        SMHC0->FIFO = buf_u32[i];
    }

    /* 3. Wait for Transfer to Complete (DATA_DONE) */
    unsigned int t = 0xFFFFF;
    while(t--) {
        if (SMHC0->RINTSTS & SMHC_RINT_DATA_DONE) break;
    }
    SMHC0->RINTSTS = 0xFFFFFFFF; // Clear flags

    /* 4. CRITICAL FIX: Wait for Card Program Busy */
    /* The Card holds D0 Low while programming flash. */
    /* SMHC Status Bit 9 = Data Busy (Card Busy) */
    t = 0xFFFFFF; // Give it time (writes can take ms)
    while ((SMHC0->STATUS & (1 << 9)) && t--) {
        /* Spin while card is busy */
    }

    if (t == 0) uprintf("[WARN] Card Busy Timeout!\r\n");

    /* Send Stop for Multi-block */
    if (count > 1) {
        smhc_send_cmd(CMD12, 0, SMHC_CMD_RESP_EXP | SMHC_CMD_CHECK_CRC);
    }

    return 0;
}

int smhc_write_block(unsigned int sector, void *buffer) {
    return smhc_write_blocks(sector, 1, buffer);
}
