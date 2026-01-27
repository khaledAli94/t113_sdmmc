#include <sdhc.h>
#include <gpio.h>
#include <uart.h>
#include <ccu.h>

/* Simple memory compare to verify signatures */
int memcmp(const void *s1, const void *s2, unsigned int n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* Helper to extract a 32-bit Little Endian integer from a byte buffer */
unsigned int get_uint32_le(const unsigned char *buf) {
    return (unsigned int)buf[0] |
           ((unsigned int)buf[1] << 8) |
           ((unsigned int)buf[2] << 16) |
           ((unsigned int)buf[3] << 24);
}

/* Helper to print a small hex dump of a buffer */
void print_hex(const unsigned char *buf, int len) {
    for (int i = 0; i < len; i++) {
        /* Use simple %x. If your uprintf is very basic, 
           it might not handle leading zeros, but we will see the values. */
        uprintf("%x ", buf[i]); 
        if ((i + 1) % 16 == 0) uprintf("\r\n");
    }
    uprintf("\r\n");
}

int raise(int sig){
    return 0;
}


/* Data buffer must be aligned to 4 bytes for DMA */
unsigned char sector_buffer[512] __attribute__((aligned(4)));

int main(void) {
    int ret;
    unsigned partition_lba = 0;

    uprintf("\r\n========================================\r\n");
    uprintf("      T113-S3 SDHC Driver Test          \r\n");
    uprintf("========================================\r\n");
    /* 1. Initialize the SMHC Controller */
    uprintf("[INFO] Initializing SMHC...\r\n");
    
    
    if ((ret = smhc_init()) != 0) {
        uprintf("[FAIL] SMHC Init failed! Error code: %d\r\n", ret);
        return  ret;
    }
    uprintf("[PASS] SMHC Initialized successfully.\r\n");

    /* ---------------------------------------------------------------------
     * TEST 1: Read Master Boot Record (MBR) - Sector 0
     * --------------------------------------------------------------------- */
    uprintf("[INFO] Reading MBR (Sector 0)...\r\n");

    /* Clear buffer */
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0xcc;  
       
    /* Read Sector 0 */    
    if ((ret = smhc_read_block(0, sector_buffer)) != 0) {
        uprintf("[FAIL] Read MBR failed! Error code: %d\r\n", ret);
        return ret;
    }
    uprintf("[PASS] Read MBR complete.\r\n");

    /* Verify MBR Signature */
    unsigned char sig1 = sector_buffer[510]; //0x1f0 +14
    unsigned char sig2 = sector_buffer[511]; //0x1f0 +15
    
    uprintf("[INFO] Checking MBR Signature (Expect 55 AA): Found 0x%x 0x%x\r\n", sig1, sig2);

    if (sig1 != 0x55 || sig2 != 0xAA) {
        uprintf("[FAIL] Invalid MBR Signature!\r\n");
        uprintf("[DATA] First 16 bytes of Sector 0:\r\n");
        print_hex(sector_buffer, 16);
        return -5;
    }
    uprintf("[PASS] MBR Signature Valid.\r\n");

    /* ---------------------------------------------------------------------
     * TEST 2: Parse MBR to find File System (FS) Sector
     * --------------------------------------------------------------------- */
    
    /* Offset 0x1BE + 8 contains LBA Start of Partition 1 */
    partition_lba = get_uint32_le(&sector_buffer[0x1BE + 8]);

    uprintf("[INFO] Partition 1 LBA Start: %u (0x%x)\r\n", partition_lba, partition_lba);

    if (partition_lba == 0) {
        uprintf("[WARN] Partition LBA is 0. This might be a Superfloppy or empty card.\r\n");
    } else {
        /* -----------------------------------------------------------------
         * TEST 3: Read The First Sector of the Filesystem
         * ----------------------------------------------------------------- */
        uprintf("[INFO] Reading VBR (Volume Boot Record) at Sector %u...\r\n", partition_lba);

        /* Clear buffer */
        for (int i = 0; i < 512; i++) sector_buffer[i] = 0;

        /* Read the partition start sector */        
        if ((ret = smhc_read_block(partition_lba, sector_buffer)) != 0) {
            uprintf("[FAIL] Read VBR failed! Error code: %d\r\n", ret);
            return -6;
        }
        uprintf("[PASS] Read VBR complete.\r\n");

        /* Dump some info to prove we read real data */
        uprintf("[DATA] VBR First 16 Bytes (Jump Code & OEM Name):\r\n");
        print_hex(sector_buffer, 16);

        /* Check for common signatures */
        /* FAT32 usually has "FAT32" at offset 82 */
        /* FAT16/12 usually has "FAT16" or "FAT12" at offset 54 */
        /* NTFS usually has "NTFS" at offset 3 */
        
        uprintf("[DATA] VBR Last 2 Bytes (Signature): 0x%x 0x%x\r\n", sector_buffer[510], sector_buffer[511]);
    }
    uprintf("\r\n[DONE] Test Sequence Completed Successfully.\r\n");


    #if 0
   /* WRITE TEST: Fill ENTIRE Sector 3000 with DEADBEEF */
    uprintf("[INFO] Filling Sector 3000 with DEADBEEF...\r\n");

    /* Cast buffer to unsigned int pointer */
    unsigned int *p32 = (unsigned int *)sector_buffer;

    /* Fill all 128 words (128 * 4 = 512 bytes) */
    for(int i=0; i<128; i++) {
        p32[i] = 0xDEADBEEF;
    }

    /* Write to Sector 3000 */
    ret = smhc_write_blocks(3000, 1, sector_buffer);

    if (ret == 0) {
        uprintf("[PASS] Full Sector Write Complete.\r\n");
    }
    #endif

    return 0;
}
