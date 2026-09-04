#!/usr/bin/env bash

for i in {1..100}; do
    rmmod tapedev
    insmod tapedev.ko
done