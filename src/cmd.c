// SPDX-FileCopyrightText: 2018 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

int system_cmd(const char *cmd, char *output_buffer, int length)
{
    elog(ELOG_DEBUG, "system_cmd '%s'", cmd);
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        elog(ELOG_ERROR, "pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child
        int devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) {
            elog(ELOG_ERROR, "Couldn't open /dev/null");
            exit(EXIT_FAILURE);
        }

        close(pipefd[0]); // no reads from the pipe
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        if (dup2(devnull, STDIN_FILENO) < 0)
            elog(ELOG_ERROR, "dup2 devnull");
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            elog(ELOG_ERROR, "dup2 pipe");
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
        elog(ELOG_ERROR, "execvp '%s' failed", cmd);
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
        for (;;) {
            pid_t rc = waitpid(pid, &status, 0);
            if (rc == pid)
                break;
            if (rc != -1 || errno != EINTR) {
                elog(ELOG_ERROR, "waitpid failed for '%s': %d", cmd, errno);
                return -1;
            }
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            elog(ELOG_ERROR, "'%s' didn't exit", cmd);
            return -1;
        }
    }
}
