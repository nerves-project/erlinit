# Changelog

## v1.4.3

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
