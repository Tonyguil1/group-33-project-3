#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fat32.h"
#include "shell.h"
#include "directory.h"
#include "file.h"
#include "file.h"

/* ── format_name ─────────────────────────────────────────────── */
/* Converts the 11-byte on-disk FAT name to a printable string.   */
/* Examples:                                                       */
/*   "DOCS       "  "DOCS"                                       */
/*   "README  TXT"  "README.TXT"                                 */
/*   ".          "  "."                                          */
/*   "..         "  ".."                                         */
void format_name(const uint8_t raw[11], char *out) {
    char name[9], ext[4];
    int  i, j;

    /* Copy the 8-byte name part, stopping at first space */
    for (i = 0; i < 8 && raw[i] != ' '; i++)
        name[i] = (char)raw[i];
    name[i] = '\0';

    /* Copy the 3-byte extension part, stopping at first space */
    for (j = 0; j < 3 && raw[8 + j] != ' '; j++)
        ext[j] = (char)raw[8 + j];
    ext[j] = '\0';

    /* Re-join with a dot only if extension is non-empty */
    if (ext[0] == '\0')
        strcpy(out, name);
    else
        sprintf(out, "%s.%s", name, ext);
}

/* ── encode_name ─────────────────────────────────────────────── */
/* Converts a user-typed name to the 11-byte 8.3 form.            */
/* Per the spec, user input won't contain extensions, so we copy  */
/* up to 8 chars uppercased and space-pad the rest.               */
void encode_name(const char *in, uint8_t out[11]) {
    memset(out, ' ', 11);

    /* "." and ".." are stored verbatim, padded with spaces */
    if (strcmp(in, ".") == 0)  { out[0] = '.'; return; }
    if (strcmp(in, "..") == 0) { out[0] = '.'; out[1] = '.'; return; }

    size_t len = strlen(in);
    if (len > 8) len = 8;          /* truncate per assumption */
    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t)toupper((unsigned char)in[i]);
}

/* ── walk_directory_helpers ──────────────────────────────────── */
/* Both cmd_ls and find_entry use the same iteration pattern:     */
/*   for each cluster in the chain                                */
/*     for each 32-byte entry in the cluster                      */
/*       handle 0x00 = end-of-dir, 0xE5 = deleted, LFN = skip    */
/* We just inline it in each function for clarity.                */

