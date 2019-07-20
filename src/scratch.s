MULTIBOOT_MAGIC equ 0xE85250D6
MULTIBOOT_CHECKSUM equ 0x17ADAF1A
PAGING_BIT equ 0x80000000
PAGING_BIT_BAR equ 0x7FFFFFFF
PAGE_TABLE_START equ 0x1000
PAGE_TABLE_ENTRIES equ 0x1000
PAGE_SIZE equ 0x1000
PAE_BIT equ 0x20

EFER_MSR equ 0xC0000080 ; Extended feature enable register model specific register
LONG_MODE equ 0x100

; Address of first table of a type OR'd with 3
; 3 represents present and readable
PDPT equ 0x2003
PDT equ 0x3003
PT equ 0x4003

; These parameters will identity map the first 2 Megabytes
MEM_START equ 3		; Address (OR'd with 3 of first mapped physical memory
NUM_PAGES equ 0x200 ; Number of memory pages


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

gdt:
	; Null Descriptor
	dw 0xFFFF ; Limit (low)
	dw 0      ; Base (low)
	db 0      ; Base (middle)
	db 0      ; Access
	db 1      ; Granularity
	db 0      ; Base (high)

	; Code Segment
	dw 0      ; Limit (low)
	dw 0      ; Base (low)
	db 0      ; Base (middle)
	db 0x9A   ; Access (execute/read)
	db 0xAF   ; Granularity, 64 bit flag, limit19:16
	db 0      ; Base (high)

	; Data Segment
	dw 0      ; Limit (low)
	dw 0      ; Base (low)
	db 0      ; Base (middle)
	db 0x92   ; Access (read/write)
	db 0      ; Granularity
	db 0      ; Base (high)

gdt_pointer:
	dw 0x17   ; Limit



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
	add edi, PAGE_SIZE
	mov DWORD [edi], PDT
	add edi, PAGE_SIZE
	mov DWORD [edi], PT

	; Populate first page table
	add edi, PAGE_SIZE
	mov ebx, MEM_START
	mov ecx, NUM_PAGES
initPageTables:
	mov [edi], ebx
	add ebx, PAGE_SIZE
	add edi, 8
	dec ecx
	jnz initPageTables

	; Enable PAE paging
	mov eax, cr4
	or eax, PAE_BIT
	mov cr4, eax

	; Switch to long mode
	mov ecx, EFER_MSR
	rdmsr
	or eax, LONG_MODE
	wrmsr

	; Enable paging
	mov eax, cr0
	or eax, PAGING_BIT
	mov cr0, eax

	; Switch to long mode



loop:
	jmp loop

