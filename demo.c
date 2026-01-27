#include <sdhc.h>
#include <gpio.h>
#include <uart.h>
#include <ccu.h>
#include <fat32.h>
#include <string.h>
#include <stdint.h>

static uint8_t fs_buf[512] __attribute__((aligned(64)));

static void print_fat_name(uint8_t *name) {
    char buf[13];
    int p = 0;
    for(int i=0; i<8; i++) if(name[i]!=' ') buf[p++] = name[i];
    if(name[8]!=' ') {
        buf[p++] = '.';
        for(int i=8; i<11; i++) if(name[i]!=' ') buf[p++] = name[i];
    }
    buf[p] = 0;
    uprintf("%s", buf);
}


int main(void) {
    smhc_init();
    
    uprintf("\r\n[BOOT] Scanning partitions...\r\n");

    /* 1. Read MBR */
    if (smhc_read_blocks(0, 1, fs_buf)) {
        uprintf("[FAIL] MBR Read Error\r\n");
        return -1;
    }
    
    struct mbr_t *mbr = (struct mbr_t *)fs_buf;

    /* UPDATED: Using 'signature' instead of 'sig' */
    if (mbr->signature != 0xAA55) {
        uprintf("[FAIL] Invalid MBR Sig: 0x%x\r\n", mbr->signature);
        return -2;
    }

    /* 2. Auto-Detect FAT Partition */
    int found_idx = -1;
    uint32_t part_lba = 0;

    for (int i = 0; i < 4; i++) {
        /* UPDATED: Using 'partitions' instead of 'part' */
        uint8_t type = mbr->partitions[i].type;
        uint32_t lba = mbr->partitions[i].lba_start;
        uint32_t size = mbr->partitions[i].sector_count;

        if (size == 0) continue;

        uprintf("[PART] %d: Type 0x%x, LBA %d\r\n", i, type, lba);

        if (type == 0x01 || type == 0x04 || type == 0x06 || 
            type == 0x0E || type == 0x0B || type == 0x0C) {
            
            uprintf("[BOOT] Found FAT at Index %d\r\n", i);
            found_idx = i;
            part_lba = lba;
            
            /* UPDATED: Using 'partitions' */
            if (mbr->partitions[i].status == 0x80) break; 
        }
    }

    if (found_idx == -1) {
        uprintf("[FAIL] No FAT partition found\r\n");
        return -3;
    }

    /* 3. Read VBR / BPB */
    if (smhc_read_blocks(part_lba, 1, fs_buf)) return -4;
    
    struct fat32_bootsector_t *bpb = (struct fat32_bootsector_t *)fs_buf;

    /* 4. Mount Filesystem */
    uint32_t fat_size = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    uint32_t fat_begin = part_lba + bpb->reserved_sectors;
    uint32_t cluster_begin = fat_begin + (bpb->num_fats * fat_size);
    uint32_t root_lba;

    if (bpb->fat_size_16 != 0) {
        /* FAT16 */
        root_lba = cluster_begin;
        uprintf("[FS] FAT16 Root LBA: %d\r\n", root_lba);
    } else {
        /* FAT32 */
        root_lba = cluster_begin + ((bpb->root_cluster - 2) * bpb->sectors_per_cluster);
        uprintf("[FS] FAT32 Root LBA: %d\r\n", root_lba);
    }

    /* 5. List Files */
    if (smhc_read_blocks(root_lba, 1, fs_buf)) return -5;
    
    struct fat32_dir_entry_t *dir = (struct fat32_dir_entry_t *)fs_buf;

    uprintf("\r\n=== FILES ===\r\n");
    for (int i = 0; i < 16; i++) {
        /* FIXED SYNTAX: dir[i], not dir.[i] */
        if (dir[i].name[0] == 0) break;       
        if (dir[i].name[0] == 0xE5) continue; 
        if (dir[i].attr == 0x0F) continue;    

        if (dir[i].attr & 0x10) uprintf("[DIR]  ");
        else                    uprintf("[FILE] ");
        
        print_fat_name(dir[i].name);
        uprintf(" (%d bytes)\r\n", dir[i].size);
    }
    uprintf("=================\r\n");

    return 0;
}
