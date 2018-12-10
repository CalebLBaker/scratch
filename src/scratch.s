# Multiboot header
dd 0xE85250D6
dd 0
dd 0x10
dd 0x17ADAF1A
# Null tag to terminate list of tags
dw 0
dw 0
dd 8
_start:
jmp _start
