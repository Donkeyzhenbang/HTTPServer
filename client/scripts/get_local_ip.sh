#!/bin/sh
local_ip=$(ip -4 addr show ppp0 | grep -oP '(?<=inet\s)\d+(\.\d+){2}')
echo "$local_ip"
