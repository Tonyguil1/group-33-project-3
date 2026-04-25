#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"
#include "shell.h"
#include "directory.h"
#include "file.h"

/* The table itself. Globals are zero-initialized at program     */
/* start, so every slot begins with in_use == 0.                 */
OpenFile open_files[MAX_OPEN_FILES];

/* ── find_open_by_raw ────────────────────────────────────────── */
/* Look up an entry by (parent_cluster, raw 11-byte name).        */
/* Returns the slot index, or -1 if not found.                   */
int find_open_by_raw(uint32_t parent_cluster, const uint8_t name_raw[11]) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) continue;
        if (open_files[i].parent_cluster != parent_cluster) continue;
        if (memcmp(open_files[i].name_raw, name_raw, 11) == 0) return i;
    }
    return -1;
}

/* ── parse_mode ──────────────────────────────────────────────── */
/* Translate "-r", "-w", "-rw", "-wr" into can_read/can_write.   */
/* Returns 0 on success, -1 if the flag is not one of those.     */
static int parse_mode(const char *flag, int *can_read, int *can_write) {
    if (strcmp(flag, "-r")  == 0) { *can_read = 1; *can_write = 0; return 0; }
    if (strcmp(flag, "-w")  == 0) { *can_read = 0; *can_write = 1; return 0; }
    if (strcmp(flag, "-rw") == 0) { *can_read = 1; *can_write = 1; return 0; }
    if (strcmp(flag, "-wr") == 0) { *can_read = 1; *can_write = 1; return 0; }
    return -1;
}

/* ── cmd_open ────────────────────────────────────────────────── */
void cmd_open(const char *name, const char *flag) {
    int can_read, can_write;
    if (parse_mode(flag, &can_read, &can_write) != 0) {
        fprintf(stderr,
            "Error: invalid mode '%s' (use -r, -w, -rw, or -wr)\n", flag);
        return;
    }

    /* Verify the file exists and isn't a directory. */
    DirEntry_t e;
    long       entry_off;
    if (find_entry(cwd_cluster, name, &e, &entry_off) != 0) {
        fprintf(stderr, "Error: file '%s' does not exist\n", name);
        return;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        fprintf(stderr, "Error: '%s' is a directory, cannot be opened\n", name);
        return;
    }

    /* Reject re-opening a file already in the table. */
    if (find_open_by_raw(cwd_cluster, e.DIR_Name) >= 0) {
        fprintf(stderr, "Error: file '%s' is already opened\n", name);
        return;
    }

    /* Find a free slot. */
    int idx = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        fprintf(stderr,
            "Error: too many open files (max %d)\n", MAX_OPEN_FILES);
        return;
    }

    /* Populate the slot. */
    OpenFile *of = &open_files[idx];
    of->in_use = 1;
    memcpy(of->name_raw, e.DIR_Name, 11);
    format_name(e.DIR_Name, of->name_display);

    /* Store mode without the leading dash for lsof display. */
    strncpy(of->mode, flag + 1, sizeof(of->mode) - 1);
    of->mode[sizeof(of->mode) - 1] = '\0';

    of->offset         = 0;
    of->parent_cluster = cwd_cluster;
    of->first_cluster  = get_first_cluster(&e);
    of->size           = e.DIR_FileSize;
    of->entry_offset   = entry_off;
    of->can_read       = can_read;
    of->can_write      = can_write;

    /* Build the displayed path: cwd_path + "/" + display name.   */
    /* Copy name_display to a local first — snprintf's args must  */
    /* not overlap with its destination, and both fields live in  */
    /* the same struct.                                            */
    char nm[13];
    strcpy(nm, of->name_display);
    if (strcmp(cwd_path, "/") == 0)
        snprintf(of->path, sizeof(of->path), "/%s", nm);
    else
        snprintf(of->path, sizeof(of->path), "%.1000s/%s", cwd_path, nm);
}

/* ── cmd_close ───────────────────────────────────────────────── */
void cmd_close(const char *name) {
    DirEntry_t e;
    if (find_entry(cwd_cluster, name, &e, NULL) != 0) {
        fprintf(stderr, "Error: file '%s' does not exist\n", name);
        return;
    }

    int idx = find_open_by_raw(cwd_cluster, e.DIR_Name);
    if (idx < 0) {
        fprintf(stderr, "Error: file '%s' is not opened\n", name);
        return;
    }

    /* Mark slot free; data is overwritten on next open. */
    open_files[idx].in_use = 0;
}

