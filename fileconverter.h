
#ifndef fileconverter_h
#define fileconverter_h

void set_fileconverter(const char *fc);

int close_fileconverter_handle(FILE *fileconverter_handle, pid_t fileconverter_pid);
void get_fileconverter_handle(const char *alreadyread, FILE **fd, pid_t *pid);

#endif

