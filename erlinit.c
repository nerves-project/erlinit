/*
The MIT License (MIT)

Copyright (c) 2013 Frank Hunleth

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define ERLANG_ROOT_DIR "/usr/lib/erlang"
#define RELEASE_ROOT_DIR "/srv/erlang"

static char * const erlargv[] = {
    "erlexec",
    NULL
};

static void info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void fatal(const char *fmt, ...)
{
    fprintf(stderr, "\n\nFATAL ERROR:\n");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\nCANNOT CONTINUE.\n");

    sleep(9999);
}

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

static int starts_with(const char *str, const char *what)
{
    return strstr(str, what) == str;
}

static int erts_filter(const struct dirent *d)
{
    return starts_with(d->d_name, "erts-");
}

static void find_erts_directory(char *path)
{
    struct dirent **namelist;
    int n = scandir(ERLANG_ROOT_DIR,
                    &namelist,
                    erts_filter,
                    NULL);
    if (n < 0)
        fatal("scandir failed: %s\n", strerror(errno));
    else if (n == 0)
        fatal("erts not found. Check that erlang was installed to %s\n", ERLANG_ROOT_DIR);
    else if (n > 1)
        fatal("Found multiple erts directories. Clean up the installation.\n");

    sprintf(path, "%s/%s", ERLANG_ROOT_DIR, namelist[0]->d_name);

    free(namelist[0]);
    free(namelist);
}

static void setup_environment(const char *erts_path, const char *release_rootdir)
{
    // Set up the environment for running erlang.
    putenv("HOME=/");

    // PATH appears to only be needed for user convenience when runninf os:cmd/1
    // It may be possible to remove in the future.
    putenv("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    putenv("TERM=vt100");

    // Erlang environment

    // ROOTDIR points to the release unless it wasn't found.
    char envvar[PATH_MAX];
    sprintf(envvar, "ROOTDIR=%s", (release_rootdir && *release_rootdir) ? release_rootdir : ERLANG_ROOT_DIR);
    putenv(envvar);

    // BINDIR points to the erts bin directory.
    sprintf(envvar, "BINDIR=%s/bin", erts_path);
    putenv(envvar);

    putenv("EMU=beam");
    putenv("PROGNAME=erl");
}

static void child()
{
    // Locate erts
    char erts_path[PATH_MAX];
    find_erts_directory(erts_path);

    // Set up the environment for running erlang.
    setup_environment(erts_path, NULL);

    // Mount the virtual file systems.
    mount("", "/proc", "proc", 0, NULL);
    mount("", "/sys", "sysfs", 0, NULL);

    // Fix the terminal settings so that CTRL keys work.
    fix_ctty();

    // Start Erlang up
    char erlexec_path[PATH_MAX];
    sprintf(erlexec_path, "%s/bin/erlexec", erts_path);
    execvp(erlexec_path, erlargv);

    // execvpe is not supposed to return
    fatal("execvp failed to run %s: %s", erlexec_path, strerror(errno));
}

int main(int argc, char *argv[])
{
    info("Loading erlang shell...\n");

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
        info("Unexpected return from wait(): %d\n", waitpid);

    fatal("Why are we here\n");

    // When erlang exits on purpose (or on accident), reboot
    reboot(LINUX_REBOOT_CMD_RESTART);

    // If we can't reboot, oops the kernel.
    return 0;
}
