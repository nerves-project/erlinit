#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define ERTS_VERSION "5.9.3.1"

static char * const erlargv[] = {
    "erlexec",
    NULL
};

static char * const erlenv[] = {
    // General environment
    "HOME=/",
    // PATH appears to only be needed for user convenience when runninf os:cmd/1
    // It may be possible to remove in the future.
    "PATH=/usr/sbin:/usr/bin:/sbin:/bin",
    "TERM=vt100",

    // Erlang environment
    "ROOTDIR=/usr/lib/erlang",
    "BINDIR=/usr/lib/erlang/erts-" ERTS_VERSION "/bin",
    "EMU=beam",
    "PROGNAME=erl",

    NULL
};

static int readsysfs(const char *path, char *buffer, int maxlen)
{

    int fd = open(path, O_RDONLY);
    if (fd < 0) 
        return 0;

    int count = read(fd, buffer, maxlen - 1);
    
    close(fd);
    if (count <= 0)
        return 0;

    // Trim trailing \n
    count--;
    buffer[count] = 0;
    return count;
}

static void fix_ctty()
{
    // Set up a controlling terminal for Erlang so that
    // it's possible to get to shell management mode.
    // See http://www.busybox.net/FAQ.html#job_control
    setsid();

    char ttypath[32];
    strcpy(ttypath, "/dev/");
    readsysfs("/sys/class/tty/console/active", &ttypath[5], sizeof(ttypath) - 5);
   
    int fd = open(ttypath, O_RDWR);
    if (fd > 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else {
        fprintf(stderr, "Error setting controlling terminal: %s\n", ttypath);
    }
}

static void child()
{
    // Set up the environment for running erlang.
    char * const *envvar = erlenv;
    while (*envvar != NULL)
        putenv(*envvar++);

    // Mount the virtual file systems.
    mount("", "/proc", "proc", 0, NULL);
    mount("", "/sys", "sysfs", 0, NULL);

    fix_ctty();

    // Start Erlang up
    execvp("/usr/lib/erlang/erts-" ERTS_VERSION "/bin/erlexec", erlargv);

    // execvpe is not supposed to return
    perror("execvp");
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "Loading erlang shell...\n");

    pid_t pid = fork();
    if (pid == 0) {
        child();
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
