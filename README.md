ODROID XU4 KVM enabled android test kernel
===========
this kernel is KVM enabled kernel for my test<br/>
you can build and use if you want

### Fixes of kernel
* enable LPAE for KVM
* KVM enabled
* add KVM/ARM commits for Cortex-A7
* LPAE DMA fix
* dts edit for KVM

### How to build
* do not use root for building kernel
* just "bash build_kernel.sh" in your kernel folder
