#!/bin/sh
F=$1
gst-launch -m --gst-plugin-path=`pwd`/src/.libs \
	filesrc location=${F} ! siddecfp ! alsasink
