#!/bin/sh
ip route del default dev ppp0
ip route add default dev ppp0 proto static scope link metric 90
