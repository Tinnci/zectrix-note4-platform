#ifdef __APPLE__
.section __TEXT,__const
.global _note4_book_web_html
_note4_book_web_html:
#else
.section .rodata
.global note4_book_web_html
note4_book_web_html:
#endif
.incbin NOTE4_BOOK_WEB_PATH
.byte 0
#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
