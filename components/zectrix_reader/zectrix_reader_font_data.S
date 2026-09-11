#ifdef __APPLE__
.section __TEXT,__const
.global _zectrix_reader_font_data
_zectrix_reader_font_data:
#else
.section .rodata
.global zectrix_reader_font_data
.type zectrix_reader_font_data, @object
zectrix_reader_font_data:
#endif
.incbin ZECTRIX_READER_FONT_PATH
#ifdef __APPLE__
.global _zectrix_reader_font_end
_zectrix_reader_font_end:
#else
.size zectrix_reader_font_data, . - zectrix_reader_font_data
.global zectrix_reader_font_end
zectrix_reader_font_end:
#endif
#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
