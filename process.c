
#include "foomaticrip.h"
#include "process.h"
#include <unistd.h>
#include "util.h"
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>

int kidgeneration = 0;

struct process {
    char name[32];
    pid_t pid;
    int isgroup;

    struct process *next;
};

struct process *proclist = NULL;

void add_proc(const char *name, int pid, int isgroup)
{
    struct process *proc = malloc(sizeof(struct process));
    strlcpy(proc->name, name, 32);
    proc->pid = pid;
    proc->isgroup = isgroup;

    proc->next = proclist;
    proclist = proc;
}

void clear_proc_list()
{
    struct process *proc = proclist;
    while (proc) {
        proc = proclist->next;
        free(proclist);
        proclist = proc;
    }
}

struct process * take_process(int pid)
{
    struct process *proc = proclist;
    struct process *prev = NULL;

    while (proc) {
        if (proc->pid == pid) {
            if (prev)
                prev->next = proc->next;
            else
                proclist = proc->next;
            return proc;
        }
        prev = proc;
        proc = proc->next;
    }
    return NULL;
}

static int _start_process(const char *name, int (*proc_func)(), void *arg, int *fdin, int *fdout, int createprocessgroup)
{
    pid_t pid;
    int pfdin[2], pfdout[2];
    int ret;

    if (fdin) {
        pipe(pfdin);
        *fdin = pfdin[1];
    }
    if (fdout) {
        pipe(pfdout);
        *fdout = pfdout[0];
    }

    _log("Starting process \"%s\" (generation %d)\n", name, kidgeneration +1);

    pid = fork();
    if (pid < 0) {
        _log("Could not fork for %s\n", name);
        if (fdin) {
            close(pfdin[0]);
            close(pfdin[1]);
        }
        if (fdout) {
            close(pfdout[0]);
            close(pfdout[1]);
        }
        return -1;
    }

    if (pid == 0) { /* Child */
        if (fdin) {
            close(pfdin[1]);
            if (dup2(pfdin[0], STDIN_FILENO) < 0) {
                _log("%s: Could not dup stdin\n", name);
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }
        }
        if (fdout) {
            close(pfdout[0]);
            if (dup2(pfdout[1], STDOUT_FILENO) < 0) {
                _log("%s: Could not dup stdout\n", name);
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }
        }

        if (createprocessgroup)
            setpgid(0, 0);

        kidgeneration++;

        /* The subprocess list is only valid for the parent. Clear it. */
        clear_proc_list();

        if (arg)
            ret = proc_func(arg);
        else
            ret = proc_func();
        exit(ret);
    }

    /* Parent */
    if (fdin)
        close(pfdin[0]);
    if (fdout)
        close(pfdout[1]);

    /* Add the child process to the list of open processes (to be able to kill
     * them in case of a signal. */
    add_proc(name, pid, createprocessgroup);

    return pid;
}

int exec_command(const char *cmd)
{
    execl(get_modern_shell(), get_modern_shell(), "-c", cmd, (char *)NULL);

    _log("Error: Executing \"%s -c %s\" failed (%s).\n", get_modern_shell(), cmd, strerror(errno));
    return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
}

int start_system_process(const char *name, const char *command, int *fdin, int *fdout)
{
    return _start_process(name, exec_command, (void*)command, fdin, fdout, 1);
}

int start_process(const char *name, int (*proc_func)(), int *fdin, int *fdout)
{
    return _start_process(name, proc_func, NULL, fdin, fdout, 0);
}

int wait_for_process(int pid)
{
    struct process *proc;
    int status;

    proc = take_process(pid);
    if (!proc) {
        _log("No such process \"%d\"", pid);
        return -1;
    }

    waitpid(proc->pid, &status, 0);
    if (WIFEXITED(status))
        _log("%s exited with status %d\n", proc->name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        _log("%s received signal %d\n", proc->name, WTERMSIG(status));
    return status;
}

void kill_all_processes()
{
    struct process *proc = proclist;

    while (proc) {
        _log("Killing %s\n", proc->name);
        kill(proc->isgroup ? -proc->pid : proc->pid, 15);
        sleep(1 << (3 - kidgeneration));
        kill(proc->isgroup ? -proc->pid : proc->pid, 9);
        proc = proc->next;
    }
    clear_proc_list();
}

int run_system_process(const char *command)
{
    int pid = start_system_process(command, command, NULL, NULL);
    return wait_for_process(pid);
}

