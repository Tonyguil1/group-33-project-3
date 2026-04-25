#ifndef FILE_H
#define FILE_H

#include <stdint.h>

/* Spec: at most 10 files may be open simultaneously. */
#define MAX_OPEN_FILES 10

/* ── OpenFile ────────────────────────────────────────────────── */
/* One slot of the in-memory open file table. The table is the   */
/* shell's bookkeeping — FAT32 itself has no concept of "open."  */
typedef struct {
    int      in_use;            /* 1 if this slot holds a live handle */
    uint8_t  name_raw[11];      /* canonical 8.3 name from disk      */
    char     name_display[13];  /* formatted for printing            */
    char     mode[4];           /* "r", "w", "rw", "wr" (no dash)    */
    uint32_t offset;            /* current read/write cursor         */
    char     path[1024];        /* full path string for lsof display */
    uint32_t parent_cluster;    /* cluster of containing directory   */
    uint32_t first_cluster;     /* cluster of file's data            */
    uint32_t size;              /* file size in bytes                */
    long     entry_offset;      /* byte offset of dir entry on disk  */
    int      can_read;          /* mode permits reading              */
    int      can_write;         /* mode permits writing              */
} OpenFile;

extern OpenFile open_files[MAX_OPEN_FILES];

/* Lookup helpers — find_open is reused by close, lseek, read,  */
/* write, and the rm/rmdir/mv "is it open?" checks.             */
int  find_open_by_raw(uint32_t parent_cluster, const uint8_t name_raw[11]);

/* Commands */
void cmd_open  (const char *name, const char *flag);
void cmd_close (const char *name);
void cmd_lsof  (void);
void cmd_lseek (const char *name, uint32_t offset);
void cmd_read  (const char *name, uint32_t size);
void cmd_write (const char *name, const char *str);

#endif /* FILE_H */