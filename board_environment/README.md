# Nor flash file system test environment for STM32H750XBH6

## Introduction
The project is supported by [ALIENTEK](www.alientek.com). 
We build the project based on the FATFS template. 
We also migrate FreeRTOS for further implementation. 

## Compile
Compiled by Keil uVision v5.38.0.0. The project file is located at `./USER/NORENV.uvprojx`.

## Important Note

- Choose device as `STM32H750XBHx`

- Select C99 mode

- Using USMART software as the serial port terminal

- DO NOT select `Use MicroLib` in the project, or the program will not work, see details in `./CORE/startup_stm32h750xx.s` as below:

    ```asm
    ...
    ;*******************************************************************************
    ; User Stack and Heap initialization
    ;*******************************************************************************
                    IF      :DEF:__MICROLIB
                    
                    EXPORT  __initial_sp
                    EXPORT  __heap_base
                    EXPORT  __heap_limit
                    
                    ELSE
                    
                    IMPORT  __use_two_region_memory
                    EXPORT  __user_initial_stackheap
                    
    __user_initial_stackheap

                    LDR     R0, =  Heap_Mem
                    LDR     R1, =(Stack_Mem + Stack_Size)
                    LDR     R2, = (Heap_Mem +  Heap_Size)
                    LDR     R3, = Stack_Mem
                    BX      LR

                    ALIGN

                    ENDIF

                    END
    ```