#include "exec.h"
#include "shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Convert the raw status word from waitpid() into a shell exit status. */
static int decode_wait_status(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        /* Like bash: say why a program died, except for SIGINT (the user
         * pressed Ctrl-C, they know) and SIGPIPE (normal in pipelines,
         * e.g. `yes | head`). */
        if (sig != SIGINT && sig != SIGPIPE) {
            fprintf(stderr, "%s", strsignal(sig));
#ifdef WCOREDUMP
            if (WCOREDUMP(status))
                fprintf(stderr, " (core dumped)");
#endif
            fputc('\n', stderr);
        }
        return 128 + sig;
    }
    return 1;   /* stopped/continued: not reachable until job control */
}

extern char **environ;

int exec_external(char *const argv[], char **envp)
{
    /* Flush our own stdio buffers BEFORE fork(). Otherwise any unwritten
     * output sitting in the parent's buffer is copied into the child and
     * can end up printed twice. */
    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork: %s\n", MSH_NAME, strerror(errno));
        return 1;
    }

    if (pid == 0) {
        /* ---- child ---- */
        /* Swap in the environment we built. execvp() reads PATH from
         * `environ`, so `PATH=/x cmd` also changes where cmd is searched
         * for, exactly as in bash. Only the child's copy changes. */
        environ = envp;
        execvp(argv[0], argv);

        /* execvp only returns on failure. Pick the exit code a POSIX
         * shell would use, and say why. */
        int err = errno;
        if (err == ENOENT && strchr(argv[0], '/') == NULL) {
            fprintf(stderr, "%s: %s: command not found\n", MSH_NAME, argv[0]);
            _exit(127);
        }
        fprintf(stderr, "%s: %s: %s\n", MSH_NAME, argv[0], strerror(err));
        _exit(err == ENOENT ? 127 : 126);
        /* _exit, not exit: exit() would run atexit handlers and flush
         * stdio buffers that belong to the parent's copy of the world. */
    }

    /* ---- parent ---- */
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;      /* a signal interrupted the wait: retry */
        fprintf(stderr, "%s: waitpid: %s\n", MSH_NAME, strerror(errno));
        return 1;
    }
    return decode_wait_status(status);
}
