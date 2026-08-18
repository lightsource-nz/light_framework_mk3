/* stm32f103c8.ld
 *
 * Full linker script for the STM32F103C8T6 (BluePill/BluePill+), on the same model as the
 * F411 port's stm32f411ce.ld: ST's cmsis_device_f1 ships register headers, startup and
 * system_stm32f1xx.c but no linker script of its own (that lives in STM32CubeF1, which this
 * port deliberately does not pull in), so the whole memory layout is defined here rather than
 * borrowed from ST's community Ac6-style templates.
 *
 * STM32F103C8T6: 64K flash at 0x08000000, 20K SRAM at 0x20000000 -- the "medium-density, low
 * end" member of the family the STM32F103xB CMSIS symbol covers (which spans both the 64K C8
 * and 128K CB parts; the peripheral set is identical, only this memory map differs).
 */

ENTRY(Reset_Handler)

MEMORY
{
        FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
        RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* the stack grows down from the top of RAM. _estack is what startup_stm32f103xb.s places in
 * the first vector-table entry, so the core loads it into MSP before a single instruction of
 * Reset_Handler runs -- a wrong value here fails before anything can report it */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* purely a link-time assertion, as on the F411 port -- it cannot catch runtime stack growth,
 * only a build whose statically-allocated data has already eaten the budget. Kept smaller than
 * the F411's, in proportion to this part's much smaller RAM */
_Min_Heap_Size = 0x100;
_Min_Stack_Size = 0x400;

SECTIONS
{
        /* must be first in flash: the core fetches the initial MSP and the reset vector from
         * the first two words at 0x08000000 */
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

        /* the framework's static-registration sections -- see stm32f411ce.ld, whose comment
         * applies verbatim here: KEEP is not optional, nothing else references these objects */
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

        /* initialised data: stored in flash, copied to RAM by Reset_Handler */
        _sidata = LOADADDR(.data);
        .data :
        {
                . = ALIGN(4);
                _sdata = .;
                *(.data)
                *(.data*)
                . = ALIGN(4);
                _edata = .;
        } > RAM AT> FLASH

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
        } > RAM

        /* newlib's sbrk grows the heap from `end` -- named both ways, as on the F411 port */
        ._user_heap_stack :
        {
                . = ALIGN(8);
                PROVIDE(end = .);
                PROVIDE(_end = .);
                . = . + _Min_Heap_Size;
                . = . + _Min_Stack_Size;
                . = ALIGN(8);
        } > RAM

        .ARM.attributes 0 : { *(.ARM.attributes) }
}
