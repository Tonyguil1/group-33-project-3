#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* Current working directory state – used by navigation commands */
extern uint32_t cwd_cluster;
extern char     cwd_path[1024];

void shell_init(void);
void shell_run(void);

#endif /* SHELL_H */
