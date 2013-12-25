#!/bin/bash

while true
do
   link=0
   while [ $link -eq 0 ]
   do
      if ! iwconfig wlan0 | grep "SOOM-WORKS" > /dev/null
      then
           echo "link is down, restarting wlan0..."
           ifdown wlan0
           sleep 5
           ifup wlan0
      else
           echo "link is up, waiting 30 seconds..."
           link=1
      fi
    done
    sleep 30
done
exit(1)

