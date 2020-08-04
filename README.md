ODROID XU4 KVM enabled android test kernel
===========
this kernel is KVM enabled kernel for my test<br/>
you can build and use if you want

### Fixes of kernel
* enable LPAE for KVM
* KVM enabled
* [add KVM/ARM commits for Cortex-A7](https://bugs.launchpad.net/arndale/+bug/1088845)
* [LPAE DMA fix](https://lists.cs.columbia.edu/pipermail/kvmarm/2013-October/006069.html)
* dts edit for KVM

### How to build
* just "bash build_kernel.sh" in your kernel folder

### Credits
* [KVM support for the Odroid-XU3](https://github.com/hardkernel/linux/commit/d6dda5c5b1b937e64b56495bf4330829386189b0)

