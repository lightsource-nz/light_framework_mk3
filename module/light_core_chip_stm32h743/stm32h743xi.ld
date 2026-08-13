/* stm32h743xi.ld
 *
 * Full linker script for the STM32H743xI, as with the F411 -- cmsis_device_h7 ships register
 * headers, startup and system_stm32h7xx.c but no linker script.
 *
 * THIS PART DOES NOT HAVE "RAM". It has five separate memories on different buses, in
 * different power domains, with different reachability, and choosing between them is the
 * substance of this file rather than a detail of it:
 *
 *   ITCM   0x00000000   64K   instruction tightly-coupled. Zero wait state to the core, and
 *                             UNREACHABLE BY EVERY DMA ENGINE.
 *   DTCM   0x20000000  128K   data tightly-coupled. Zero wait state, also unreachable by DMA.
 *                             The fastest place for the stack and for hot data.
 *   AXI    0x24000000  512K   D1 domain. Reachable by MDMA and by the D1/D2 DMA engines, and
 *                             the largest single block. This is what a driver's DMA buffer
 *                             has to live in.
 *   SRAM1/2/3 0x30000000 288K D2 domain. Reachable by DMA1/DMA2, and the natural home for
 *                             peripheral buffers -- BUT the D2 SRAMs are NOT clocked out of
 *                             reset; RCC_AHB2ENR must enable them first. Declared here and
 *                             deliberately unused, because a linker script that places data
 *                             in unclocked memory produces an image that faults before main().
 *   SRAM4  0x38000000   64K   D3 domain, stays powered in low-power modes.
 *
 * The choice made here: everything in AXI SRAM, and the stack at the top of DTCM.
 *
 * AXI rather than DTCM for .data/.bss because a device driver added later will want DMA to
 * reach the framework's buffers, and moving them after the fact means moving them while
 * something depends on where they are. 512K also removes any question of fit.
 * Stack in DTCM because it is the one allocation that is pure core traffic, never touched by
 * DMA, and benefits most from zero wait states.
 *
 * Flash is 2MB in TWO BANKS of 1MB (0x08000000 and 0x08100000), 8 x 128K sectors each. Only
 * bank 1 is described below: a single image has no reason to straddle the gap, and the second
 * bank is what makes read-while-write and dual-image update possible later.
 */

ENTRY(Reset_Handler)

MEMORY
{
        FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
        DTCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
        AXI    (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
        SRAM_D2 (rwx): ORIGIN = 0x30000000, LENGTH = 288K
        SRAM_D3 (rwx): ORIGIN = 0x38000000, LENGTH = 64K
        ITCM   (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

/* stack at the top of DTCM. startup_stm32h743xx.s puts this in the first vector-table entry,
 * so the core loads it into MSP before Reset_Handler executes an instruction */
_estack = ORIGIN(DTCM) + LENGTH(DTCM);

_Min_Heap_Size = 0x800;
_Min_Stack_Size = 0x1000;

SECTIONS
{
        .isr_vector :
        {
                . = ALIGN(4);
                KEEP(*(.isr_vector))
                . = ALIGN(4);
        } > FLASH

        .text :
        {
                . = ALIGN(4);
                *(.text)
                *(.text*)
                *(.glue_7)
                *(.glue_7t)
                *(.eh_frame)
                KEEP(*(.init))
                KEEP(*(.fini))
                . = ALIGN(4);
                _etext = .;
        } > FLASH

        .rodata :
        {
                . = ALIGN(4);
                *(.rodata)
                *(.rodata*)
                . = ALIGN(4);
        } > FLASH

        /* the framework's static-registration sections. KEEP is not optional -- nothing holds
         * a reference to these objects, being found by a linker walk IS how they are
         * referenced, so --gc-sections would discard every one */
        .light.stream :
        {
                . = ALIGN(4);
                __light_streams_start = .;
                KEEP(*(.light.static_stream))
                . = ALIGN(4);
                __light_streams_end = .;
        } > FLASH
        .light.module :
        {
                . = ALIGN(4);
                __light_modules_start = .;
                KEEP(*(.light.static_module))
                . = ALIGN(4);
                __light_modules_end = .;
        } > FLASH
        .light.static :
        {
                . = ALIGN(4);
                __light_statics_start = .;
                KEEP(*(.light.static))
                . = ALIGN(4);
                __light_statics_end = .;
        } > FLASH

        .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
        .ARM : {
                __exidx_start = .;
                *(.ARM.exidx*)
                __exidx_end = .;
        } > FLASH

        .preinit_array :
        {
                PROVIDE_HIDDEN(__preinit_array_start = .);
                KEEP(*(.preinit_array*))
                PROVIDE_HIDDEN(__preinit_array_end = .);
        } > FLASH
        .init_array :
        {
                PROVIDE_HIDDEN(__init_array_start = .);
                KEEP(*(SORT(.init_array.*)))
                KEEP(*(.init_array*))
                PROVIDE_HIDDEN(__init_array_end = .);
        } > FLASH
        .fini_array :
        {
                PROVIDE_HIDDEN(__fini_array_start = .);
                KEEP(*(SORT(.fini_array.*)))
                KEEP(*(.fini_array*))
                PROVIDE_HIDDEN(__fini_array_end = .);
        } > FLASH

        /* initialised data: stored in flash, copied to AXI SRAM by Reset_Handler. The startup
         * file reads _sidata/_sdata/_edata by exactly these names */
        _sidata = LOADADDR(.data);
        .data :
        {
                . = ALIGN(4);
                _sdata = .;
                *(.data)
                *(.data*)
                . = ALIGN(4);
                _edata = .;
        } > AXI AT> FLASH

        . = ALIGN(4);
        .bss :
        {
                _sbss = .;
                __bss_start__ = _sbss;
                *(.bss)
                *(.bss*)
                *(COMMON)
                . = ALIGN(4);
                _ebss = .;
                __bss_end__ = _ebss;
        } > AXI

        /* heap grows from `end` in AXI SRAM. The stack is NOT here -- it is in DTCM, at
         * _estack -- so this reserves heap only, and the _Min_Stack_Size check below is
         * against DTCM instead */
        ._user_heap :
        {
                . = ALIGN(8);
                PROVIDE(end = .);
                PROVIDE(_end = .);
                . = . + _Min_Heap_Size;
                . = ALIGN(8);
        } > AXI

        /* link-time assertion only: it cannot see runtime stack growth, just a build whose
         * static allocation has already eaten the budget */
        ASSERT(_Min_Stack_Size <= LENGTH(DTCM), "stack does not fit in DTCM")

        .ARM.attributes 0 : { *(.ARM.attributes) }
}
