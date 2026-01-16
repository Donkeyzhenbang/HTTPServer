#!/bin/sh
# $Id$
#
if [ -z "$1" ]; then
    echo "Usage: `basename $0` prog"
    exit 1
fi
valgrind --leak-check=full \
--show-reachable=no \
--show-leak-kinds=all \
--track-origins=yes \
--log-file=valmem.log \
"$@"