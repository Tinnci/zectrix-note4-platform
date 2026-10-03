#ifdef __APPLE__
.section __TEXT,__const
.global _note4_reader_font_data
_note4_reader_font_data:
#else
.section .rodata
.global note4_reader_font_data
.type note4_reader_font_data, @object
note4_reader_font_data:
#endif
.incbin NOTE4_READER_FONT_PATH
#ifdef __APPLE__
.global _note4_reader_font_end
_note4_reader_font_end:
#else
.size note4_reader_font_data, . - note4_reader_font_data
.global note4_reader_font_end
note4_reader_font_end:
#endif
#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
