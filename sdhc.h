#ifndef __SDHC_H__
#define __SDHC_H__

/* IDMA Descriptor */
struct mmc_descriptor_t {
    volatile unsigned int config;      /* DES0: Control/Status */
    volatile unsigned int buf_size;    /* DES1: Buffer Size */
    volatile unsigned int buf_addr;    /* DES2: Buffer Address */
    volatile unsigned int next_desc;   /* DES3: Next Descriptor Address */
};

/* IDMAC Descriptor Flags */
#define DES_OWN         (1U << 31)  /* DMA Owns this descriptor */
#define DES_DIC         (1U << 1)   /* Disable Interrupt on Completion */
#define DES_LD          (1U << 3)   /* Last Descriptor */
#define DES_FD          (1U << 2)   /* First Descriptor */
#define DES_CH          (1U << 4)   /* Chain Mode */
#define DES_ER          (1U << 5)   /* End of Ring */

/* MMC / SD Commands */
#define CMD0     0   /* GO_IDLE_STATE: Reset card to idle state */
#define CMD2     2   /* ALL_SEND_CID: Ask card to send its CID (Card ID) */
#define CMD3     3   /* SEND_RELATIVE_ADDR: Assign / request RCA (Relative Card Address) */
#define ACMD6    6   /* SET_BUS_WIDTH (after CMD55): Set data bus width (1-bit / 4-bit) */
#define CMD7     7   /* SELECT/DESELECT_CARD: Put card into transfer state */
#define CMD8     8   /* SEND_IF_COND: Check voltage range & SD version (SD2.0+) */
#define CMD12    12  /* STOP_TRANSMISSION: Stop multi‑block read/write */
#define CMD17    17  /* READ_SINGLE_BLOCK: Read one 512‑byte block */
#define CMD18    18  /* READ_MULTIPLE_BLOCK: Read multiple 512‑byte blocks */
#define CMD24    24  /* WRITE_BLOCK: Write one 512‑byte block */
#define CMD25    25  /* WRITE_MULTIPLE_BLOCK: Write multiple 512‑byte blocks */
#define CMD55    55  /* APP_CMD: Next command is application‑specific (ACMD) */
#define ACMD41   41  /* SD_SEND_OP_COND: Initialize card & negotiate operating conditions */

/* SMHC_CTRL Bits  */
#define SMHC_CTRL_SOFT_RST      (1 << 0)
#define SMHC_CTRL_FIFO_RST      (1 << 1)
#define SMHC_CTRL_DMA_RST       (1 << 2)
#define SMHC_CTRL_INT_ENB       (1 << 4)
#define SMHC_CTRL_DMA_ENB       (1 << 5)
#define SMHC_CTRL_FIFO_AC_MOD   (1U << 31) /* 0: DMA bus, 1: AHB bus */

/* SMHC_CMD Bits  */
#define SMHC_CMD_START          (1U << 31)
#define SMHC_CMD_PROG_CLK       (1 << 21)
#define SMHC_CMD_SEND_INIT      (1 << 15)
#define SMHC_CMD_STOP_ABT       (1 << 14)
#define SMHC_CMD_WAIT_PRE_OVER  (1 << 13)
#define SMHC_CMD_STOP_AUTO      (1 << 12)
#define SMHC_CMD_WRITE          (1 << 10)
#define SMHC_CMD_DATA_EXP       (1 << 9)
#define SMHC_CMD_CHECK_CRC      (1 << 8)
#define SMHC_CMD_LONG_RESP      (1 << 7)
#define SMHC_CMD_RESP_EXP       (1 << 6)

/* SMHC_RINTSTS Bits */
#define SMHC_RINT_RESP_ERR      (1 << 1)
#define SMHC_RINT_CMD_DONE      (1 << 2)
#define SMHC_RINT_DATA_DONE     (1 << 3)
#define SMHC_RINT_ERR_MASK      (0xbfc2) /* Mask of all error bits */
#define SMHC_RINT_RX_DRQ   (1 << 2)      /* Receive Data Request */

/* SMHC_IDST Bits  */
#define SMHC_IDST_TX_INT        (1 << 0)
#define SMHC_IDST_RX_INT        (1 << 1)
#define SMHC_IDST_FATAL_BERR    (1 << 2)

/* SMHC_STATUS Bits  */
#define SMHC_STATUS_FIFO_EMPTY        (1 << 2)


int smhc_init(void);
int smhc_read_block(unsigned int sector, void *buffer);
int smhc_read_blocks(unsigned int sector, unsigned int count, void *buffer);
int smhc_write_blocks(unsigned int sector, unsigned int count, void *buf);
int smhc_write_block(unsigned int sector, void *buffer);

#endif /* __SDHC_H__ */
