# erlinit

[![Build Status](https://travis-ci.org/nerves-project/erlinit.svg?branch=master)](https://travis-ci.org/nerves-project/erlinit) [![Coverity Scan Build Status](https://scan.coverity.com/projects/4086/badge.svg)](https://scan.coverity.com/projects/4086)

This is a replacement for `/sbin/init` that launches an Erlang/OTP release. It
is intentionally minimalist as it expects Erlang/OTP to be in charge of
application initialization and supervision. It can be thought of as a simple
Erlang/OTP release start script with some basic system initialization.

## Release setup

A system should contain only one Erlang/OTP release under the `/srv/erlang`
directory. A typical directory hierarchy would be:

    /srv/erlang/my_app*
         ├── lib
         │   ├── my_app-0.0.1
         │   │   ├── ebin
         │   │   └── priv
         │   ├── kernel-2.16.2
         │   │   └── ebin
         │   └── stdlib-1.19.2
         │       └── ebin
         └── releases
             ├── 1
             │   ├── my_app.boot
             │   ├── my_app.rel
             │   ├── my_app.script
             │   ├── sys.config
             │   └── vm.args
             └── RELEASES

In the above release hierarchy, the directory `my_app` at the base is
optional. If there are multiple releases, the first one is used.

Currently, `erlinit` runs the Erlang VM found in `/usr/lib/erlang` so it is
important that the release and the VM match versions. As would be expected,
the `sys.config` and `vm.args` are used, so it is possible to configure
the system via files in the release.

## Configuration and Command line options

`erlinit` pulls its configuration from both the commandline and the file
`/etc/erlinit.config`. The commandline comes from the Linux kernel arguments
that that are left over after the kernel processes them. Look at the bootloader
configuration (e.g. U-Boot) and the Linux kernel configuration (in the case of default args)
to see how to modify these.

The `erlinit.config` file is parsed line by line. If a line starts with a `#`,
it is ignored. Parameters are passed via the file similar to a commandline.
For example, the following is a valid `/etc/erlinit.config`:

    # erlinit.config example

    # Enable UTF-8 filename handling
    -e LANG=en_US.UTF-8;LANGUAGE=en

    # Uncomment to enable verbose prints
    -v

The following lists the options:

    -c, --ctty <tty[n]>
        Force the controlling terminal (ttyAMA0, tty1, etc.)

    -d, --uniqueid-exec <program and arguments>
        Run the specified program to get a unique id for the board. This is useful with -n

    -e, --env <VAR=value;VAR2=Value2...>
        Set additional environment variables

    -h, --hang-on-exit
        Hang the system if Erlang exits. The default is to reboot.

    -H, --reboot-on-exit
        Reboot when Erlang exits.

    --hang-on-fatal
        Hang if a fatal error is detected in erlinit. This is the default.

    -m, --mount <dev:path:type:flags:options>
        Mount the specified path. See mount(8) and fstab(5) for fields
        Specify multiple times for more than one path to mount.

    -n, --hostname-pattern <pattern>
        Specify a hostname for the system. The pattern is a printf(3)
        pattern. It is passed a unique ID for the board. E.g., "nerves-%.4s"

    --poweroff-on-exit
        Power off when Erlang exits. This is similar to --hang-on-exit except it's for
        platforms without a reset button or an easy way to restart

    --poweroff-on-fatal
        Power off if a fatal error is detected in erlinit.

    --reboot-on-fatal
        Reboot if a fatal error is detected in erlinit.

    -r, --release-path <path1[:path2...]>
        A colon-separated lists of paths to search for
        Erlang releases. The default is /srv/erlang.

    -s, --alternate-exec <program and arguments>
        Run another program that starts Erlang up

    -t, --print-timing
        Print out when erlinit starts and when it launches Erlang (for
        benchmarking)

    -v, --verbose
        Enable verbose prints

    --warn-unused-tty
        Print a message on ttys receiving kernel logs, but not an Erlang console

## Rebooting or hanging when the Erlang VM exits

When you're developing your app, it is useful to hang the platform when something bad
happens in the Erlang VM. To do this, pass the `-h` option. In production,
the desired behavior is usually to reboot. Rebooting is the default, but you can
specify this explicitly by passing `-H` or `reboot-on-exit`.

## Read-only root file systems

By default `erlinit` keeps the root filesystem mounted read-only. This is useful
since it significantly reduces the chance of corrupting the root filesystem at
runtime. If applications need to write to disk, they can always mount a
writable partition and have code that handles corruptions on it. Recovering from
a corrupt root filesystem is harder. During development, though, working with a
read-only root filesystem can be a pain so an alternative is to remount it
read-write. Applications can do this, but another approach is to update the
application to reference the files in development from /tmp or a writable
partition. For example, the Erlang code search path can be updated at runtime to
references new directories for code. The
[relsync](https://github.com/fhunleth/relsync) program does this to dynamically
update Erlang code via the Erlang distribution protocol.

## Debugging erlinit

Since `erlinit` is the first user process run, it can be a little tricky
to debug when things go wrong. Hopefully this won't happen to you, but if
it does, try passing '-v' in the kernel arguments so that `erlinit` runs in
verbose mode. If it looks like the Erlang runtime is being started, but it
crashes or hangs midway without providing any usable console output, try
passing `-s "/usr/bin/strace -f"` in the config file or via kernel arguments
to run `strace` on the initialization process. Be sure to add the strace program to `/usr/bin`.
Sometimes you need to sift through the strace output to find the missing
library or file that couldn't be loaded. When debugged, please consider
contributing a fix back to help other `erlinit` users.

## Filesystem mounting notes

It is possible to mount filesystems before the Erlang VM is started. This is
useful if the Erlang release is not on the root filesystem and to support
logging to a writable filesystem. This mechanism is not intended to support
all types of mounts as the error conditions and handling are usually better
handled at the application level. Additionally, it is good practice to check
that a filesystem has mounted successfully in an application just so that
if an error occurred, the filesystem can be fixed or reformatted. Obviously,
logging or loading an alternative Erlang release will not be available if
this happens, but at least the system can recover for the next reboot.

Typical mount commandline arguments look like:

    -m /dev/mmcblk0p4:/mnt:vfat::utf8

This mounts /dev/mccblk0p4 as a vfat filesystem to the /mnt directory. No flags
are passed, and the utf8 option is passed to the vfat driver. See mount(8) for
options.

## Hostnames

`erlinit` can set the hostname of the system so that it is available when Erlang
starts up. The hostname can be hardcoded in the `/etc/hostname` file or it can
be set by parameters to `erlinit`. Additionally, it's possible to specify a part
of the name to be based on a unique ID or other information present on the file
system. The `-n` argument is used to specify the hostname string. It is a
printf(3) formatted string that is passed a string argument. The string argument
is found by running the command specified by `-d`. For example, if a command is
available the prints a unique identifier to stdout, it can be used to define the
hostname:

    -d "getmyid -args" -n erl-%.4s

If the `getmyid` program returns `012345`, then the hostname would be
`erl-0123`.

Another use would be for the program specified by `-d` to just return the
hostname. The configuration would look like:

    -d "getmyhostname -args" -n %s

In theory, the `getmyhostname` program could read an EEPROM or some file on a
writable partition to return the hostname.

## Chaining programs

It's possible for `erlinit` to run a program that launches `erlexec` so that
various aspects of the Erlang VM can be modified in an advanced way. This is
done by specifying `-s` or `--alternate-exec`. The program (and arguments) specified
are invoked and the `erlexec` and other options that would have been run are passed
as the last arguments.

One use is running `strace` on the Erlang VM as described in the debugging
section. Another use is to capture the Erlang console to a pipe and redirect it
to a GUI or web app. The `dtach` utility is useful for this. An example
invocation is: `--alternate-exec "/usr/bin/dtach -N /tmp/iex_prompt"`.
See the `dtach` manpage for details.

## Multiple consoles

Some targets such as the Raspberry Pi have multiple locations where the Erlang shell
could be sent. Currently, `erlinit` only supports a console on one of the locations.
This can cause some confusion and look like a hang. To address this, `erlinit` can
print a warning message on the unused consoles using the `--warn-unused-tty` option.
For example, if the user specifies that the Erlang shell is on `ttyAMA0` (the UART
port), a message will be printed on `tty0` (the HDMI output).
