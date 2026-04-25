#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stdio.h>

/* ── Directory entry attribute flags ─────────────────────────── */
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  0x0F   /* skip these entries */

/* FAT32 end-of-chain marker */
#define FAT_EOC         0x0FFFFFF8

/* ── Boot Parameter Block (BPB) ──────────────────────────────── */
/* __attribute__((packed)) prevents the compiler from adding      */
/* padding bytes, so our struct matches the raw disk layout.      */
typedef struct __attribute__((packed)) {
    uint8_t  BS_jmpBoot[3];      /* offset  0 – jump instruction  */
    uint8_t  BS_OEMName[8];      /* offset  3 – OEM name string   */
    uint16_t BPB_BytsPerSec;     /* offset 11 – bytes per sector  */
    uint8_t  BPB_SecPerClus;     /* offset 13 – sectors per clus  */
    uint16_t BPB_RsvdSecCnt;     /* offset 14 – reserved sectors  */
    uint8_t  BPB_NumFATs;        /* offset 16 – number of FATs    */
    uint16_t BPB_RootEntCnt;     /* offset 17 – 0 on FAT32        */
    uint16_t BPB_TotSec16;       /* offset 19 – 0 on FAT32        */
    uint8_t  BPB_Media;          /* offset 21 – media descriptor  */
    uint16_t BPB_FATSz16;        /* offset 22 – 0 on FAT32        */
    uint16_t BPB_SecPerTrk;      /* offset 24 – sectors/track     */
    uint16_t BPB_NumHeads;       /* offset 26 – number of heads   */
    uint32_t BPB_HiddSec;        /* offset 28 – hidden sectors    */
    uint32_t BPB_TotSec32;       /* offset 32 – total sectors     */
    uint32_t BPB_FATSz32;        /* offset 36 – sectors per FAT   */
    uint16_t BPB_ExtFlags;       /* offset 40 – ext flags         */
    uint16_t BPB_FSVer;          /* offset 42 – filesystem ver    */
    uint32_t BPB_RootClus;       /* offset 44 – root dir cluster  */
    uint16_t BPB_FSInfo;         /* offset 48 – FSInfo sector     */
    uint16_t BPB_BkBootSec;      /* offset 50 – backup boot sec   */
    uint8_t  BPB_Reserved[12];   /* offset 52 – reserved          */
    uint8_t  BS_DrvNum;          /* offset 64 – drive number      */
    uint8_t  BS_Reserved1;       /* offset 65 – reserved          */
    uint8_t  BS_BootSig;         /* offset 66 – boot signature    */
    uint32_t BS_VolID;           /* offset 67 – volume serial #   */
    uint8_t  BS_VolLab[11];      /* offset 71 – volume label      */
    uint8_t  BS_FilSysType[8];   /* offset 82 – "FAT32   "        */
} BPB_t;

/* ── 32-byte Directory Entry ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  DIR_Name[11];       /* 8.3 name, space-padded        */
    uint8_t  DIR_Attr;           /* attribute byte                */
    uint8_t  DIR_NTRes;          /* reserved for Windows NT       */
    uint8_t  DIR_CrtTimeTenth;   /* creation time (tenths of sec) */
    uint16_t DIR_CrtTime;        /* creation time                 */
    uint16_t DIR_CrtDate;        /* creation date                 */
    uint16_t DIR_LstAccDate;     /* last access date              */
    uint16_t DIR_FstClusHI;      /* high 16 bits of cluster #     */
    uint16_t DIR_WrtTime;        /* last write time               */
    uint16_t DIR_WrtDate;        /* last write date               */
    uint16_t DIR_FstClusLO;      /* low 16 bits of cluster #      */
    uint32_t DIR_FileSize;       /* file size in bytes            */
} DirEntry_t;

/* ── Global image state ──────────────────────────────────────── */
/* Declared here, defined in fat32.c so every file can access it */
extern FILE  *img;          /* the open image file pointer       */
extern BPB_t  bpb;          /* the parsed boot sector            */
extern char   img_name[256];/* base name of the image file       */

/* ── Function prototypes ─────────────────────────────────────── */
int      fat32_open(const char *path);
void     fat32_close(void);
void     cmd_info(void);

uint32_t get_first_cluster(const DirEntry_t *entry);
uint32_t cluster_to_offset(uint32_t cluster);
uint32_t next_cluster(uint32_t cluster);
uint32_t count_data_clusters(void);
uint32_t count_fat_entries(void);

/* Cluster + FAT modification helpers (used by mkdir, creat,      */
/* write, rm, rmdir, etc.)                                        */
void     write_fat_entry(uint32_t cluster, uint32_t value);
void     zero_cluster(uint32_t cluster);
uint32_t alloc_cluster(void);
void     free_cluster_chain(uint32_t start_cluster);
void     free_cluster_chain(uint32_t start);

#endif /* FAT32_H */