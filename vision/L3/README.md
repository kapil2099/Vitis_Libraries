## Level 3: Pipeline Applications

This directory contains whole applications formed by stitching a pipleine of xfOpenCV funtions. The host code shows how to call this multiple functions in OpenCL.

'examples' folder contains the OpenCL host code file and a C++ accel file that demonstrate the call of xfOpenCV functions to build for Vitis.

'tests' folder has sub-folders named according to the function and the configuration it would run. Each individual folder has Makefiles and config files that would perform software emulation, hardware emulation and hardware build and run of a given function, based on the 'Board' the user selects.

### Commands to run:

source < path-to-Vitis-installation-directory >/settings64.sh

source < part-to-XRT-installation-directory >/setenv.sh

export DEVICE=< path-to-platform-directory >/<platform>.xpfm

**For PCIe devices:**

make host xclbin TARGET=< sw_emu|hw_emu|hw >

**For embedded devices:**

export SYSROOT=< path-to-platform-sysroot >

make host xclbin TARGET=hw BOARD=Zynq 