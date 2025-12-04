// SPDX-FileCopyrightText: 2015 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

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
            elog(ELOG_WARNING, "Unrecognized filesystem mount flag: %s", flag);

        flag = strtok(NULL, ",");
    }
    return flags;
}

int pivot_root(const char *new_root, const char *put_old);
void pivot_root_on_overlayfs()
{
    elog(ELOG_DEBUG, "pivot_root_on_overlayfs");

    // Setup an overlay filesystem for the rootfs so that the official contents
    // are protected in a read-only fs, but we can still update files when
    // debugging.
    if (mount("", "/mnt", "tmpfs", 0, "size=10%") < 0) {
        elog(ELOG_ERROR, "Could not mount tmpfs in /mnt: %s\n"
             "Check that tmpfs support is enabled in the kernel config.", strerror(errno));
        return;
    }

    (void) mkdir("/mnt/.merged", 0755);
    (void) mkdir("/mnt/.upper", 0755);
    (void) mkdir("/mnt/.work", 0755);


    if (mount("", "/mnt/.merged", "overlay", 0,
              "lowerdir=/,upperdir=/mnt/.upper,workdir=/mnt/.work") < 0) {
        elog(ELOG_ERROR, "Could not mount overlayfs: %s\n"
             "Check that CONFIG_OVERLAY_FS=y is in the kernel config.", strerror(errno));
        return;
    }

    OK_OR_WARN(mkdir("/mnt/.merged/dev", 0755), "Cannot create /mnt/.merged/dev");

    if (mount("/dev", "/mnt/.merged/dev", "tmpfs", MS_MOVE, NULL) < 0) {
        elog(ELOG_ERROR, "Could not move /dev to upper overlay: %s", strerror(errno));
        return;
    }
    if (chdir("/mnt/.merged") < 0) {
        elog(ELOG_ERROR, "Could not change directory to /mnt/.merged: %s", strerror(errno));
        return;
    }

    if (mkdir(".oldrootfs", 0755) < 0) {
        elog(ELOG_ERROR, "Could not create directory .oldrootfs: %s", strerror(errno));
        return;
    }

    if (pivot_root(".", ".oldrootfs") < 0) {
        elog(ELOG_ERROR, "pivot_root failed: %s", strerror(errno));
        return;
    }
    elog(ELOG_DEBUG, "pivot_root_on_overlayfs done!");
}

void setup_pseudo_filesystems()
{
    // This only works in the real environment.
    OK_OR_WARN(mount("proc", "/proc", "proc", MS_NOEXEC | MS_NOSUID | MS_NODEV, NULL),
               "Cannot mount /proc");
    OK_OR_WARN(mount("sysfs", "/sys", "sysfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, NULL),
               "Cannot mount /sys");

    // /dev should be automatically created/mounted by Linux
    OK_OR_WARN(mount("devtmpfs", "/dev", "devtmpfs", MS_REMOUNT | MS_NOEXEC | MS_NOSUID, "size=1024k"),
               "Cannot remount /dev");

    // Create entries in /dev. Turn off the umask since we want the exact
    // permissions that we're specifying.
    mode_t old_umask = umask(0);
    OK_OR_WARN(mkdir("/dev/pts", 0755), "Cannot create /dev/pts");
    OK_OR_WARN(mkdir("/dev/shm", 01777), "Cannot create /dev/shm");
    umask(old_umask);

    OK_OR_WARN(mount("devpts", "/dev/pts", "devpts", MS_NOEXEC | MS_NOSUID, "gid=5,mode=620"),
               "Cannot mount /dev/pts");
}

void mount_filesystems()
{
    // Mount /tmp and /run since they're almost always needed and it's
    // not easy to do it at the right time in Erlang.
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=1777,size=10%") < 0)
        elog(ELOG_WARNING, "Could not mount tmpfs in /tmp: %s\r\n"
             "Check that tmpfs support is enabled in the kernel config.", strerror(errno));

    if (mount("tmpfs", "/run", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=0755,size=5%") < 0)
        elog(ELOG_WARNING, "Could not mount tmpfs in /run: %s", strerror(errno));

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
            if (mount(source, target, filesystemtype, imountflags, (void *) data) < 0)
                elog(ELOG_WARNING, "Cannot mount %s at %s: %s", source, target, strerror(errno));
        } else {
            elog(ELOG_WARNING, "Invalid parameter to -m. Expecting 5 colon-separated fields");
        }
    }
}

void unmount_all()
{
    elog(ELOG_DEBUG, "unmount_all");

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        elog(ELOG_WARNING, "/proc/mounts not found");
        return;
    }

    struct mount_info {
        char source[256];
        char target[256];
    } mounts[MAX_MOUNTS];

    int i = 0;
    while (i < MAX_MOUNTS &&
            fscanf(fp, "%255s %255s %*s %*s %*d %*d", mounts[i].source, mounts[i].target) == 2) {
        i++;
    }
    fclose(fp);

    // Unmount as much as we can in reverse order.
    int num_mounts = i;
    for (i = num_mounts - 1; i >= 0; i--) {
        // Allow directories that don't unmount or remount immediately (rootfs)
        if (strcmp(mounts[i].source, "devtmpfs") == 0 ||
                strcmp(mounts[i].source, "/dev/root") == 0)
            continue;

        elog(ELOG_DEBUG, "unmounting %s at %s...", mounts[i].source, mounts[i].target);
        if (umount(mounts[i].target) < 0 && umount(mounts[i].source) < 0)
            elog(ELOG_WARNING, "umount %s failed: %s", mounts[i].target, strerror(errno));
    }
}
