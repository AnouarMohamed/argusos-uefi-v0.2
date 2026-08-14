ENTRY(argus_user_start)

SECTIONS
{
    . = 0x0000008000001000;
    .text : ALIGN(16)
    {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }

    ASSERT(SIZEOF(.text) <= 0x4000, "User app exceeds four code pages")
    ASSERT(SIZEOF(.data) == 0, "User app contains writable data")
    ASSERT(SIZEOF(.bss) == 0, "User app contains static storage")

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.*)
    }
}
