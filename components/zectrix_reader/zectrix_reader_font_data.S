#ifdef __APPLE__
.section __TEXT,__const
.global _zectrix_reader_font_data
_zectrix_reader_font_data:
#else
.section .rodata
.global zectrix_reader_font_data
zectrix_reader_font_data:
#endif
.incbin ZECTRIX_READER_FONT_PATH
#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
