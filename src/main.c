#include <stdio.h>
#include <stdlib.h>
#include "fat32.h"
#include "shell.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [FAT32 image]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Open the image and parse the boot sector */
    if (fat32_open(argv[1]) != 0) {
        return EXIT_FAILURE;
    }

    /* Hand control to the shell REPL */
    shell_run();

    /* Clean up on exit */
    fat32_close();
    return EXIT_SUCCESS;
}