MULTIBOOT_MAGIC equ 0xE85250D6
MULTIBOOT_CHECKSUM equ 0x17ADAF1A
PAGING_BIT_BAR equ 0x7FFFFFFF
PAGE_TABLE_START equ 0x1000
PAGE_TABLE_ENTRIES equ 0x1000

[bits 32]
; Multiboot header
dd MULTIBOOT_MAGIC
dd 0    ; Flags
dd 0x10 ; Header Length
dd MULTIBOOT_CHECKSUM
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
	mov edi, 0x1000     ; Set destination
	xor eax, eax        ; Clear eax
	mov ecx, 0x1000     ; Set number of page table entries to clear
	mov cr3, edi        ; Set cr3 to start of page table
	rep stosd           ; Clear page tables
	mov edi, cr3        ; Reset destination

_loop:
	jmp _loop

