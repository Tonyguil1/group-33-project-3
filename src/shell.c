#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"
#include "shell.h"
#include "directory.h"
#include "file.h"

/* Maximum length of one input line. Generous so that a `write`  */
/* with a long quoted STRING fits comfortably on one line.        */
#define MAX_INPUT 8192

/* ── Current working directory state ────────────────────────── */
/* cwd_cluster: which cluster the current directory starts at    */
/* cwd_path:    display path shown in the prompt  e.g. "/docs"   */
uint32_t cwd_cluster;
char     cwd_path[1024];

/* ── shell_init ──────────────────────────────────────────────── */
/* Called once after the image is opened. Sets cwd to root.      */
void shell_init(void) {
    cwd_cluster = bpb.BPB_RootClus;
    strcpy(cwd_path, "/");
}

/* ── print_prompt ────────────────────────────────────────────── */
/* Prints:  imagename/path/>                                      */
static void print_prompt(void) {
    /* cwd_path starts with '/' so avoid printing it twice       */
    if (strcmp(cwd_path, "/") == 0)
        printf("%s/>", img_name);
    else
        printf("%s%s>", img_name, cwd_path);
    fflush(stdout);
}

/* ── parse_args ──────────────────────────────────────────────── */
/* Splits a line into tokens stored in argv[], returns argc.     */
/* Handles quoted strings (for the write command's "STRING").    */
static int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p  = line;

    while (*p && argc < max_args) {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            /* quoted token – include everything until closing " */
            p++;                     /* skip opening quote        */
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';  /* terminate & skip "    */
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

/* ── dispatch ────────────────────────────────────────────────── */
/* Matches the command string and calls the right function.      */
/* Returns 0 to keep running, 1 to exit the shell loop.         */
static int dispatch(int argc, char *argv[]) {
    if (argc == 0) return 0;  /* blank line – do nothing */

    const char *cmd = argv[0];

    /* ── Part 1 ── */
    if (strcmp(cmd, "info") == 0) {
        if (argc != 1) { fprintf(stderr, "Usage: info\n"); return 0; }
        cmd_info();

    } else if (strcmp(cmd, "exit") == 0) {
        return 1;  /* signal the loop to stop */

    /* ── Part 2 ── */
    } else if (strcmp(cmd, "ls") == 0) {
        if (argc != 1) { fprintf(stderr, "Usage: ls\n"); return 0; }
        cmd_ls();

    } else if (strcmp(cmd, "cd") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: cd [DIRNAME]\n"); return 0; }
        cmd_cd(argv[1]);

    /* ── Part 3 ── */
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: mkdir [DIRNAME]\n"); return 0; }
        cmd_mkdir(argv[1]);

    } else if (strcmp(cmd, "creat") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: creat [FILENAME]\n"); return 0; }
        cmd_creat(argv[1]);

    /* ── Part 4 ── */
    } else if (strcmp(cmd, "open") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: open [FILENAME] [FLAGS]\n"); return 0; }
        cmd_open(argv[1], argv[2]);

    } else if (strcmp(cmd, "close") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: close [FILENAME]\n"); return 0; }
        cmd_close(argv[1]);

    } else if (strcmp(cmd, "lsof") == 0) {
        if (argc != 1) { fprintf(stderr, "Usage: lsof\n"); return 0; }
        cmd_lsof();

    } else if (strcmp(cmd, "lseek") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: lseek [FILENAME] [OFFSET]\n"); return 0; }
        cmd_lseek(argv[1], (uint32_t)strtoul(argv[2], NULL, 10));

    } else if (strcmp(cmd, "read") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: read [FILENAME] [SIZE]\n"); return 0; }
        cmd_read(argv[1], (uint32_t)strtoul(argv[2], NULL, 10));

    /* ── Part 5 ── */
    } else if (strcmp(cmd, "write") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: write [FILENAME] \"STRING\"\n"); return 0; }
        cmd_write(argv[1], argv[2]);

    } else if (strcmp(cmd, "mv") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: mv [SRC] [DST]\n"); return 0; }
        cmd_mv(argv[1], argv[2]);

    /* ── Part 6 ── */
    } else if (strcmp(cmd, "rm") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: rm [FILENAME]\n"); return 0; }
        cmd_rm(argv[1]);

    } else if (strcmp(cmd, "rmdir") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: rmdir [DIRNAME]\n"); return 0; }
        cmd_rmdir(argv[1]);

    /* ── Catch-all ── */
    } else {
        fprintf(stderr, "Error: unknown command '%s'\n", cmd);
    }

    return 0;
}

/* ── shell_run ───────────────────────────────────────────────── */
/* The main REPL loop. Reads a line, parses it, dispatches it.   */
void shell_run(void) {
    char   line[MAX_INPUT];
    char  *argv[32];

    shell_init();

    while (1) {
        print_prompt();

        /* fgets returns NULL on EOF (Ctrl-D) – treat as exit */
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        /* Strip the trailing newline */
        line[strcspn(line, "\n")] = '\0';

        int argc = parse_args(line, argv, 32);

        if (dispatch(argc, argv) == 1) break;
    }
}