/* ── cmd_lsof ────────────────────────────────────────────────── */
void cmd_lsof(void) {
    int any = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) continue;

        if (!any) {
            printf("INDEX  NAME          MODE  OFFSET  PATH\n");
            any = 1;
        }
        printf("%-5d  %-12s  %-4s  %-6u  %s\n",
               i,
               open_files[i].name_display,
               open_files[i].mode,
               open_files[i].offset,
               open_files[i].path);
    }
    if (!any) printf("No files are currently opened.\n");
}

/* ── cmd_lseek ───────────────────────────────────────────────── */
void cmd_lseek(const char *name, uint32_t offset) {
    DirEntry_t e;
    if (find_entry(cwd_cluster, name, &e, NULL) != 0) {
        fprintf(stderr, "Error: file '%s' does not exist\n", name);
        return;
    }

    int idx = find_open_by_raw(cwd_cluster, e.DIR_Name);
    if (idx < 0) {
        fprintf(stderr, "Error: file '%s' is not opened\n", name);
        return;
    }

    if (offset > open_files[idx].size) {
        fprintf(stderr,
            "Error: offset (%u) is larger than file size (%u)\n",
            offset, open_files[idx].size);
        return;
    }

    open_files[idx].offset = offset;
}

/* ── cmd_read ────────────────────────────────────────────────── */
/* Read SIZE bytes from the file's current offset to stdout.     */
/*                                                                 */
/* The trick is that file data may span multiple clusters, and    */
/* those clusters are linked through the FAT (not contiguous on   */
/* disk). So we walk the cluster chain to find the one containing */
/* the start offset, then keep walking as we copy bytes out.      */
void cmd_read(const char *name, uint32_t size) {
    DirEntry_t e;
    if (find_entry(cwd_cluster, name, &e, NULL) != 0) {
        fprintf(stderr, "Error: file '%s' does not exist\n", name);
        return;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        fprintf(stderr, "Error: '%s' is a directory\n", name);
        return;
    }

    int idx = find_open_by_raw(cwd_cluster, e.DIR_Name);
    if (idx < 0 || !open_files[idx].can_read) {
        fprintf(stderr,
            "Error: file '%s' is not opened for reading\n", name);
        return;
    }

    OpenFile *of = &open_files[idx];

    /* Already at or past EOF, or empty file → nothing to read. */
    if (of->offset >= of->size || of->first_cluster == 0) {
        printf("\n");
        return;
    }

    /* Cap the read so it never goes past the end of the file. */
    uint32_t remaining = of->size - of->offset;
    if (size > remaining) size = remaining;

    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;

    /* Step 1: walk forward through the chain to find the cluster */
    /* that contains byte `offset` of the file.                   */
    uint32_t cluster = of->first_cluster;
    uint32_t skip    = of->offset / cluster_size;
    for (uint32_t i = 0; i < skip; i++) {
        cluster = next_cluster(cluster);
        if (cluster >= FAT_EOC) {
            fprintf(stderr,
                "Error: file's cluster chain ended unexpectedly\n");
            return;
        }
    }

    /* Position within the starting cluster. */
    uint32_t in_clus_off = of->offset % cluster_size;
    uint32_t left        = size;

    /* Step 2: stream bytes out, advancing cluster as we go. */
    char *buf = malloc(cluster_size);
    if (!buf) { fprintf(stderr, "Error: out of memory\n"); return; }

    while (left > 0 && cluster < FAT_EOC) {
        uint32_t can = cluster_size - in_clus_off;
        if (can > left) can = left;

        fseek(img, cluster_to_offset(cluster) + in_clus_off, SEEK_SET);
        fread (buf, 1, can, img);
        fwrite(buf, 1, can, stdout);

        left          -= can;
        of->offset    += can;
        in_clus_off    = 0;     /* subsequent clusters start at 0 */

        if (left > 0) cluster = next_cluster(cluster);
    }

    free(buf);
    printf("\n");  /* trailing newline so the prompt isn't clobbered */
}

