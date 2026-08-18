/* stm32f446xe.ld
 *
 * Full linker script for the STM32F446xE, on the same model as the F411 port's
 * stm32f411ce.ld: ST's cmsis_device_f4 ships register headers, startup and system_stm32f4xx.c
 * but no linker script (that lives in STM32CubeF4, deliberately not pulled in here).
 *
 * STM32F446xE: 512K flash at 0x08000000. SRAM1 (112K) and SRAM2 (16K) sit contiguously at
 * 0x20000000-0x2001FFFF (see cmsis_device_f4's stm32f446xx.h: SRAM2_BASE = SRAM1_BASE + 112K
 * exactly), so this models them as one 128K region rather than as two -- identical to the
 * F411's own memory map size for size, just on a chip with more peripherals behind it. No CCM
 * data RAM on this part (unlike the F407/F429 family), so there is nothing else to map.
 */

ENTRY(Reset_Handler)

MEMORY
{
        FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
        RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* the stack grows down from the top of RAM. _estack is what startup_stm32f446xx.s places in
 * the first vector-table entry, so the core loads it into MSP before a single instruction of
 * Reset_Handler runs -- a wrong value here fails before anything can report it */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* purely a link-time assertion, as on the F411 port -- it cannot catch runtime stack growth,
 * only a build whose statically-allocated data has already eaten the budget */
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x800;

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
