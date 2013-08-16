#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char * const erlargv[] = {
    "erlexec",
    NULL
};

char * const erlenv[] = {
    // General environment
    "HOME=/",

    // Erlang environment
    "ROOTDIR=/usr/lib/erlang",
    "BINDIR=/usr/lib/erlang/erts-5.9.1/bin",
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
    execvp("/usr/lib/erlang/erts-5.9.1/bin/erlexec", erlargv);

    // execvpe is not supposed to return
    perror("execvpe");

    // oops the kernel
    return 0;
}
