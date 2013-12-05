# erlinit

This is a replacement for /sbin/init that launches an Erlang/OTP release. It
is intentionally minimalist as it expects Erlang/OTP to be in charge of
application initialization and supervision. It can be thought of as a simple
Erlang/OTP release start script with some basic system initialization.

## Release setup

A system should contain only one Erlang/OTP release under the `/srv/erlang`
directory. A typical directory hierarchy would be:

    /srv/erlang
         ├── lib
         │   ├── my_app-0.0.1
         │   │   ├── ebin
         │   │   └── priv
         │   ├── kernel-2.16.2
         │   │   └── ebin
         │   └── stdlib-1.19.2
         │       └── ebin
         └── releases
             ├── 1
             │   ├── my_app.boot
             │   ├── my_app.rel
             │   ├── my_app.script
             │   ├── sys.config
             │   └── vm.args
             └── RELEASES

Currently, `erlinit` runs the Erlang VM found in `/usr/lib/erlang` so it is
important that the release and the VM match versions. As would be expected,
the `sys.config` and `vm.args` are used, so it is possible to configure
the system via files in the release. `erlinit` does not introduce any
new configuration files.

## Debugging

Since `erlinit` is the first user process run, it can be a little tricky
to debug when things go wrong. Hopefully this won't happen to you, but if
it does, try recompiling with the `DEBUG` #define enabled. If it looks like
the Erlang runtime is being started, but it crashes or hangs midway without
providing any usable console output, try recompiling
with the `USE_STRACE` #define and add the strace program to /usr/bin. 
Sometimes you need to sift through the strace output to find the missing
library or file that couldn't be loaded. When debugged, please consider
contributing a fix back to help other `erlinit` users.
