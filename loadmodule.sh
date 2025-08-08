#!/bin/sh

set -e

module=tftdriver
devnode=tftchar
cd `dirname $0`

if [ $# -eq 0 ] ; then
    modprobe ${module} || exit 1
    major=$(awk "\$2==\"$devnode\" {print \$1}" /proc/devices)

    rm -f /dev/${devnode}
    mknod -m 664 /dev/${devnode} c $major 0

elif [ $# -eq 1 ] && { [ "$1" = "-r" ] || [ "$1" = "--remove" ]; } ; then
    rmmod ${module} || exit 1
    rm -f /dev/${devnode}

else
    echo "Usage: loadmodule.sh [-r | -h]"
    echo "  <default>       load ${module}.ko and create /dev/${devnode}"
    echo "  -r| --remove    unload ${module}.ko and remove /dev/${devnode}"
    echo "  -h| --help      print this usage line"

fi
