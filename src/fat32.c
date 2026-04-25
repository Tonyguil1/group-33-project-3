#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"

/* ── Global image state ──────────────────────────────────────── */
FILE  *img       = NULL;
BPB_t  bpb;
char   img_name[256];

/* ── fat32_open ──────────────────────────────────────────────── */
/* Opens the image file, reads the boot sector into the global    */
/* BPB struct, and stores the image's base name for the prompt.   */
/* Returns 0 on success, -1 on failure.                           */
int fat32_open(const char *path) {
    img = fopen(path, "r+b");  /* r+b = read/write, binary, no truncate */
    if (!img) {
        fprintf(stderr, "Error: cannot open image '%s'\n", path);
        return -1;
    }

    /* Read the 90-byte BPB from the very start of the image */
    fseek(img, 0, SEEK_SET);
    if (fread(&bpb, sizeof(BPB_t), 1, img) != 1) {
        fprintf(stderr, "Error: failed to read boot sector\n");
        fclose(img);
        img = NULL;
        return -1;
    }

    /* Quick sanity check: BPB_BytsPerSec should be 512 for most images */
    if (bpb.BPB_BytsPerSec == 0) {
        fprintf(stderr, "Error: image does not look like a valid FAT32 volume\n");
        fclose(img);
        img = NULL;
        return -1;
    }

    /* Extract just the filename (no path) for the shell prompt */
    const char *slash = strrchr(path, '/');
    strncpy(img_name, slash ? slash + 1 : path, sizeof(img_name) - 1);
    img_name[sizeof(img_name) - 1] = '\0';

    return 0;
}

/* ── fat32_close ─────────────────────────────────────────────── */
void fat32_close(void) {
    if (img) {
        fclose(img);
        img = NULL;
    }
}

/* ── cluster_to_offset ───────────────────────────────────────── */
/* Converts a cluster number to its byte offset within the image. */
/*                                                                 */
/* FAT32 layout:                                                   */
/*   [Reserved sectors] [FAT tables] [Data region starting at 2]  */
/*                                                                 */
/* first_data_sector = reserved + (numFATs × FATSz32)             */
/* cluster offset    = first_data_sector + (cluster-2) × SecPerClus */
uint32_t cluster_to_offset(uint32_t cluster) {
    uint32_t first_data_sector = bpb.BPB_RsvdSecCnt
                               + (bpb.BPB_NumFATs * bpb.BPB_FATSz32);
    uint32_t sector = first_data_sector
                    + (cluster - 2) * bpb.BPB_SecPerClus;
    return sector * bpb.BPB_BytsPerSec;
}

/* ── next_cluster ────────────────────────────────────────────── */
/* Looks up the next cluster in the FAT for a given cluster.      */
/* Each FAT32 entry is 4 bytes. Mask the top 4 bits (reserved).   */
/* Returns FAT_EOC (0x0FFFFFF8) or higher when at end of chain.   */
uint32_t next_cluster(uint32_t cluster) {
    /* FAT starts after the reserved sectors */
    uint32_t fat_start  = bpb.BPB_RsvdSecCnt * bpb.BPB_BytsPerSec;
    uint32_t fat_offset = fat_start + cluster * 4;

    uint32_t val = 0;
    fseek(img, fat_offset, SEEK_SET);
    fread(&val, 4, 1, img);

    return val & 0x0FFFFFFF;  /* mask upper 4 reserved bits */
}

