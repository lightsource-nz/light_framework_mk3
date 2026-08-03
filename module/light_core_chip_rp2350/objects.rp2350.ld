SECTIONS
{
        .light.stream :
        {
                . = ALIGN(4);
                __light_streams_start = .;
                KEEP(*(.light.static_stream))
                __light_streams_end = .;
        } > FLASH
        .light.module :
        {
                . = ALIGN(4);
                __light_modules_start = .;
                KEEP(*(.light.static_module))
                __light_modules_end = .;
        } > FLASH
        .light.static :
        {
                . = ALIGN(4);
                __light_statics_start = .;
                KEEP(*(.light.static))
                __light_statics_end = .;
        } > FLASH
}
/* mirrors light_core_chip_rp2040/objects.rp2040.ld exactly -- this only places sections
 * relative to .text/FLASH, nothing RP2040-specific (no hardcoded addresses), so the same
 * reasoning applies unchanged on RP2350: without this, these sections land wherever this
 * script happens to be included relative to pico-sdk's own memmap script, which risks
 * displacing whatever the boot ROM expects at the very start of flash. AFTER .text, not
 * AFTER .boot2, for the same reason as the rp2040 version: pico-sdk's own linker script has
 * no hardcoded address for the vector table -- it follows immediately after whatever comes
 * first in the image (boot stage on rp2040; rp2350's own boot path is handled entirely by
 * pico-sdk's own linker script before this point), so inserting after .text preserves
 * whatever adjacency pico-sdk's own script already establishes, without this script needing
 * to know the specifics of that boot mechanism itself */
INSERT AFTER .text;
