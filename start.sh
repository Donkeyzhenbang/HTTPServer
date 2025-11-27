#!/bin/sh
make clean && make -j8
cd /home/jym/code/cpp/personal-project/gw-server/bin/web/uploads
../../SocketServer
# scp ./bin/SocketServer jym@47.121.121.86:/home/jym/
# scp ./bin/SocketServer mv@47.122.114.144:/home/mv/