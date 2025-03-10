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
        }
}