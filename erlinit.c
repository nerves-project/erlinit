#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ERTS_VERSION "5.9.3.1"

char * const erlargv[] = {
    "erlexec",
    NULL
};

char * const erlenv[] = {
    // General environment
    "HOME=/",
    // PATH appears to only be needed for user convenience when runninf os:cmd/1
    // It may be possible to remove in the future.
    "PATH=/usr/sbin:/usr/bin:/sbin:/bin",

    // Erlang environment
    "ROOTDIR=/usr/lib/erlang",
    "BINDIR=/usr/lib/erlang/erts-" ERTS_VERSION "/bin",
    "EMU=beam",
    "PROGNAME=erl",

    NULL
};

int main(int argc, char *argv[])
{
    fprintf(stderr, "Loading erlang shell...\n");

    pid_t pid = fork();
    if (pid == 0) {
        // Set up the environment for running erlang
        char * const *envvar = erlenv;
        while (*envvar != NULL)
            putenv(*envvar++);

        execvp("/usr/lib/erlang/erts-" ERTS_VERSION "/bin/erlexec", erlargv);

        // execvpe is not supposed to return
        perror("execvp");
        exit(1);
    }

    // If Erlang exists, then something went wrong, so handle it.
    pid_t waitpid;
    do {
        waitpid = wait(NULL);
    } while (waitpid > 0 && waitpid != pid);

    if (waitpid != pid)
        fprintf(stderr, "Unexpected return from wait(): %d\n", waitpid);

    // Reboot rather than hanging.
    reboot(LINUX_REBOOT_CMD_RESTART);

    // If we can't reboot, oops the kernel.
    return 0;
}
