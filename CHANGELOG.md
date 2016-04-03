# Changelog

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
