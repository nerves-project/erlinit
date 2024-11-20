# Changelog

## v1.14.2

* New features
  * Support setting the core file pattern early in the boot process to enabling
    capturing core files from `beam.smp` startup. See --core-pattern.

* Bug fixes
  * Fix parse issue when looking for file systems to unmount. Previously if file
    system options for longer than 127 characters, there'd be a harmless parse
    error. It was harmless since it just resulted in an ignored error and
    everything that was supposed to be unmounted was. It was just distracting to
    see in the logs.

## v1.14.1

* Bug fixes
  * Fix several issues found with clang's -fsanitize feature. Thanks to
    @jbnoblot for reporting them. See
    [#114](https://github.com/nerves-project/erlinit/issues/114) for details.


## v1.14.0

* New features
  * Support passing arguments to reboot via the `/run/reboot-param` file. This
    is required to support the Raspberry Pi's tryboot feature to implement A/B
    partition failback. See the README.md for info.
  * In the hostname pattern, support a special printf format specifier for
    trimming the left side of the serial number string. `%-0.4s` lets you use
    the 4 rightmost characters of the serial number in the hostname.

* Bug fixes
  * Disable core dumps before killing programs on shutdowns and reboots. This
    saves I/O operations that might impact the saving of important data.
  * Sync before killing programs on shutdowns and reboots to reduce the amount
    of I/O needed during the graceful part of the shutdown.

## v1.13.0

* New features
  * Save and restore random number seeds with [SeedRNG](https://git.zx2c4.com/seedrng/tree/README.md).
    This saves seeds to `/root/seedrng` since that's a persisted, writable
    location on Nerves. If not using Nerves, this location can be changed at
    compile-time using `LOCALSTATEDIR`.

## v1.12.3

* Bug fixes
  * Switch default TERM setting from `vt100` to `xterm-256color`. VT100
    emulation was causing `$<50>` output delay sequences to be inserted and
    these aren't necessary. `xterm-256color` was determined to be a safe default
    for the majority of console users.

## v1.12.2

* Bug fixes
  * Extend max config file line from 127 characters to 255 to allow for longer
    environment settings. You can also specify `-env` on multiple lines. Thanks
    to @pojiro for raising this issue.

## v1.12.1

* Bug fixes
  * Remove absolute path requirement on `--alternate-exec` for binaries in the
    Erlang runtime. This makes it much less brittle to use since the ERTS
    directory has a version number in it that may change on every Erlang
    update. If you're using `--alternate-exec` to run `run_erl`, remove the
    absolute path to it. `erlinit` will run the one associated with the OTP
    release.

## v1.12.0

* Changes
  * Check OTP release for ERTS before checking the system. The previous behavior
    was to only check one or the other based on whether `--release-include-erts`
    was specified.

* Bug fixes
  * Fixed missing handling of EINTR when running the program to get the device's
    ID. The handling has been missing since it was added and is not known to
    have caused issues. However, it could cause the device's ID to not be set.

## v1.11.0

* New features
  * Export `RELEASE_SYS_CONFIG`, `RELEASE_ROOT`, and `RELEASE_TMP` to support
    Elixir mix releases that contain a `runtime.exs`. Support for `runtime.exs`
    is currently experimental.

## v1.10.0

* New features
  * Added `--limit` option to enable  core dumps, increase the number of
    processes, file descriptors and other settings that the `ulimit` command
    is normally used for.

    Use it like this:

    ```
    --limit core:456:unlimited
    ```

    The format is <resource:soft_limit:hard_limit>. The resources should be
    specified as lower case and can be found on the `setrlimit(2)` man page.
    The soft_limit and hard_limit can be either numbers or the word
    `infinity`.

* Bug fixes
  * Double the max length of release version names to 32 characters. This should
    hopefully be more than sufficient now.

## v1.9.0

* New features
  * Support experimentation with using Linux's `overlayfs` to make normally
    read-only root filesystems be writable. See the
    `--x-pivot-root-on-overlayfs` option.
  * Restrict operations on the `/dev` mount. This prevents things like
    copying large files to `/dev` or using it as a place to run random binaries.

## v1.8.0

* New features
  * Added `--shutdown-report <path>`. Whenever the Erlang VM exits, if a path
    has been specified, `erlinit` will write a short report that may be useful
    in debugging whatever caused the exit. In Nerves:

    ```elixir
    config :nerves,
      erlinit: [shutdown_report: "/data/shutdown.txt"]
    ```

## v1.7.1

* Bug fixes
  * `$HOME` is now set based on `/etc/passwd` rather than hardcoded to `/root`.
    This fixes an inconsistency in how `erlinit` worked and pretty much every
    Linux distribution. If the lookup fails, `erlinit` defaults to `/root`.
    Nerves creates a `/etc/passwd` that already has root's home directory set to
    `/root`, so most users won't notice a change.

## v1.7.0

* New features
  * Added `--tty-options` parameter for initializing UARTs.
    E.g., `--tty-options 115200n8` or in Nerves, use:

    ```elixir
    config :nerves,
      erlinit: [ctty: "ttyS0", tty_options: "115200n8", alternate_exec: "/usr/bin/nbtty"]
    ```

    Only common baud rates and 8-bit/no parity are supported.

## v1.6.1

* Bug fixes
  * Fix rootdisk symlink determination. Erlinit has been responsible for
    creating `/dev/rootdisk*` symlinks so that applications can generically
    figure out which block device is the boot disk. This is used for firmware
    updates, mounting other filesystems, etc. Previously a heuristic was used
    that worked a good amount of time. It broke or created extraneous symlinks
    on some systems. The new mechanism queries Linux directly and does a better
    job.

## v1.6.0

* New features
  * Added the `--release-include-erts` option so that `erlinit` can use the ERTS
    provided by embedded release. Thanks to João Henrique Ferreira de Freitas
    for this feature.

## v1.5.3

* Bug fixes
  * Change `PROGNAME` setting to `erlexec` so that OTP's
    [`slave`](http://erlang.org/doc/man/slave.html) works. `slave` had been
    calling `erl` which would try to figure out the `ROOTDIR` again. `erlinit`
    already sets `ROOTDIR` properly, so this isn't necessary and `erl` would also
    get it wrong.

## v1.5.2

* Bug fixes
  * Due to an embedded timestamp for checking the system clock, erlinit was not
    reproducible. This adds support for SOURCE_DATE_EPOCH. See
    https://reproducible-builds.org/ for motivation and more details.
  * Fix graceful shutdown time calculation bug. The seconds part of the
    calculation was correct, but the nanoseconds part was not. Since the default
    timeout was an even number of  seconds, it's likely that this fix is doesn't
    affect many users.

## v1.5.1

* Bug fixes
  * Fixed an issue with reaping zombie processes if too many died at once. If
    this happened and you ran `ps`, you'd start seeing processes in the `Z`
    state piling up over time.

## v1.5.0

* New features
  * Support Elixir 1.9+ releases by setting the `-boot_var RELEASE_LIB`.

## v1.4.9

* Bug fixes
  * Work around kernel message rate limiter. This makes it possible to see >10
    messages when verbose is enabled.
  * Support merging commandline arguments together so that it's possible to
    launch erlang via `run_erl`. This is required since `run_erl` runs
    `sh -c` to start Erlang up and that requires all arguments to be passed
    as a long string. Example: `-s "run_erl /tmp/ /tmp exec"`

## v1.4.8

* Bug fixes
  * Handle or fix hostnames that were generated with non-RFC 1123 compatible
    characters or failures with unique id generation.

## v1.4.7

* Bug fixes
  * Fixed root disk detection when the root filesystem is on an NVME-connected
    SSD.

## v1.4.6

* Bug fixes
  * Fixed permissions on /dev/shm so that non-root users could create shared
    memory. This is required for Chromium unless you want to run it as root.

## v1.4.5

* Bug fixes
  * Refer to start_erl.data for determining which ERTS version to use. While
    this shouldn't be necessary, it is possible to get into situations with
    two versions of ERTS installed and this change prevents erlinit from
    getting confused and failing to boot.

## v1.4.4

* Bug fixes
  * Restore a default signal mask on launched processes. Prior to this change,
    the application launched by erlinit would get erlinit's signal mask. This
    resulted in some unexpected behavior due to assumptions about signals
    being unmasked.

## v1.4.3

* Bug fixes
  * Set the IPv6 loopback address (::1/128) on the loopback interface.
    Previously only the IPv4 loopback address was set. Both are set best
    effort.

* Regression test improvements
  * Regression tests work on both Linux and OSX now
  * Improved coverage of tests to verify calls to things like mount, ioctl,
    and reboot
  * fakechroot no longer needed
  * Removed "unit test" special build of erlinit. A test fixture shared
    library now similates the PID 1 environment that erlinit expects.

## v1.4.2

* Bug fixes
  * Add dummy name to filesystem mounts. This fixes parse errors from
    programs that read /proc/self/mountinfo (in particular Docker). Thanks to
    Troels Brødsgaard for reporting this issue.

## v1.4.1

* Bug fixes
  * Bulletproof hostname generation to avoid accidents that add whitespace in
    the hostname string.

## v1.4.0

* New features
  * Add option to force system clock forward to the build date/time. This is
    off by default. This feature makes it possible to use code that depends on
    the clock not being in the 70s before NTP syncs.
  * Tighten file system mount options. While not bulletproof, this makes
    /sys and /proc nosuid, noexec, and nodev.
  * If mountpoints don't exist, try to create them. This is needed for
    cgroup support since the convention mounts the cgroup directories on
    a tmpfs.
  * Add the "rw" mount option. Even though this is the default, people
    sometimes specify it and this prevents the mount error from it not
    being recognized by erlinit.

## v1.3.1

* Bug fixes
  * Addressed feedback to make rootdisk* symlinks more consistent with Linux
    naming. Not backwards compatible with the 1.3.0 naming.

## v1.3.0

* New features
  * Create /dev/rootdisk* symlinks to the block devices that are on the same
    physical device.  This makes it a lot easier to deal with systems that
    don't have predictable device names or don't have them early enough in
    the boot process.

## v1.2.0

* New features
  * Support Elixir's consolidated directory if found.

## v1.1.4

* Bug fixes
  * Fix reboot hang on the BBB when using the g_serial driver and sending the
    IEx prompt out the virtual serial port.

## v1.1.3

* Bug fixes
  * Improve debug on graceful shutdown to help debug errors in the future.
    Also, handle some error returns that were not being handled before.
  * Change default to `--reboot-on-fatal` so that fatal errors from `erlinit`
    don't result in hung boards. This is the expectation in production, so
    users debugging `erlinit` issues should add `--hang-on-fatal` to their
    `erlinit.config` files from now on.

## v1.1.2

* Bug fixes
  * Log to `/dev/kmsg` to avoid blocking on `stderr` due to error messages
    being printed. This fixes an issue blocking reboots when logging to a
    gadget serial port.

## v1.1.1

* Bug fixes
  * Reap orphaned zombie processes

## v1.1.0

* New features
  * Add graceful-shutdown-timeout option to specify longer or shorter
    timeouts for waiting for the Erlang VM to exit gracefully.

## v1.0.1

* Fixes
  * Fix Erlang VM exit detection breakage introduced by graceful shutdown.

## v1.0.0

* New features
  * Added support for graceful shutdowns. Now requests to reboot, halt,
    and power off won't immediately terminate the Erlang VM. It will get
    about 10 seconds to shut down graceful before it's killed.
  * Added support for reading `start_erl.data` to decide which version
    of a release to run.
  * Sorted all directory scans so that they're deterministic. Previously
    the order that files were read in a directory could determine what was
    run. While the order appeared stable in practice, it wasn't guaranteed.
  * Added `--working-directory` to change the current directory to
    something else. This was useful for OTP applications that wrote to
    the current directory.
  * Added quite a few more regression tests.

## v0.8.0

* New features
  * Add -b/--boot parameter to specify which `.boot` file to run.
  * Add support for running a program before launching the Erlang VM.
    This simplifies a use case where wrapping the call to run the VM
    was too much work.

## v0.7.3

* New features
  * Run a program when the Erlang VM exits. For example, run `sh` so
    that you can look around to gather debug information, or run
    another program to revert to a known good image.
  * Run the Erlang VM with non-superuser privileges (`--uid` and `--gid`
    options. Thanks to @ches for discussion on this topic.

* Fixes
  * Pass `-boot_var ERTS_LIB_DIR ...` to `erlexec` for platforms using
    the system libraries (Not Nerves). This is required for at least
    the use of `erlinit` in Yocto. Thanks to chrta for the fix.

## v0.7.2

* New features
  * Print out the hostname on non-controlling terminals in case the
    user can connect over the network
  * Support passing --mount multiple times for more than one mount

## v0.7.1

* New features
  * Support powering off on errors and fatals for platforms without
    and easy way to get out of a hang.

* Fixes
  * The 'halt' command works now.

## v0.7.0

* New features
  * Added --warn-unused-tty to display warning when multiple tty's
    are available, but only one is used.
  * Added long options to make config files easier to read
  * Added -H, --reboot-on-exit to override a -h that was specified in
    the erlinit.config file from the kernel cmdline

## v0.6.1

* Fixes
  * Builds using the musl C library now

## v0.6.0

* New features
  * Support setting unique hostnames per board

## v0.5.3

* New features
  * Works with exrm now

* Fixes
  * Only hang on exit if unintentional
  * Mount /run; clean up other mounts
  * Use first boot file if multiple are found
  * HOME environment variable is /root now so that it is possible to mount a
    writable filesystem there and use it

## v0.4.6

* Fixes
  * Address issues found with Coverity
