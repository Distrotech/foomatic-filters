
#ifndef process_h
#define process_h

int start_process(const char *name, int (*proc_func)(), int *fdin, int *fdout);
int start_system_process(const char *name, const char *command, int *fdin, int *fdout);

/* returns command's return status (see waitpid(2)) */
int run_system_process(const char *command);

int wait_for_process(int pid);

void kill_all_processes();

#endif

