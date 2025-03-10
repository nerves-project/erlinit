# erlinit

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/nerves-project/erlinit/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/nerves-project/erlinit/tree/main)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4086/badge.svg)](https://scan.coverity.com/projects/4086)
[![REUSE status](https://api.reuse.software/badge/github.com/nerves-project/erlinit)](https://api.reuse.software/info/github.com/nerves-project/erlinit)

This is a replacement for `/sbin/init` that launches an Erlang/OTP release. It
is intentionally minimalist as it expects Erlang/OTP to be in charge of
application initialization and supervision. It can be thought of as a simple
Erlang/OTP release start script with some basic system initialization.

## Building

`erlinit` comes pre-built for you as part of Nerves and a couple other
Embedded Erlang setups. You shouldn't need to build it. If you do want to
change something, you'll need to cross-compile `erlinit` for your target and
then copy it to `/sbin/init` on your device. Here's how to do this on Nerves. It
assumes that you've built a project that uses Nerves already so that you can
re-use the crosscompiler that was already downloaded:

```sh
$ cd erlinit
$ make clean

# Substitute the toolchain path appropriately. Tab complete will
# probably get you the right thing.
$ export CC=~/.nerves/artifacts/nerves_toolchain_arm_unknown_linux_gnueabihf-linux_x86_64-1.1.0/bin/arm-unknown-linux-gnueabihf-gcc
$ make

# Copy `erlinit` to your Nerves project's rootfs_overlay
$ cp erlinit ~/path/to/your/nerves/project/rootfs_overlay/sbin/init
```

We definitely accept PRs to this project, but before spending too much time,
please review the [Hacking](#hacking) section.

If you need a [reproducible build](https://reproducible-builds.org/), it is
critical that your build system set the `SOURCE_DATE_EPOCH` environment
variable. The real-time clock check in `erlinit` uses a stored timestamp that
will change between successive builds if this is unset.

## Release setup

A system should contain only one Erlang/OTP release under the `/srv/erlang`
directory. A typical directory hierarchy would be:

```text
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
```

In the above release hierarchy, the directory `my_app` at the base is optional.
If there are multiple releases, the first one is used.

Currently, `erlinit` runs the Erlang VM found in `/usr/lib/erlang` so it is
important that the release and the VM match versions. As would be expected, the
`sys.config` and `vm.args` are used, so it is possible to configure the system
via files in the release.

## Configuration and Command line options

`erlinit` pulls its configuration from both the commandline and the file
`/etc/erlinit.config`. The commandline comes from the Linux kernel arguments
that that are left over after the kernel processes them. Look at the bootloader
configuration (e.g. U-Boot) and the Linux kernel configuration (in the case of
default args) to see how to modify these.

The `erlinit.config` file is parsed line by line. If a line starts with a `#`,
it is ignored. Parameters are passed via the file similar to a commandline.  For
example, the following is a valid `/etc/erlinit.config`:

```text
# erlinit.config example

# Enable UTF-8 filename handling
-e LANG=en_US.UTF-8;LANGUAGE=en

# Uncomment to enable verbose prints
-v
```

The following lists the options:

```text
-b, --boot <path>
    Specify a specific .boot file for the Erlang VM to load
    Normally, the .boot file is automatically detected. The .boot extension is
    optional. A relative path is relative to the release directory.

--core-pattern <pattern>
    Specify a pattern for core dumps. This can be a file path like "/data/core".
    See https://elixir.bootlin.com/linux/v6.11.8/source/Documentation/admin-guide/sysctl/kernel.rst#L144.

-c, --ctty <tty[n]>
    Force the controlling terminal (ttyAMA0, tty1, etc.)

-d, --uniqueid-exec <program and arguments>
    Run the specified program to get a unique id for the board. This is useful with -n

-e, --env <VAR=value;VAR2=Value2...>
    Set additional environment variables
    Specify multiple times to break up long lines and avoid 255 character max line length

--gid <id>
    Run the Erlang VM under the specified group ID

--graceful-shutdown-timeout <milliseconds>
    After the application signals that it wants to reboot, poweroff, or halt,
    wait this many milliseconds for it to cleanup and exit.

-h, --hang-on-exit
    Hang the system if Erlang exits. The default is to reboot.

-H, --reboot-on-exit
    Reboot when Erlang exits.

--hang-on-fatal
    Hang if a fatal error is detected in erlinit.

-l, --limits <resource:soft:hard>
    Set resource limits. See prlimit(1) and prlimit(2) for available resources.
    Specify multiple times to set more than one resource's limits.

-m, --mount <dev:path:type:flags:options>
    Mount the specified path. See mount(8) and fstab(5) for fields
    Specify multiple times for more than one path to mount.

-n, --hostname-pattern <pattern>
    Specify a hostname for the system. The pattern supports a "%[-][.len]s"
    where len is the length of the unique ID to use and the "-" controls
    whether the ID is trimmed from the right or left. E.g., "nerves-%.4s"

--pre-run-exec <program and arguments>
    Run the specified command before Erlang starts

--poweroff-on-exit
    Power off when Erlang exits. This is similar to --hang-on-exit except it's for
    platforms without a reset button or an easy way to restart

--poweroff-on-fatal
    Power off if a fatal error is detected in erlinit.

--reboot-on-fatal
    Reboot if a fatal error is detected in erlinit. This is the default.

-r, --release-path <path1[:path2...]>
    A colon-separated lists of paths to search for
    Erlang releases. The default is /srv/erlang.

--release-include-erts
    Use an ERTS provided by the release.

--run-on-exit <program and arguments>
    Run the specified command on exit.

-s, --alternate-exec <program and arguments>
    Run another program that starts Erlang up. The arguments to `erlexec` are
    passed afterwards. This requires an absolute path to the program unless
    you're running a program out of the ERTS directory. For example, to run
    `run_erl`, just pass `run_erl`.

--shutdown-report <path>
    Before shutting down or rebooting, save a report to the specified path.

-t, --print-timing
    Print out when erlinit starts and when it launches Erlang (for
    benchmarking)

--tty-options <baud>[<parity><bits>]
    Initialize the tty to the specified baud rate, parity and bits. This
    option follows the [Linux kernel format](https://www.kernel.org/doc/html/latest/admin-guide/serial-console.html),
    but currently only 9600-115200 baud rates are supported.

--uid <id>
    Run the Erlang VM under the specified user ID

--update-clock
    Force the system clock to at least the build date/time of erlinit.

-v, --verbose
    Enable verbose prints

--warn-unused-tty
    Print a message on ttys receiving kernel logs, but not an Erlang console

--working-directory <path>
    Set the working directory

--x-pivot-root-on-overlayfs
    This enables support for making a read-only root filesystem writable using
    an overlayfs. It is experimental and the option will change.
```

If you're using this in combination with `Nerves` you can customize configration
via the application env as well. For details see the
[related documentation](https://hexdocs.pm/nerves/advanced-configuration.html#overriding-erlinit-config-from-mix-config).

## Rebooting or hanging when the Erlang VM exits

In production, if the Erlang VM exits for any reason, the desired behavior is
usually to reboot. This is the default. When developing your app, you'll quickly
find that this is frustrating since it makes it more difficult to gather debug
information. The following other options are available:

1. `-h` or `--hang-on-exit` - `erlinit` instructs the kernel to halt. On most
    systems this will cause the kernel to hang. Some systems reboot after a long
    delay - for example, a watchdog timer could trigger a reboot.
2. `--poweroff-on-exit` - `erlinit` instructs the kernel to power off.
3. `--run-on-exit` - `erlinit` runs the specified program on exit.

The 3rd option can be particularly useful for debugging since it allows you to
manually collect debug data. For example, specifying `--run-on-exit /bin/sh`
launches a shell. Another use is to invoke a program that reverts back to a
known good version of the application. When the command exits, `erlinit` will
either reboot, hang, or poweroff depending on whether `--hang-on-exit` or
`--poweroff-on-exit` were passed.

If you're using `heart`/`nerves_heart` or some other kind of application watchdog
make sure to disable those as well. They might also be triggering reboots if the
application is not up and running.

## Read-only root file systems

By default `erlinit` keeps the root filesystem mounted read-only. This is useful
since it significantly reduces the chance of corrupting the root filesystem at
runtime. If applications need to write to disk, they can always mount a writable
partition and have code that handles corruptions on it. Recovering from a
corrupt root filesystem is harder. During development, though, working with a
read-only root filesystem can be a pain so an alternative is to remount it
read-write. Applications can do this, but another approach is to update the
application to reference the files in development from /tmp or a writable
partition. For example, the Erlang code search path can be updated at runtime to
references new directories for code. The
[relsync](https://github.com/fhunleth/relsync) program does this to dynamically
update Erlang code via the Erlang distribution protocol.

## Logging

`erlinit` logs to `/dev/kmsg` and the messages can be viewed by running `dmesg`.
For debug purposes and if `/dev/kmsg` cannot be opened, logging goes to
`stderr`. This latter situation isn't desirable for normal use since writing to
`stderr` can block. In some scenarios, it can block indefinitely (e.g., logging
to a gadget serial device).

## Shutdown reports

`erlinit` can help debug unexpected reboots and poweroffs. If you specify a path
to `--shutdown-report`, `erlinit` will save what it knows about why and when
Erlang exited.

## Debugging erlinit

Since `erlinit` is the first user process run, it can be a little tricky to
debug when things go wrong. Hopefully this won't happen to you, but if it does,
try passing '-v' in the kernel arguments so that `erlinit` runs in verbose mode.
If it looks like the Erlang runtime is being started, but it crashes or hangs
midway without providing any usable console output, try passing `-s
"/usr/bin/strace -f"` in the config file or via kernel arguments to run `strace`
on the initialization process. Be sure to add the strace program to `/usr/bin`.
Sometimes you need to sift through the strace output to find the missing library
or file that couldn't be loaded. When debugged, please consider contributing a
fix back to help other `erlinit` users.

## Filesystem mounting notes

It is possible to mount filesystems before the Erlang VM is started. This is
useful if the Erlang release is not on the root filesystem and to support
logging to a writable filesystem. This mechanism is not intended to support all
types of mounts as the error conditions and handling are usually better handled
at the application level. Additionally, it is good practice to check that a
filesystem has mounted successfully in an application just so that if an error
occurred, the filesystem can be fixed or reformatted. Obviously, logging or
loading an alternative Erlang release will not be available if this happens, but
at least the system can recover for the next reboot.

Typical mount commandline arguments look like:

```text
-m /dev/mmcblk0p4:/mnt:vfat::utf8
```

This mounts `/dev/mccblk0p4` as a vfat filesystem to the /mnt directory. No
flags are passed, and the utf8 option is passed to the vfat driver. See mount(8)
for options.

## Hostnames

`erlinit` can set the hostname of the system so that it is available when Erlang
starts up. Do this by passing the hostname as the `-n` argument to `erlinit`, or
hardcoding it in `/etc/hostname`.

The `-n` argument takes a
printf(3) formatted string that is passed a string argument, which
is found by running the command specified by `-d`. This makes it possible to
specify a unique ID, or some other information present on the file
system. For example, if a _getmyid_ command is
available that prints a unique identifier to stdout, it can be used to define the
hostname like:

```text
-d "getmyid -args" -n erl-%.4s
```

In this example, if the _getmyid_ program returns `012345`, then the hostname would be
`erl-0123`.

Alternatively, the program specified by `-d` could return a full
hostname. The configuration would look like:

```text
-d "getmyhostname -args" -n %s
```

In theory, the _getmyhostname_ program could read an EEPROM or some file on a
writable partition to return this hostname.

## Root disk naming

If you have multiple memory cards, SSDs, or other devices connected, it's
possible that Linux will enumerate those devices in a nondeterministic order.
This can be mitigated by using `udev` to populate the `/dev/disks/by-*`
directories, but even this can be inconvenient when you just want to refer to
the drive that provides the root filesystem. To address this, `erlinit` creates
`/dev/rootdisk0`, `/dev/rootdisk0p1`, etc. and symlinks them to the expected
devices. For example, if your root file system is on `/dev/mmcblk0p1`, you'll
get a symlink from `/dev/rootdisk0p1` to `/dev/mmcblk0p1` and the whole disk
will be `/dev/rootdisk0`. Similarly, if the root filesystem is on `/dev/sdb1`,
you'd still get `/dev/rootdisk0p1` and `/dev/rootdisk0` and they'd by symlinked
to `/dev/sdb1` and `/dev/sdb` respectively.

## Chaining programs

It's possible for `erlinit` to run a program that launches `erlexec` so that
various aspects of the Erlang VM can be modified in an advanced way. This is
done by specifying `-s` or `--alternate-exec`. The program (and arguments)
specified are invoked and the `erlexec` and other options that would have been
run are passed as the last arguments.

One use is running `strace` on the Erlang VM as described in the debugging
section. Another use is to capture the Erlang console to a pipe and redirect it
to a GUI or web app. The `dtach` utility is useful for this. An example
invocation is: `--alternate-exec "/usr/bin/dtach -N /tmp/iex_prompt"`.  See the
`dtach` manpage for details.

IMPORTANT: Use absolute paths to the programs that you want to run unless they
are supplied by the Erlang runtime. `erlinit` knows about the Erlang runtime and
will find the proper Erlang runtime binary (like `run_erl`), if you just pass
the program name.

## Multiple consoles

Some targets such as the Raspberry Pi have multiple locations where the Erlang
shell could be sent. Currently, `erlinit` only supports a console on one of the
locations.  This can cause some confusion and look like a hang. To address this,
`erlinit` can print a warning message on the unused consoles using the
`--warn-unused-tty` option.  For example, if the user specifies that the Erlang
shell is on `ttyAMA0` (the UART port), a message will be printed on `tty0` (the
HDMI output).

## Privilege

By default, `erlinit` starts the Erlang VM with superuser privilege. It is
possible to reduce privilege by specifying the `--uid` and `--gid` options.
Before doing this, make sure that your embedded Erlang/OTP application can
support this. When dealing with hardware, it is quite easy to run into
situations requiring elevated privileges.

## Clocks

If you're running on a system without a real-time clock, the clock will report
that it's 1970. Even if you have a real-time clock, a failure of the battery
could still cause it to show a date in the 1980s or 1990s. `erlinit` isn't smart
enough to fix this, but it can set a lower bound for the clock based on its
build timestamp.  Specify the `--update-clock` option to enable this.
Additionally, this lower bound is set very early on in the boot process so
nothing besides `erlinit` should see a decade's old time.

This option has the following caveats:

1. Some projects have chosen to use the date to check whether NTP has
   synchronized the clock yet. If you are a person who did this, please consider
   a more direct route of checking NTP's synchronization status.
2. Look into what `fake-hwclock` does if you need something better.
3. If using this to guarantee a minimum timestamp so that SSL certificates work,
   be sure that the SSL certificates don't expire before the next firmware
   update.  (Not that you don't have to do that anyway, but just a friendly
   reminder)

NOTE: This feature is at odds with creating [reproducible
builds](https://reproducible-builds.org). Set the
[SOURCE_DATE_EPOCH](https://reproducible-builds.org/docs/source-date-epoch/)
environment variable to force the build time to be a constant.

## Random number seeds

Embedded systems usually run a script to save a random number on shutdown that's
used to initialize the random number generator on the next boot. `erlinit` uses
[SeedRNG](https://git.zx2c4.com/seedrng/tree/README.md) for this. The seed is
stored under `/root/seedrng` by default since that's a writable and persisted
partition on Nerves. SeedRNG is called right before a `--pre-run-exec` and at
shutdown after all other processes have exited. Errors are ignored, but logged
as warnings. Specify `-v` to `erlinit` to see some information messages.

## Passing arguments to reboot

It's possible to pass arguments to the `reboot(2)` syscall. This feature is used
by the Raspberry Pi bootloader to support its automatic failback mechanism. See
the [tryboot
documentation](https://www.raspberrypi.com/documentation/computers/config_txt.html#the-tryboot-filter)
for high level details for Raspberry Pi OS. The main difference with `erlinit`
is how the `"0 tryboot"` parameter gets passed to the kernel. Raspberry Pi OS
uses Systemd and Systemd's `reboot` implementation writes the parameter to
`/run/systemd/reboot-param` to pass it to PID 1. With `erlinit`, you're
responsible for writing `"0 tryboot"` to `/run/reboot-param` and then running
`reboot`. `erlinit` will see file and pass the argument to the kernel as
intended.

## Saving core dumps early boot

Saving core dumps requires that they be enabled in the Linux kernel and that
there be a place to write them. Erlinit can make that happen. The configuration
snippet below shows a line that will mount a writable filesystem at `/root`,
then set the core pattern to write a file named `core` to that path, and then
adjust the limits to save an unlimited amount of data to the `core` file. Modify
to suit your system.

```text
-m /dev/mmcblk0p1:/root:f2fs:nodev:
--core-pattern /root/core
--limit core:unlimited:unlimited
```

## Hacking

It seems like there are an endless number of small tweaks to `erlinit` that
yield meaningful improvements. Please post Github issues before starting on
anything substantial since there's often another way.

To verify that your changes work, run `make check` to run `erlinit` through its
regression tests. These tests should run fine on both OSX and Linux even though
`erlinit` is intended to be run on a minimal embedded Linux system. See
`test/fixture` for the shared library that's used to simulate `erlinit` being
run as Linux's init process (pid 1).
