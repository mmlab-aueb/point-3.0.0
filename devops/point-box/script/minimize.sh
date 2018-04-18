#!/bin/bash -eux

if [[ "$DESKTOP" =~ ^(true|yes|on|1|TRUE|YES|ON])$ ]]; then
  exit
fi

echo "==> Disk usage before minimization"
df -h

echo "==> Installed packages before cleanup"
dpkg --get-selections | grep -v deinstall

# Remove some packages to get a minimal install
echo "==> Removing all linux kernels except the current one"
dpkg --list | awk '{ print $2 }' | grep 'linux-image-3.*-generic' | grep -v $(uname -r) | xargs apt-get -y purge

if [[ "$REMOVE_DOCS" =~ ^(true|yes|on|1|TRUE|YES|ON])$ ]]; then
  echo "==> Removing documentation"
  dpkg --list | awk '{ print $2 }' | grep -- '-doc$' | xargs apt-get -y purge
fi

echo "==> Removing X11 libraries"
apt-get -y purge libx11-data xauth libxmuu1 libxcb1 libx11-6 libxext6
echo "==> Removing desktop components"
apt-get -y purge gnome-getting-started-docs
apt-get -y purge $(dpkg --get-selections | grep -v deinstall | grep libreoffice | cut -f 1)
echo "==> Removing obsolete networking components"
apt-get -y purge ppp pppconfig pppoeconf
echo "==> Removing other oddities"
apt-get -y purge popularity-contest installation-report wireless-tools wpasupplicant
echo "==> Removing default system Ruby"
apt-get -y purge ruby ri doc libffi5

# Clean up the apt cache
echo "==> Cleaning up the apt cache"
apt-get -y autoremove --purge
apt-get -y autoclean
apt-get -y clean

echo "==> Removing APT files"
find /var/lib/apt -type f -delete

# if [[ "$REMOVE_DOCS" =~ ^(true|yes|on|1|TRUE|YES|ON])$ ]]; then
#   echo "==> Removing man pages"
#   find /usr/share/man -type f -delete
#   echo "==> Removing any docs"
#   find /usr/share/doc -type f -delete
#   echo "==> Removing info"
#   rm -rf /usr/share/info/*
# fi

echo "==> Removing caches"
find /var/cache -type f -delete
echo "==> Removing lintian linda"
rm -rf /usr/share/lintian/* /usr/share/linda/*

echo "==> Disk usage after cleanup"
df -h

