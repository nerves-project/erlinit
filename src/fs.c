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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

static unsigned long str_to_mountflags(char *s)
{
    unsigned long flags = 0;

    // These names correspond to ones used by mount(8)
    char *flag = strtok(s, ",");
    while (flag) {
        if (strcmp(flag, "dirsync") == 0)
            flags |= MS_DIRSYNC;
        else if (strcmp(flag, "mand") == 0)
            flags |= MS_MANDLOCK;
        else if (strcmp(flag, "noatime") == 0)
            flags |= MS_NOATIME;
        else if (strcmp(flag, "nodev") == 0)
            flags |= MS_NODEV;
        else if (strcmp(flag, "nodiratime") == 0)
            flags |= MS_NODIRATIME;
        else if (strcmp(flag, "noexec") == 0)
            flags |= MS_NOEXEC;
        else if (strcmp(flag, "nosuid") == 0)
            flags |= MS_NOSUID;
        else if (strcmp(flag, "ro") == 0)
            flags |= MS_RDONLY;
        else if (strcmp(flag, "relatime") == 0)
            flags |= MS_RELATIME;
        else if (strcmp(flag, "silent") == 0)
            flags |= MS_SILENT;
        else if (strcmp(flag, "strictatime") == 0)
            flags |= MS_STRICTATIME;
        else if (strcmp(flag, "sync") == 0)
            flags |= MS_SYNCHRONOUS;
        else
            warn("Unrecognized filesystem mount flag: %s", flag);

        flag = strtok(NULL, ",");
    }
    return flags;
}

void setup_pseudo_filesystems()
{
    // Since we can't mount anything in this environment, just return.
    if (options.regression_test_mode)
        return;

    if (mount("", "/proc", "proc", 0, NULL) < 0)
        warn("Cannot mount /proc");
    if (mount("", "/sys", "sysfs", 0, NULL) < 0)
        warn("Cannot mount /sys");

    // /dev should be automatically created/mounted by Linux
    if (mkdir("/dev/pts", 0755) < 0)
        warn("Cannot create /dev/pts");
    if (mkdir("/dev/shm", 0755) < 0)
        warn("Cannot create /dev/shm");
    if (mount("", "/dev/pts", "devpts", 0, "gid=5,mode=620") < 0)
        warn("Cannot mount /dev/pts");
}

void setup_filesystems()
{
    // Mount /tmp and /run since they're almost always needed and it's
    // not easy to do it at the right time in Erlang.
    if (options.regression_test_mode ||
            mount("", "/tmp", "tmpfs", 0, "mode=1777,size=10%") < 0)
        warn("Could not mount tmpfs in /tmp: %s\r\n"
             "Check that tmpfs support is enabled in the kernel config.", options.regression_test_mode ? "regression test" : strerror(errno));

    if (options.regression_test_mode ||
            mount("", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755,size=5%") < 0)
        warn("Could not mount tmpfs in /run: %s", options.regression_test_mode ? "regression test" : strerror(errno));

    // Mount any filesystems specified by the user. This is best effort.
    // The user is required to figure out if anything went wrong in their
    // applications. For example, the filesystem might not be formatted
    // yet, and erlinit is not smart enough to figure that out.
    //
    // An example mount specification looks like:
    //    /dev/mmcblk0p4:/mnt:vfat::utf8
    if (options.extra_mounts) {
        char *temp = options.extra_mounts;
        const char *source = strsep(&temp, ":");
        const char *target = strsep(&temp, ":");
        const char *filesystemtype = strsep(&temp, ":");
        char *mountflags = strsep(&temp, ":");
        const char *data = strsep(&temp, ":");

        if (source && target && filesystemtype && mountflags && data) {
            unsigned long imountflags =
                    str_to_mountflags(mountflags);
            if (options.regression_test_mode ||
                    mount(source, target, filesystemtype, imountflags, data) < 0)
                warn("Cannot mount %s at %s: %s", source, target, options.regression_test_mode ? "regression test" : strerror(errno));
        } else {
            warn("Invalid parameter to -m. Expecting 5 colon-separated fields");
        }
    }
}

void unmount_all()
{
    debug("unmount_all");
    if (options.regression_test_mode)
        return;

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        warn("/proc/mounts not found");
        return;
    }

    struct mount_info {
        char source[256];
        char target[256];
        char fstype[32];
    } mounts[MAX_MOUNTS];

    char options[128];
    int freq;
    int passno;
    int i = 0;
    while (i < MAX_MOUNTS &&
           fscanf(fp, "%255s %255s %31s %127s %d %d", mounts[i].source, mounts[i].target, mounts[i].fstype, options, &freq, &passno) >= 3) {
        i++;
    }
    fclose(fp);

    // Unmount as much as we can in reverse order.
    int num_mounts = i;
    for (i = num_mounts - 1; i >= 0; i--) {
        // Whitelist directories that don't unmount or
        // remount immediately (rootfs)
        if (strcmp(mounts[i].source, "devtmpfs") == 0 ||
                strcmp(mounts[i].source, "/dev/root") == 0 ||
                strcmp(mounts[i].source, "rootfs") == 0)
            continue;

        debug("unmounting %s(%s)...", mounts[i].source, mounts[i].target);
        if (umount(mounts[i].target) < 0 && umount(mounts[i].source) < 0)
            warn("umount %s(%s) failed: %s", mounts[i].source, mounts[i].target, strerror(errno));
    }
}