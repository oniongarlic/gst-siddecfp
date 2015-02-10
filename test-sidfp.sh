#!/bin/sh
F=$1
GSL=gst-launch-1.0

case "$2" in
tune)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp tune=$3 ! alsasink
;;
mono)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp ! alsasink
;;
stereo)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp ! alsasink
;;
pal)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp clock=pal ! alsasink
;;
ntsc)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp clock=ntsc ! alsasink
;;
*)
 ${GSL} -m --gst-plugin-path=`pwd`/src/.libs filesrc location=${F} ! siddecfp ! alsasink
;;
esac
