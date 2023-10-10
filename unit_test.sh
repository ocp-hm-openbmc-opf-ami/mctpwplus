#!/bin/sh

# Need to have D-Bus for our tests
# Default system bus configuration is too restrictive - we want to allow
# everything.
sed -i 's/deny/allow/g' /usr/share/dbus-1/system.conf
dbus-daemon --config-file=/usr/share/dbus-1/system.conf

meson setup builddir -Dexamples=enabled --wipe
meson compile -C builddir -v

# Clean up
git config --global --add safe.directory /root/local
git clean
