#!/bin/sh
cd `dirname $0`
echo 0 | tee /sys/class/leds/led0/brightness
echo 0 | tee /sys/class/leds/led1/brightness
./udp &
