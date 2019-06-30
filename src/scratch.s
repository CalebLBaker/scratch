MULTIBOOT_MAGIC equ 0xE85250D6
MULTIBOOT_CHECKSUM equ 0x17ADAF1A
PAGING_BIT_BAR equ 0x7FFFFFFF
PAGE_TABLE_START equ 0x1000
PAGE_TABLE_ENTRIES equ 0x1000
PAGE_TABLE_SIZE equ 0x1000

; Address of first table of a type OR'd with 3
; 3 represents present and readable
PDPT equ 0x2003
PDT equ 0x3003
PT equ 0x4003

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
	and eax, PAGING_BIT_BAR
	mov cr0, eax

	; Clear page tables
	mov edi, PAGE_TABLE_START   ; Set destination
	xor eax, eax                ; Clear eax
	mov ecx, PAGE_TABLE_ENTRIES ; Set number of page table entries to clear
	mov cr3, edi                ; Set cr3 to start of page table
	rep stosd                   ; Clear page tables
	mov edi, cr3                ; Reset destination

	; Populate the first entry of each table
	mov DWORD [edi], PDPT
	add edi, PAGE_TABLE_SIZE
	mov DWORD [edi], PDT
	add edi, PAGE_TABLE_SIZE
	mov DWORD [edi], PT
	add edi, PAGE_TABLE_SIZE

_loop:
	jmp _loop