/* ── find_entry ──────────────────────────────────────────────── */
int find_entry(uint32_t start_cluster, const char *name,
               DirEntry_t *entry_out, long *entry_offset) {
    uint8_t target[11];
    encode_name(name, target);

    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;
    uint32_t entries_per  = cluster_size / sizeof(DirEntry_t);
    uint32_t cluster      = start_cluster;

    while (cluster < FAT_EOC) {
        long base = cluster_to_offset(cluster);

        for (uint32_t i = 0; i < entries_per; i++) {
            DirEntry_t e;
            long off = base + i * (long)sizeof(DirEntry_t);

            fseek(img, off, SEEK_SET);
            if (fread(&e, sizeof(e), 1, img) != 1) return -1;

            if (e.DIR_Name[0] == 0x00) return -1;        /* end of directory */
            if (e.DIR_Name[0] == 0xE5) continue;          /* deleted slot    */
            if ((e.DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;

            if (memcmp(e.DIR_Name, target, 11) == 0) {
                *entry_out = e;
                if (entry_offset) *entry_offset = off;
                return 0;
            }
        }
        cluster = next_cluster(cluster);
    }
    return -1;
}

/* ── cmd_ls ──────────────────────────────────────────────────── */
/* Prints every visible entry in the current directory.           */
void cmd_ls(void) {
    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;
    uint32_t entries_per  = cluster_size / sizeof(DirEntry_t);
    uint32_t cluster      = cwd_cluster;

    while (cluster < FAT_EOC) {
        long base = cluster_to_offset(cluster);

        for (uint32_t i = 0; i < entries_per; i++) {
            DirEntry_t e;
            fseek(img, base + i * (long)sizeof(DirEntry_t), SEEK_SET);
            if (fread(&e, sizeof(e), 1, img) != 1) return;

            if (e.DIR_Name[0] == 0x00) return;          /* end of directory */
            if (e.DIR_Name[0] == 0xE5) continue;         /* deleted          */
            if ((e.DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
            if (e.DIR_Attr & ATTR_VOLUME_ID) continue;   /* skip volume label */

            char name[13];
            format_name(e.DIR_Name, name);
            printf("%s\n", name);
        }
        cluster = next_cluster(cluster);
    }
}

/* ── find_or_alloc_free_slot ─────────────────────────────────── */
/* Walk the chain looking for an entry whose first byte is 0x00   */
/* (never used) or 0xE5 (deleted). If we exhaust the chain, grow  */
/* it by one cluster and return the first slot of the new cluster.*/
long find_or_alloc_free_slot(uint32_t start_cluster) {
    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;
    uint32_t entries_per  = cluster_size / sizeof(DirEntry_t);
    uint32_t cluster      = start_cluster;
    uint32_t last_cluster = start_cluster;

    while (cluster < FAT_EOC) {
        long base = cluster_to_offset(cluster);

        for (uint32_t i = 0; i < entries_per; i++) {
            uint8_t first;
            long    off = base + i * (long)sizeof(DirEntry_t);

            fseek(img, off, SEEK_SET);
            if (fread(&first, 1, 1, img) != 1) return -1;

            if (first == 0x00 || first == 0xE5)
                return off;
        }
        last_cluster = cluster;
        cluster      = next_cluster(cluster);
    }

    /* Chain is full — extend it with a fresh cluster. */
    uint32_t new_clus = alloc_cluster();
    if (new_clus == 0) return -1;

    write_fat_entry(last_cluster, new_clus);   /* link old → new */
    return cluster_to_offset(new_clus);        /* first slot of new cluster */
}

/* ── make_dir_entry ──────────────────────────────────────────── */
/* Internal helper: builds and writes a 32-byte directory entry   */
/* at the given offset in the image.                              */
static void make_dir_entry(long offset, const uint8_t name[11],
                           uint8_t attr, uint32_t first_cluster,
                           uint32_t size) {
    DirEntry_t e;
    memset(&e, 0, sizeof(e));

    memcpy(e.DIR_Name, name, 11);
    e.DIR_Attr      = attr;
    e.DIR_FstClusHI = (first_cluster >> 16) & 0xFFFF;
    e.DIR_FstClusLO =  first_cluster        & 0xFFFF;
    e.DIR_FileSize  = size;

    fseek(img, offset, SEEK_SET);
    fwrite(&e, sizeof(e), 1, img);
    fflush(img);
}

/* ── cmd_creat ───────────────────────────────────────────────── */
/* Creates a 0-byte file in the current working directory.        */
void cmd_creat(const char *name) {
    DirEntry_t existing;

    /* Refuse to clobber an existing entry. */
    if (find_entry(cwd_cluster, name, &existing, NULL) == 0) {
        fprintf(stderr, "Error: '%s' already exists\n", name);
        return;
    }

    long slot = find_or_alloc_free_slot(cwd_cluster);
    if (slot < 0) {
        fprintf(stderr, "Error: no free directory slot (volume full?)\n");
        return;
    }

    uint8_t enc[11];
    encode_name(name, enc);
    make_dir_entry(slot, enc, ATTR_ARCHIVE, /*cluster=*/0, /*size=*/0);
}

/* ── cmd_mkdir ───────────────────────────────────────────────── */
/* Creates a new subdirectory in the current working directory.   */
/*                                                                 */
/* Steps:                                                          */
/*   1. Reject if the name already exists.                        */
/*   2. Allocate one cluster to hold the new directory's entries. */
/*   3. Initialize that cluster with "." and ".." entries.        */
/*   4. Write the dirname entry into a free slot in the parent.   */
void cmd_mkdir(const char *name) {
    DirEntry_t existing;

    if (find_entry(cwd_cluster, name, &existing, NULL) == 0) {
        fprintf(stderr, "Error: '%s' already exists\n", name);
        return;
    }

    long slot = find_or_alloc_free_slot(cwd_cluster);
    if (slot < 0) {
        fprintf(stderr, "Error: no free directory slot (volume full?)\n");
        return;
    }

    /* Allocate a cluster for the new directory's contents. */
    uint32_t new_clus = alloc_cluster();
    if (new_clus == 0) {
        fprintf(stderr, "Error: no free clusters available\n");
        return;
    }

    /* ── Write "." and ".." inside the new directory ──────── */
    long base = cluster_to_offset(new_clus);

    uint8_t dot_name[11]    = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    uint8_t dotdot_name[11] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};

    /* "."  → points at the new directory itself */
    make_dir_entry(base, dot_name, ATTR_DIRECTORY, new_clus, 0);

    /* ".." → points at parent. FAT32 quirk: if parent is the    */
    /* root, store cluster 0 instead of BPB_RootClus.            */
    uint32_t parent_clus = (cwd_cluster == bpb.BPB_RootClus) ? 0 : cwd_cluster;
    make_dir_entry(base + sizeof(DirEntry_t), dotdot_name,
                   ATTR_DIRECTORY, parent_clus, 0);

    /* ── Write the dirname entry into the parent ─────────── */
    uint8_t enc[11];
    encode_name(name, enc);
    make_dir_entry(slot, enc, ATTR_DIRECTORY, new_clus, 0);
}

/* ── cmd_cd ──────────────────────────────────────────────────── */
/* Changes the working directory after validating the target.    */
void cmd_cd(const char *name) {
    DirEntry_t e;
    if (find_entry(cwd_cluster, name, &e, NULL) != 0) {
        fprintf(stderr, "Error: directory '%s' does not exist\n", name);
        return;
    }
    if (!(e.DIR_Attr & ATTR_DIRECTORY)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", name);
        return;
    }

    uint32_t target = get_first_cluster(&e);

    /* FAT32 quirk: ".." in a top-level subdir stores cluster 0,  */
    /* meaning "the root". Translate that back to the real root.  */
    if (target == 0) target = bpb.BPB_RootClus;

    cwd_cluster = target;

    /* ── Update the displayed path ─────────────────────────── */
    if (strcmp(name, ".") == 0) {
        /* "." → no change */
    } else if (strcmp(name, "..") == 0) {
        /* ".." → pop last path component */
        char *slash = strrchr(cwd_path, '/');
        if (slash && slash != cwd_path) {
            *slash = '\0';                /* trim "/foo" off "/foo/bar" */
        } else {
            strcpy(cwd_path, "/");        /* already at top, stay at /  */
        }
    } else {
        /* Append "/name", avoiding a double slash at the root */
        char tmp[2048];
        if (strcmp(cwd_path, "/") == 0)
            snprintf(tmp, sizeof(tmp), "/%s", name);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", cwd_path, name);
        strncpy(cwd_path, tmp, sizeof(cwd_path) - 1);
        cwd_path[sizeof(cwd_path) - 1] = '\0';
    }
}

/* ────────────────────────────────────────────────────────────── */
/* (find_open_by_raw is provided by file.h)                       */

/* ── cmd_mv ──────────────────────────────────────────────────── */
/* Two modes:                                                     */
/*   • If `dst` exists as a directory  move `src` into it       */
/*     (keeping its name).                                       */
/*   • If `dst` does not exist         rename `src` to `dst`.   */
/*   • If `dst` exists as a file       error.                   */
/*                                                                 */
/* When moving a directory, we also rewrite its ".." entry so it */
/* points at the new parent (or 0 if the new parent is root).    */
void cmd_mv(const char *src, const char *dst) {
    DirEntry_t src_e;
    long       src_off;
    if (find_entry(cwd_cluster, src, &src_e, &src_off) != 0) {
        fprintf(stderr, "Error: '%s' does not exist\n", src);
        return;
    }

    /* Files must be closed before being moved. (Directories have*/
    /* no "open" concept in this shell.)                          */
    if (!(src_e.DIR_Attr & ATTR_DIRECTORY)
        && find_open_by_raw(cwd_cluster, src_e.DIR_Name) >= 0) {
        fprintf(stderr,
            "Error: '%s' is opened — close it first\n", src);
        return;
    }

    DirEntry_t dst_e;
    long       dst_off;
    int dst_exists = (find_entry(cwd_cluster, dst, &dst_e, &dst_off) == 0);

    if (dst_exists) {
        /* Refuse to clobber a file. */
        if (!(dst_e.DIR_Attr & ATTR_DIRECTORY)) {
            fprintf(stderr,
                "Error: '%s' is a file, not a directory\n", dst);
            return;
        }

        /* Same on-disk slot? Nothing to do. */
        if (dst_off == src_off) return;

        /* ── Move src's entry into dst directory ────────────── */
        uint32_t dst_clus = get_first_cluster(&dst_e);
        if (dst_clus == 0) dst_clus = bpb.BPB_RootClus;  /* ".." quirk */

        /* Refuse if the destination already contains an entry    */
        /* with the same name as the source.                      */
        DirEntry_t conflict;
        char src_name_str[13];
        format_name(src_e.DIR_Name, src_name_str);
        if (find_entry(dst_clus, src_name_str, &conflict, NULL) == 0) {
            fprintf(stderr,
                "Error: '%s' already exists in destination\n", src_name_str);
            return;
        }

        long new_slot = find_or_alloc_free_slot(dst_clus);
        if (new_slot < 0) {
            fprintf(stderr, "Error: destination directory is full\n");
            return;
        }

        /* Copy the source entry verbatim into the new slot. */
        fseek (img, new_slot, SEEK_SET);
        fwrite(&src_e, sizeof(src_e), 1, img);

        /* Mark the old slot as deleted. */
        uint8_t tombstone = 0xE5;
        fseek (img, src_off, SEEK_SET);
        fwrite(&tombstone, 1, 1, img);

        /* If we moved a real subdirectory (not "." or ".."),    */
        /* fix up its ".." to point at the new parent.           */
        int is_dot    = (memcmp(src_e.DIR_Name,
                                (uint8_t[]){'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
                                11) == 0);
        int is_dotdot = (memcmp(src_e.DIR_Name,
                                (uint8_t[]){'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '},
                                11) == 0);
        if ((src_e.DIR_Attr & ATTR_DIRECTORY) && !is_dot && !is_dotdot) {
            uint32_t src_first    = get_first_cluster(&src_e);
            uint32_t parent_value = (dst_clus == bpb.BPB_RootClus)
                                    ? 0 : dst_clus;

            /* The ".." entry is the second 32-byte slot of the   */
            /* directory's first cluster.                         */
            long dotdot_off = cluster_to_offset(src_first) + sizeof(DirEntry_t);
            DirEntry_t dotdot;
            fseek(img, dotdot_off, SEEK_SET);
            fread(&dotdot, sizeof(dotdot), 1, img);
            dotdot.DIR_FstClusHI = (parent_value >> 16) & 0xFFFF;
            dotdot.DIR_FstClusLO =  parent_value        & 0xFFFF;
            fseek (img, dotdot_off, SEEK_SET);
            fwrite(&dotdot, sizeof(dotdot), 1, img);
        }

        fflush(img);
    } else {
        /* ── Rename: just rewrite DIR_Name in place ─────────── */
        uint8_t new_name[11];
        encode_name(dst, new_name);

        DirEntry_t fresh = src_e;
        memcpy(fresh.DIR_Name, new_name, 11);

        fseek (img, src_off, SEEK_SET);
        fwrite(&fresh, sizeof(fresh), 1, img);
        fflush(img);
    }
}

/* ── is_directory_empty ──────────────────────────────────────── */
/* Returns 1 if the directory at `start_cluster` contains only   */
/* "." and ".." entries, 0 otherwise.                            */
static int is_directory_empty(uint32_t start_cluster) {
    uint32_t cluster_size = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;
    uint32_t entries_per  = cluster_size / sizeof(DirEntry_t);
    uint32_t cluster      = start_cluster;

    static const uint8_t DOT[11]    =
        {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t DOTDOT[11] =
        {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};

    while (cluster < FAT_EOC) {
        long base = cluster_to_offset(cluster);
        for (uint32_t i = 0; i < entries_per; i++) {
            DirEntry_t ent;
            fseek(img, base + i * (long)sizeof(DirEntry_t), SEEK_SET);
            if (fread(&ent, sizeof(ent), 1, img) != 1) return 1;

            if (ent.DIR_Name[0] == 0x00) return 1;       /* end-of-dir */
            if (ent.DIR_Name[0] == 0xE5) continue;        /* deleted    */
            if ((ent.DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
            if (memcmp(ent.DIR_Name, DOT,    11) == 0)   continue;
            if (memcmp(ent.DIR_Name, DOTDOT, 11) == 0)   continue;

            return 0;  /* found a real entry */
        }
        cluster = next_cluster(cluster);
    }
    return 1;
}

/* ── cmd_rmdir ───────────────────────────────────────────────── */
void cmd_rmdir(const char *name) {
    /* Refuse to delete "." or ".." outright. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        fprintf(stderr,
            "Error: cannot remove '.' or '..'\n");
        return;
    }

    DirEntry_t e;
    long       entry_off;
    if (find_entry(cwd_cluster, name, &e, &entry_off) != 0) {
        fprintf(stderr, "Error: '%s' does not exist\n", name);
        return;
    }
    if (!(e.DIR_Attr & ATTR_DIRECTORY)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", name);
        return;
    }

    uint32_t dir_clus = get_first_cluster(&e);

    /* Defensive: even though an empty directory cannot have any  */
    /* open files, the spec calls out this check explicitly.      */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].in_use
            && open_files[i].parent_cluster == dir_clus) {
            fprintf(stderr,
                "Error: directory '%s' has open files\n", name);
            return;
        }
    }

    if (!is_directory_empty(dir_clus)) {
        fprintf(stderr, "Error: directory '%s' is not empty\n", name);
        return;
    }

    /* Free the directory's data clusters and tombstone its entry.*/
    free_cluster_chain(dir_clus);

    uint8_t tombstone = 0xE5;
    fseek (img, entry_off, SEEK_SET);
    fwrite(&tombstone, 1, 1, img);
    fflush(img);
}

/* ── update_entry_size ───────────────────────────────────────── */
/* Patch the DIR_FileSize field of an existing on-disk entry.    */
/* Used by `write` whenever it extends a file beyond its old end. */
void update_entry_size(long entry_offset, uint32_t size) {
    fseek (img, entry_offset + 28, SEEK_SET);   /* offset of DIR_FileSize */
    fwrite(&size, 4, 1, img);
    fflush(img);
}

/* ── update_entry_cluster ────────────────────────────────────── */
/* Patch the high+low halves of DIR_FstClus for an entry.        */
/* Used by `write` when first allocating data for an empty file. */
void update_entry_cluster(long entry_offset, uint32_t cluster) {
    uint16_t hi = (cluster >> 16) & 0xFFFF;
    uint16_t lo =  cluster        & 0xFFFF;

    fseek (img, entry_offset + 20, SEEK_SET);   /* DIR_FstClusHI */
    fwrite(&hi, 2, 1, img);
    fseek (img, entry_offset + 26, SEEK_SET);   /* DIR_FstClusLO */
    fwrite(&lo, 2, 1, img);
    fflush(img);
}

/* ── free_cluster_chain lives in fat32.c ─────────────────────── */

/* ── cmd_rm ──────────────────────────────────────────────────── */
/* Remove a regular file: free its clusters, tombstone its entry. */
void cmd_rm(const char *name) {
    DirEntry_t e;
    long       entry_off;
    if (find_entry(cwd_cluster, name, &e, &entry_off) != 0) {
        fprintf(stderr, "Error: '%s' does not exist\n", name);
        return;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        fprintf(stderr,
            "Error: '%s' is a directory (use rmdir)\n", name);
        return;
    }
    if (find_open_by_raw(cwd_cluster, e.DIR_Name) >= 0) {
        fprintf(stderr,
            "Error: '%s' is opened — close it first\n", name);
        return;
    }

    /* A 0-byte file may have first_cluster == 0 (no chain to free).*/
    free_cluster_chain(get_first_cluster(&e));

    uint8_t tombstone = 0xE5;
    fseek (img, entry_off, SEEK_SET);
    fwrite(&tombstone, 1, 1, img);
    fflush(img);
}