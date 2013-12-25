#!/bin/bash

#export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

pid=`ps -ef | grep "/home/pi/soomap/vipserver" | grep -v 'grep' | awk '{print $2}'`

if [ -z $pid ]; then

   echo $(date)
   
   /home/pi/soomap/vipserver d

   echo ""
fi

