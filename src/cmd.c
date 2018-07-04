/*
The MIT License (MIT)

Copyright (c) 2013-16 Frank Hunleth

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

#include "erlinit.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

int system_cmd(const char *cmd, char *output_buffer, int length)
{
    debug("system_cmd '%s'", cmd);
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        warn("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child
        int devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) {
            warn("Couldn't open /dev/null");
            exit(EXIT_FAILURE);
        }

        close(pipefd[0]); // no reads from the pipe
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        if (dup2(devnull, STDIN_FILENO) < 0)
            warn("dup2 devnull");
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            warn("dup2 pipe");
        close(devnull);

        char *cmd_copy = strdup(cmd);
        char *exec_path = strtok(cmd_copy, " ");
        char *exec_argv[16];
        int arg = 0;

        exec_argv[arg++] = exec_path;
        while ((exec_argv[arg] = strtok(NULL, " ")) != NULL)
            arg++;
        exec_argv[arg] = 0;
        if (exec_path)
            execvp(exec_path, exec_argv);

        // Not supposed to reach here.
        warn("execvp '%s' failed", cmd);
        exit(EXIT_FAILURE);
    } else {
        // parent
        close(pipefd[1]); // No writes to the pipe

        length--; // Save room for a '\0'
        int index = 0;
        int amt;
        while (index != length &&
               (amt = read(pipefd[0], &output_buffer[index], length - index)) > 0)
            index += amt;
        output_buffer[index] = '\0';
        close(pipefd[0]);

        int status;
        if (waitpid(pid, &status, 0) != pid) {
            warn("waitpid");
            return -1;
        }
        return status;
    }
}
