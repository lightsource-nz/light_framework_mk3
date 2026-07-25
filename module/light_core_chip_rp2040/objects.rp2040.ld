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
/* without this, these sections land wherever this script happens to be included relative to
 * pico-sdk's own memmap script -- which put .light.stream at the very start of flash (0x10000000),
 * ahead of .boot2. the RP2040 boot ROM requires boot2 at exactly that address (it checksums the
 * first 256 bytes as boot2); with something else occupying even the first few bytes, the checksum
 * never validates and the ROM silently falls back to its USB bootloader instead of ever running
 * this firmware.
 *
 * AFTER .text, not AFTER .boot2: pico-sdk's own linker script (section_boot2.incl,
 * sections_default_text.incl) has no hardcoded address for the vector table -- the required
 * "immediately after boot2, at flash+0x100" placement is achieved purely by boot2 being exactly
 * 256 bytes (its own ASSERT enforces this) with .text, which starts with the vector table,
 * following it with nothing in between. inserting here instead of after .boot2 preserves that
 * adjacency */
INSERT AFTER .text;