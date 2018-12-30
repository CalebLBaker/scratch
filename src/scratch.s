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

	; Set up paging

	; Disable old paging
	mov eax, cr0
	and eax, 0x7FFFFFFF
	mov cr0, eax

	; Clear page tables
	mov edi, 0x4000     ; Set destination (0x0)
	xor eax, eax        ; Clear eax
	mov ecx, 0x1000     ; Set number of page table entries to clear
	mov cr3, edi        ; Set cr3 to start of page table
	rep stosd           ; Clear page tables
	mov edi, cr3        ; Reset destination

_loop:
	jmp _loop