/* ── cmd_write ───────────────────────────────────────────────── */
/* Write STRING into a file at its current offset, growing the    */
/* file (and allocating new clusters) as needed.                  */
/*                                                                 */
/* Three things may need updating in the on-disk dir entry at the */
/* end:                                                            */
/*   • DIR_FstClusHI/LO — if we just allocated the very first     */
/*     cluster for an empty file.                                 */
/*   • DIR_FileSize    — if the write extended the file.          */
void cmd_write(const char *name, const char *str) {
    DirEntry_t e;
    long       entry_off;
    if (find_entry(cwd_cluster, name, &e, &entry_off) != 0) {
        fprintf(stderr, "Error: file '%s' does not exist\n", name);
        return;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        fprintf(stderr, "Error: '%s' is a directory\n", name);
        return;
    }

    int idx = find_open_by_raw(cwd_cluster, e.DIR_Name);
    if (idx < 0 || !open_files[idx].can_write) {
        fprintf(stderr,
            "Error: file '%s' is not opened for writing\n", name);
        return;
    }

    OpenFile *of    = &open_files[idx];
    uint32_t  bytes = strlen(str);
    if (bytes == 0) return;            /* empty string is a no-op */

    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;

    /* If file has no allocated cluster yet, give it one. */
    int allocated_first = 0;
    if (of->first_cluster == 0) {
        uint32_t c = alloc_cluster();
        if (c == 0) {
            fprintf(stderr, "Error: no free clusters\n");
            return;
        }
        of->first_cluster = c;
        allocated_first   = 1;
    }

    /* Walk to the cluster that contains `offset`. If `offset`    */
    /* sits past the last allocated cluster (only possible if a   */
    /* previous write extended the file but stopped on a boundary)*/
    /* we extend the chain on demand here too.                    */
    uint32_t cluster = of->first_cluster;
    uint32_t skip    = of->offset / cluster_size;
    for (uint32_t i = 0; i < skip; i++) {
        uint32_t nxt = next_cluster(cluster);
        if (nxt >= FAT_EOC) {
            uint32_t newc = alloc_cluster();
            if (newc == 0) {
                fprintf(stderr, "Error: no free clusters\n");
                return;
            }
            write_fat_entry(cluster, newc);
            nxt = newc;
        }
        cluster = nxt;
    }

    /* Stream bytes into the file, allocating clusters as needed. */
    uint32_t in_clus = of->offset % cluster_size;
    uint32_t left    = bytes;
    const char *p    = str;

    while (left > 0) {
        uint32_t can = cluster_size - in_clus;
        if (can > left) can = left;

        fseek (img, cluster_to_offset(cluster) + in_clus, SEEK_SET);
        fwrite(p, 1, can, img);

        p          += can;
        left       -= can;
        of->offset += can;
        in_clus     = 0;

        if (left > 0) {
            uint32_t nxt = next_cluster(cluster);
            if (nxt >= FAT_EOC) {
                uint32_t newc = alloc_cluster();
                if (newc == 0) {
                    fprintf(stderr,
                        "Error: ran out of clusters mid-write\n");
                    break;
                }
                write_fat_entry(cluster, newc);
                nxt = newc;
            }
            cluster = nxt;
        }
    }
    fflush(img);

    /* Update the on-disk directory entry if size or first cluster*/
    /* changed.                                                    */
    int size_changed = (of->offset > of->size);
    if (size_changed) of->size = of->offset;

    if (allocated_first || size_changed) {
        DirEntry_t fresh;
        fseek(img, of->entry_offset, SEEK_SET);
        fread(&fresh, sizeof(fresh), 1, img);

        if (allocated_first) {
            fresh.DIR_FstClusHI = (of->first_cluster >> 16) & 0xFFFF;
            fresh.DIR_FstClusLO =  of->first_cluster        & 0xFFFF;
        }
        if (size_changed)
            fresh.DIR_FileSize = of->size;

        fseek(img, of->entry_offset, SEEK_SET);
        fwrite(&fresh, sizeof(fresh), 1, img);
        fflush(img);
    }
}

/* ── cmd_rm lives in directory.c ─────────────────────────────── */