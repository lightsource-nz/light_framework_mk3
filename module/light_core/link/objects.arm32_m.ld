SECTIONS
{
    .light.modules :
    {
        . = ALIGN(4);
        __light_modules_start = .;
        KEEP(*(.light.modules))
        __light_modules_end = .;
    } > FLASH
}