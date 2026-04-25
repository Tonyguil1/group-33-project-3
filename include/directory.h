#ifndef DIRECTORY_H
#define DIRECTORY_H

#include <stdint.h>
#include "fat32.h"

/* ── Name helpers ────────────────────────────────────────────── */
/* Convert raw 11-byte FAT 8.3 name → printable string.          */
/* `out` must hold at least 13 bytes (8 + '.' + 3 + '\0').       */
void format_name(const uint8_t raw[11], char *out);

/* Convert user-typed name → 11-byte FAT 8.3 format (uppercase,  */
/* space-padded). Handles "." and ".." specially.                */
void encode_name(const char *in, uint8_t out[11]);

/* ── Lookup ──────────────────────────────────────────────────── */
/* Walks the directory chain starting at `start_cluster`,         */
/* searching for an entry whose name matches `name`.              */
/*                                                                 */
/*  Returns 0 on success: *entry_out is filled,                   */
/*    *entry_offset (if non-NULL) gets the byte offset of the     */
/*    matching 32-byte entry within the image (useful for         */
/*    rm/rmdir/mv which need to overwrite the entry).             */
/*  Returns -1 if not found or on read error.                     */
int find_entry(uint32_t start_cluster, const char *name,
               DirEntry_t *entry_out, long *entry_offset);

/* ── Slot allocation ─────────────────────────────────────────── */
/* Finds a free 32-byte directory entry slot in the chain.        */
/* If the chain is full, allocates and links a new cluster.       */
/* Returns the byte offset where to write the new entry, or -1.   */
long find_or_alloc_free_slot(uint32_t start_cluster);

/* ── Directory-entry editors (used by write, mv) ─────────────── */
void update_entry_size   (long entry_offset, uint32_t size);
void update_entry_cluster(long entry_offset, uint32_t cluster);

/* ── Commands ────────────────────────────────────────────────── */
void cmd_ls(void);
void cmd_cd(const char *name);
void cmd_mkdir(const char *name);
void cmd_creat(const char *name);
void cmd_mv(const char *src, const char *dst);
void cmd_rm(const char *name);
void cmd_rmdir(const char *name);

#endif /* DIRECTORY_H */