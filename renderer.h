
#ifndef renderer_h
#define renderer_h

void get_renderer_handle(const dstr_t *prepend, FILE **fd, pid_t *pid);
int close_renderer_handle(FILE *rendererhandle, pid_t rendererpid);

#endif

