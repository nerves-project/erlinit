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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
        else if (strcmp(flag, "rw") == 0)
            flags &= MS_RDONLY;
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
    // This only works in the real environment.
    OK_OR_WARN(mount("proc", "/proc", "proc", MS_NOEXEC | MS_NOSUID | MS_NODEV, NULL), "Cannot mount /proc");
    OK_OR_WARN(mount("sysfs", "/sys", "sysfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, NULL), "Cannot mount /sys");

    // /dev should be automatically created/mounted by Linux
    OK_OR_WARN(mkdir("/dev/pts", 0755), "Cannot create /dev/pts");
    OK_OR_WARN(mkdir("/dev/shm", 0755), "Cannot create /dev/shm");
    OK_OR_WARN(mount("devpts", "/dev/pts", "devpts", MS_NOEXEC | MS_NOSUID, "gid=5,mode=620"), "Cannot mount /dev/pts");
}

static dev_t root_disk_device()
{
    // NOTE: We'd like to know the root device for the root directory, but stat
    //       only returns the partition device for the root directory. Since it
    //       is often the case of 16 minor devices to support each real device,
    //       assume that if we mask the root directory's device off that we'll
    //       get its parent.
    struct stat rootdev;
    if (stat("/", &rootdev) >= 0) {
        return (rootdev.st_dev & 0xfff0);
    } else {
        warn("Could not determine root device: %s", strerror(errno));
        return 0;
    }
}

void create_rootdisk_symlinks()
{
    // This code discovers which device has the root filesystem and creates
    // symlinks to the device files for it. This is needed to avoid logic
    // elsewhere in the erlinit.config, fwup scripts, u-boot environment
    // configuration files, etc. to point to the right place on systems that
    // don't have predictable disk names. E.g., on some boots, it's /dev/sda
    // and on others, it's /dev/sdb depending on which enumerates first.

    dev_t rdev = root_disk_device();
    if (rdev == 0)
        return;

    // NOTE: While /dev is populated asynchronously, the devices for the
    //       root disk have to be there for erlinit to run.
    struct dirent **namelist;
    int n = scandir("/dev",
                    &namelist,
                    NULL,
                    NULL);
    int i;
    for (i = 0; i < n; i++) {
        char devpath[ERLINIT_PATH_MAX];
        snprintf(devpath, sizeof(devpath), "/dev/%s", namelist[i]->d_name);

        // Block device?
        struct stat sb;
        if (stat(devpath, &sb) != 0 ||
            !S_ISBLK(sb.st_mode))
            continue;

        // Instance of the root device?
        int instance = sb.st_rdev - rdev;
        if (instance < 0 || instance >= 16)
            continue;

        char symlinkpath[ERLINIT_PATH_MAX];
        if (instance == 0)
            snprintf(symlinkpath, sizeof(symlinkpath), "/dev/rootdisk0");
        else
            snprintf(symlinkpath, sizeof(symlinkpath), "/dev/rootdisk0p%d", instance);

        if (symlink(devpath, symlinkpath) < 0)
            warn("Could not create symlink '%s'->'%s': %s", symlinkpath, devpath, strerror(errno));
    }

    if (n >= 0) {
        for (i = 0; i < n; i++)
            free(namelist[i]);
        free(namelist);
    }
}

void mount_filesystems()
{
    // Mount /tmp and /run since they're almost always needed and it's
    // not easy to do it at the right time in Erlang.
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=1777,size=10%") < 0)
        warn("Could not mount tmpfs in /tmp: %s\r\n"
             "Check that tmpfs support is enabled in the kernel config.", strerror(errno));

    if (mount("tmpfs", "/run", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=0755,size=5%") < 0)
        warn("Could not mount tmpfs in /run: %s", strerror(errno));

    // Mount any filesystems specified by the user. This is best effort.
    // The user is required to figure out if anything went wrong in their
    // applications. For example, the filesystem might not be formatted
    // yet, and erlinit is not smart enough to figure that out.
    //
    // An example mount specification looks like:
    //    /dev/mmcblk0p4:/mnt:vfat::utf8
    char *temp = options.extra_mounts;

    while (temp) {
        const char *source = strsep(&temp, ":");
        const char *target = strsep(&temp, ":");
        const char *filesystemtype = strsep(&temp, ":");
        char *mountflags = strsep(&temp, ":");
        const char *data = strsep(&temp, ";"); // multi-mount separator

        if (source && target && filesystemtype && mountflags && data) {
            // Try to mkdir the target just in case the final path entry does
            // not exist. This is a convenience for mounting in filesystems
            // created by the kernel like /dev and /sys/fs/*.
            (void) mkdir(target, 0755);

            unsigned long imountflags = str_to_mountflags(mountflags);
            if (mount(source, target, filesystemtype, imountflags, (void*) data) < 0)
                warn("Cannot mount %s at %s: %s", source, target, strerror(errno));
        } else {
            warn("Invalid parameter to -m. Expecting 5 colon-separated fields");
        }
    }
}

void unmount_all()
{
    debug("unmount_all");

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
                strcmp(mounts[i].target, "/") == 0)
            continue;

        debug("unmounting %s at %s...", mounts[i].source, mounts[i].target);
        if (umount(mounts[i].target) < 0 && umount(mounts[i].source) < 0)
            warn("umount %s failed: %s", mounts[i].target, strerror(errno));
    }
}
