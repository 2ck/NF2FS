# NF2FS

The source code and test environment of paper: "Simplifying and Accelerating NOR Flash I/O Stack for RAM-Restricted Microcontrollers". Published on the ACM International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS'25), Rotterdam, The Netherlands.

## Introduction

NF2FS is a NOR flash file system that achieves high-performance I/O and long lifespan under extremely RAM restrictions (e.g., 2216 B in our tests), and the source code is in <font style="background: lightgrey">/NF2FS_code</font>.

We deploy NF2FS on Positive Atomic STM32H750 Polaris development board to test its I/O efficiency, and the source code is in <font style="background: lightgrey">/board_environment</font>.

We also construct an emulated NOR flash environment to verify the correctness of NF2FS on PC. Moreover,  since the lifespan test is time consuming (even on PC), we accelerate it through storage layout simulation. The source code are in <font style="background: lightgrey">/emulated_environment</font>.

Finally, the design and evaluation details are in the paper <font style="background: lightgrey">/doc/NF2FS.pdf</font>.

## Board Environment Setup

**1. Clone NF2FS from Github:**

~~~shell
git clone https://github.com/HNFFS/NF2FS.git
~~~

**2. Download Keil**
Keil uVision5 is available in https://www.keil.com/demo/eval/c51.htm.
<img src=".\image\Keil.jpg" alt="Keil" />

**3. Open the board environment**

 Open the file <font style="background: lightgrey">/board_environment/USER/NORENV.uvprojx</font> with Keil uVision5.

**4. Configurations**

First, click the button <font style="background: lightgrey">Options for Target</font>.

<img src=".\image\Options-button.png" alt="Options-button" />


Then, in <font style="background: lightgrey">Device</font>, choose STM32H750XBHx as the target development board.

<img src=".\image\Device.png" alt="Device" style="zoom: 50%;" />

In <font style="background: lightgrey">Target</font>, set <font style="background: lightgrey">Read/Only and Read/Write Memory Areas</font> to store to be downloaded binary.

<img src=".\image\Target.png" alt="Target" style="zoom:50%;" />

In C/C++, choose <font style="background: lightgrey">O0 optimization</font>, <font style="background: lightgrey">One ELF Section per Function</font>, <font style="background: lightgrey">C99 Mode</font>.

<img src=".\image\C-C++.png" alt="C-C++" style="zoom:50%;" />

Finally, Click the button <font style="background: lightgrey">OK</font> to save configurations.

<img src=".\image\OK.png" alt="OK" style="zoom:50%;" />

**5. Build binary**

Click the button <font style="background: lightgrey">Rebuild</font> to build the binary.

<img src=".\image\Rebuild.png" alt="Rebuild" />

**6. Link development board to PC**

We choose <font style="background: lightgrey">Positive Atomic STM32H750 Polaris</font> as the target development board, which uses <font style="background: lightgrey">ST-Link</font> to download binary. Moreover, we use <font style="background: lightgrey">XCOM</font> to receive message from board to PC.

The link details will be updated soon.

TODO

**7. Download binary to development board**

Finally, click the button <font style="background: lightgrey">Download</font>, and NF2FS will run on the development board automatically!

<img src=".\image\Download.png" alt="Download" />

## Emulated Environment Setup

**1. Clone NF2FS from Github:**

~~~shell
git clone https://github.com/HNFFS/NF2FS.git
cd ./emulated_environment
~~~

**2. Run NF2FS in emulated environment**

~~~shell
cd ./normal_test
make test
~~~

**3. Run Lifespan Test**

We conduct lifespan test through storage layout simulation, which are stored in <font style="background: lightgrey">/emulated_environment/lifespan_test</font> as jupyter files. The reason is that running lifespan test takes too much time to wear out NOR flash (e.g., 10K P/E cycle), even if in the emulated NOR flash environment.
