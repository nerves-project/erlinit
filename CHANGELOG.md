# Changelog

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
