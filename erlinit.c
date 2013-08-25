#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
    printf("Loading erlang shell...\n");

    char * const *envvar = erlenv;
    while (*envvar != NULL)
        putenv(*envvar++);
    execvp("/usr/lib/erlang/erts-" ERTS_VERSION "/bin/erlexec", erlargv);

    // execvpe is not supposed to return
    perror("execvp");

    // oops the kernel
    return 0;
}
