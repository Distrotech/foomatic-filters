
#ifndef process_h
#define process_h

#include <stdio.h>
#include <sys/types.h>

pid_t start_process(const char *name, int (*proc_func)(), FILE **fdin, FILE **fdout);
pid_t start_system_process(const char *name, const char *command, FILE **fdin, FILE **fdout);

/* returns command's return status (see waitpid(2)) */
int run_system_process(const char *name, const char *command);

int wait_for_process(int pid);

void kill_all_processes();

#endif