/* ── get_first_cluster ───────────────────────────────────────── */
/* Combines the high and low 16-bit halves of the cluster number  */
/* stored in a directory entry.                                    */
uint32_t get_first_cluster(const DirEntry_t *entry) {
    return ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

/* ── count_data_clusters ─────────────────────────────────────── */
/* Calculates the total number of clusters in the data region.    */
/* Formula from the FAT spec:                                      */
/*   data_sectors = total_sectors - (reserved + FATs + root_dir)  */
/*   total_clusters = data_sectors / sectors_per_cluster          */
uint32_t count_data_clusters(void) {
    uint32_t total_sectors     = bpb.BPB_TotSec32;
    uint32_t reserved_sectors  = bpb.BPB_RsvdSecCnt;
    uint32_t fat_sectors       = bpb.BPB_NumFATs * bpb.BPB_FATSz32;
    uint32_t data_sectors      = total_sectors - reserved_sectors - fat_sectors;
    return data_sectors / bpb.BPB_SecPerClus;
}

/* ── count_fat_entries ───────────────────────────────────────── */
/* Each FAT entry is 4 bytes. Total entries = (FATSz32 × BytsPerSec) / 4 */
uint32_t count_fat_entries(void) {
    return (bpb.BPB_FATSz32 * bpb.BPB_BytsPerSec) / 4;
}

/* ── write_fat_entry ─────────────────────────────────────────── */
/* Updates a single FAT entry in EVERY copy of the FAT.           */
/*                                                                 */
/* FAT32 typically keeps two FAT copies on disk (BPB_NumFATs == 2)*/
/* for redundancy. Tools like fsck will complain if the copies    */
/* diverge, so always update all of them.                         */
/*                                                                 */
/* Note: the upper 4 bits of a FAT32 entry are reserved. We mask  */
/* the value to the lower 28 bits and OR it with the existing     */
/* high bits to be a good citizen of the spec.                    */
void write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_start = bpb.BPB_RsvdSecCnt * bpb.BPB_BytsPerSec;
    uint32_t fat_bytes = bpb.BPB_FATSz32   * bpb.BPB_BytsPerSec;

    for (uint8_t f = 0; f < bpb.BPB_NumFATs; f++) {
        long off = fat_start + f * fat_bytes + cluster * 4;

        /* Read existing entry so we preserve the reserved 4 bits */
        uint32_t old;
        fseek(img, off, SEEK_SET);
        fread(&old, 4, 1, img);

        uint32_t new_val = (old & 0xF0000000) | (value & 0x0FFFFFFF);

        fseek(img, off, SEEK_SET);
        fwrite(&new_val, 4, 1, img);
    }
    fflush(img);
}

/* ── zero_cluster ────────────────────────────────────────────── */
/* Writes all zeros to a cluster's data region. Critical for new  */
/* directories — unused entries must read as 0x00 (end-of-dir).   */
void zero_cluster(uint32_t cluster) {
    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;
    uint8_t *zeros = calloc(cluster_size, 1);
    if (!zeros) return;

    fseek(img, cluster_to_offset(cluster), SEEK_SET);
    fwrite(zeros, 1, cluster_size, img);
    fflush(img);
    free(zeros);
}

/* ── free_cluster_chain ──────────────────────────────────────── */
/* Walk a cluster chain starting at `start` and mark every entry */
/* in the FAT as free (0). Used by rm and rmdir.                 */
/*                                                                 */
/* We grab next_cluster BEFORE zeroing the current entry, since   */
/* zeroing it would lose the link.                                */
void free_cluster_chain(uint32_t start) {
    uint32_t c = start;
    while (c >= 2 && c < FAT_EOC) {
        uint32_t nxt = next_cluster(c);
        write_fat_entry(c, 0);
        c = nxt;
    }
}
/* ── alloc_cluster ───────────────────────────────────────────── */
/* Scans the FAT for a free entry (value == 0), claims it by      */
/* marking it as end-of-chain, zeros the data, and returns the    */
/* cluster number. Returns 0 if the volume is full.               */
/*                                                                 */
/* Clusters 0 and 1 are reserved, so we start scanning at 2.      */
uint32_t alloc_cluster(void) {
    uint32_t total      = count_fat_entries();
    uint32_t fat_start  = bpb.BPB_RsvdSecCnt * bpb.BPB_BytsPerSec;

    for (uint32_t c = 2; c < total; c++) {
        uint32_t val;
        fseek(img, fat_start + c * 4, SEEK_SET);
        fread(&val, 4, 1, img);

        if ((val & 0x0FFFFFFF) == 0) {       /* free? */
            write_fat_entry(c, 0x0FFFFFFF);  /* claim it as EOC */
            zero_cluster(c);                  /* wipe stale data */
            return c;
        }
    }
    return 0;  /* no free clusters */
}

/* ── cmd_info ────────────────────────────────────────────────── */
/* Prints the boot sector fields required by the assignment.      */
void cmd_info(void) {
    /* Get total image size by seeking to end */
    fseek(img, 0, SEEK_END);
    long img_size = ftell(img);

    printf("Position of root cluster  : %u\n",  bpb.BPB_RootClus);
    printf("Bytes per sector          : %u\n",  bpb.BPB_BytsPerSec);
    printf("Sectors per cluster       : %u\n",  bpb.BPB_SecPerClus);
    printf("Total # of clusters       : %u\n",  count_data_clusters());
    printf("# of entries in one FAT   : %u\n",  count_fat_entries());
    printf("Size of image (bytes)     : %ld\n", img_size);
}