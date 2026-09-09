#ifdef __APPLE__
.section __TEXT,__const
.global _zectrix_book_web_html
_zectrix_book_web_html:
#else
.section .rodata
.global zectrix_book_web_html
zectrix_book_web_html:
#endif
.incbin ZECTRIX_BOOK_WEB_PATH
.byte 0
#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
