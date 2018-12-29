[bits 32]
; Multiboot header
dd 0xE85250D6
dd 0
dd 0x10
dd 0x17ADAF1A
; Null tag to terminate list of tags
dw 0
dw 0
dd 8

; Entry point
_start:
	mov eax, cr0
;	and eax, 0x7FFFFFFF
;	mov cr0, eax
;	mov edi, 0
;	xor eax, eax
;	mov ecx, 0x1000
;	mov cr3, edi

_loop:
	jmp _loop

