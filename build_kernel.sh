#!/bin/bash

#odroid-xu4 android kvm test kernel by boerck
#you can use this file by terminal "bash build_kernel.sh"



function f_timer {
    ELAPSEDTIME=$(($(date +"%s") - $1))
    printf "Kernel Build Elapsed Time: %02d:%02d:%05.2f\n" $((ELAPSEDTIME/60/60)) $((ELAPSEDTIME/60)) $((ELAPSEDTIME%60))
}




echo "######################################"
echo "Build Kernel - XU4 KVM android test"
echo "boerck"
echo "######################################"
echo ""
echo ""
sleep 3


echo "Clear Folder..." \
&& make clean \
&& make mrproper \
&& echo "Clear Complete!!" \
&& echo "" \
\
\
&& echo "Make Config..." \
&& make odroidxu4_defconfig \
&& echo "Make Config Complete!!" \
&& echo "" \
\
\
&& echo "Kernel Build Start..." \
&& echo "Build timer start!!" \
&& STARTTIME=$(date +"%s") \
&& make -j4 \
&& echo "Kernel Build Complete!!" \
&& f_timer $STARTTIME

sleep 3
echo ""
echo ""
echo "Build End"
