#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#define EQUALS(left,right,size) ((left).len == (size) && !strncmp((left).d, (right), (size)))

// Can represent either a block of multiple instructions
// or a single control flow instruction
typedef struct Block {
	void   *next;		// Pointer to next block
	void   *data;		// For multi-instruction blocks, the encoded instructions;
	                    // For single instructions, the target label
	size_t  capacity;	// Capacity of buffer pointed to by data
	size_t  size;		// Size of the block
	size_t  address;	// Starting address of the block
	size_t  line_num;	// Line number for first instruction of the block
	int32_t operand;	// Single instruction only: encoded operand
	uint16_t opcode;	// Opcode of the instruction; BLOCK for a multi-instruction block
	bool    long_mode;	// true for 64 bit mode, false for 32 bit mode
} Block;

typedef struct String {
	char   *d;
	size_t  len;
} String;

typedef struct Label {
	Block  *target;
	char   *name;
	size_t  name_len;
} Label;


size_t hashString(char *d, size_t len) {
	size_t hash = 5381;
	for (size_t i = 0; i < len; i++) {
		hash = ((hash << 5) + hash) + d[i];
	}
	return hash;
}

#define DTYPE Label
#define QTYPE String
#define D_HASH(d) (hashString((d)->name, (d)->name_len))
#define Q_HASH(q) (hashString((q)->d, (q)->len))
#define DELETE(d) if ((d).name) {   \
	free((d).name);                 \
}
#define VALID(d) ((d).name)
#define EQ(d,q) (EQUALS((*q),(d).name,(d).name_len))
#include "hash-map.h"

typedef struct Constant {
	char   *name;
	size_t  name_len;
	ssize_t  val;
} Constant;

#undef DTYPE
#define DTYPE Constant
#include "hash-map.h"

#define SUCCESS 0
#define USAGE_ERROR -1
#define IO_ERROR -2
#define SYNTAX_ERROR -3
#define SEMANTIC_ERROR -4
#define FEATURE_NOT_IMPLEMENTED_YET -5
#define ERROR -6

#define ELF_MAGIC_NUMBER 0x464C457F
#define SIXTY_FOUR_BIT 2
#define LITTLE 1
#define ORIGINAL_ELF 1
#define SYSTEM_V 0
#define X86_64 0x3E

#define ELF_HEADER_SIZE 0x40
#define PH_ENTRY_SIZE 0x38

#define LOAD 1
#define R_X 5
#define ET_EXEC 2

#define BLOCK_START_SIZE 0x40

#define LABEL_MAP_START_SIZE 0x40
#define LABEL_MAP_START_MASK 0x3F
#define LABEL_MAX_LOAD_FACTOR 0.9
#define LABEL_RESIZE_SHIFT 2
#define LABEL_RESIZE_MASK 3

#define BLOCK 0

#define SHORT_JMP 0xEB
#define NEAR_JMP 0xE9

#define SHORT_JNZ 0x75
#define NEAR_JNZ 0x0F85

#define MOV_R_CR 0x200F
#define MOV_CR_R 0x220F
#define MOVB 0x88
#define MOVB_M_R 0x88
#define MOVB_R_M 0x8A
#define MOVW 0x89
#define MOVL 0x89
#define MOVW_M_R 0x89
#define MOVL_M_R 0x89
#define MOVW_R_M 0x8B
#define MOVL_R_M 0x8B
#define MOVB_I 0xB0
#define MOVW_I 0xB8
#define MOVL_I 0xB8

#define STB_I 0xC6
#define STW_I 0xC7
#define STL_I 0xC7

#define IB 0x80
#define IW 0x81
#define IL 0x81

#define REG_DEST 0xC0

#define ADD 0x00
#define OR  0x08
#define AND 0x20
#define XOR 0x30

#define DEC 0x48

#define REP_STOSB 0xAAF3
#define REP_STOSW 0xABF3
#define REP_STOSD 0xABF3

#define L 1

#define AL_I 4
#define AX_I 5
#define EAX_I 5

#define DIRECT 0xC0
#define INDIRECT 0

#define INVALID_REGISTER -1

#define MAX(a,b) ((a)<(b) ? (b) : (a));

typedef struct ElfHeader {
	uint32_t    ident_mag;
	uint8_t     ident_class;
	uint8_t     ident_data;
	uint8_t     ident_version;
	uint8_t     ident_os_abi;
	uint8_t     ident_abi_version;
	uint8_t     ident_pad_one;
	uint16_t    ident_pad_two;
	uint32_t    ident_pad_three;
	uint16_t    type;
	uint16_t    machine;
	uint32_t    version;
	uint64_t    entry;
	uint64_t    phoff;
	uint64_t    shoff;
	uint32_t    flags;
	uint16_t    ehsize;
	uint16_t    phentsize;
	uint16_t    phnum;
	uint16_t    shentsize;
	uint16_t    shnum;
	uint16_t    shstrndx;
} ElfHeader;

typedef struct ElfProgramHeader {
	uint32_t    type;
	uint32_t    flags;
	uint64_t    offset;
	uint64_t    vaddr;
	uint64_t    paddr;
	uint64_t    filesz;
	uint64_t    memsz;
	uint64_t    align;
} ElfProgramHeader;

ConstantMap constants;
char *infile_name = NULL;
size_t line_num = 0;
bool long_mode = true;

/*
 * Reads an identifier from a character buffer, ignoring whitespace
 * Param buffer: character buffer that the identifier is read from
                 (must be null or space terminated to avoid index out of bounds errors)
 * Param id:     output variable that will be set to the identifier
 * Returns:      The number of skipped whitespace characters at the front of buffer
*/
size_t getIdentifier(char *buffer, String *id) {
	size_t ret = 0;
	for (id->d = buffer; isspace(*id->d); id->d++) ret++;
	for (id->len = 0;
	     id->d[id->len] && (isalnum(id->d[id->len]) || id->d[id->len] == '_');
		 id->len++);
	return ret;
}

Block* makeRoom(Block *curr_block, size_t size) {
	if (curr_block->opcode != BLOCK) {
		Block *n = malloc(sizeof(Block));
		n->next = NULL;
		n->data = malloc(BLOCK_START_SIZE);
		n->capacity = BLOCK_START_SIZE;
		n->address = curr_block->address + curr_block->size;
		n->size = 0;
		n->opcode = BLOCK;
		n->line_num = line_num;
		curr_block->next = n;
		return n;
	}
	else if (curr_block->capacity < curr_block->size + size) {
		size_t new_capacity = MAX(curr_block->capacity << 1, BLOCK_START_SIZE);
		curr_block->data = realloc(curr_block->data, new_capacity);
		curr_block->capacity = new_capacity;
	}
	return curr_block;
}

size_t setJmpOperand(Block *curr_block) {
	curr_block->operand = ((Block*)(curr_block->data))->address - curr_block->address - curr_block->size;
	int16_t op = curr_block->opcode;
	if (curr_block->operand > INT8_MAX && (op == SHORT_JMP || op == SHORT_JNZ)) {
		size_t ret;
		if (op == SHORT_JMP) {
			ret = curr_block->long_mode ? 3 : 1;
			curr_block->opcode = NEAR_JMP;
		}
		else {
			ret = curr_block->long_mode ? 4 : 2;
			curr_block->opcode = NEAR_JNZ;
		}
		curr_block->size += ret;
		return ret;
	}
	return 0;
}

int16_t getRegisterWidth(String *r) {
	size_t len = r->len;
	char first_char = *(r->d);
	char last_char = r->d[len-1];
	if (last_char == 'l' || last_char == 'h') {
		return 8;
	}
	else if (last_char == 'w' || len == 2) {
		return 16;
	}
	else if (first_char == 'e' || last_char == 'd' || first_char == 'c' || first_char == 'd') {
		return 32;
	}
	else if (first_char == 'r' || first_char == 'm') {
		return 64;
	}
	else if (first_char == 's') {
		return 80;
	}
	else if (first_char == 'x') {
		return 128;
	}
	else if (first_char == 'y') {
		return 256;
	}
	else {
		return INVALID_REGISTER;
	}
}

int8_t encodeRegister(String *r) {
	if (EQUALS(*r,"al",2) || EQUALS(*r,"ax",2) || EQUALS(*r,"eax",3) || EQUALS(*r,"rax",3) || EQUALS(*r,"st0",3)
	 || EQUALS(*r,"mmx0",4) || EQUALS(*r,"xmm0",4) || EQUALS(*r,"ymm0",4) || EQUALS(*r,"es",2) || EQUALS(*r,"cr0",3) || EQUALS(*r,"dr0",3)) {
		return 0;
	}
	else if (EQUALS(*r,"cl",2) || EQUALS(*r,"cx",2) || EQUALS(*r,"ecx",3) || EQUALS(*r,"rcx",3) || EQUALS(*r,"st1",3)
	 || EQUALS(*r,"mmx1",4) || EQUALS(*r,"xmm1",4) || EQUALS(*r,"ymm1",4) || EQUALS(*r,"cs",2) || EQUALS(*r,"cr1",3) || EQUALS(*r,"dr1",3)) {
		return 1;
	}
	else if (EQUALS(*r,"dl",2) || EQUALS(*r,"dx",2) || EQUALS(*r,"edx",3) || EQUALS(*r,"rdx",3) || EQUALS(*r,"st2",3)
	 || EQUALS(*r,"mmx2",4) || EQUALS(*r,"xmm2",4) || EQUALS(*r,"ymm2",4) || EQUALS(*r,"ss",2) || EQUALS(*r,"cr2",3) || EQUALS(*r,"dr2",3)) {
		return 2;
	}
	else if (EQUALS(*r,"bl",2) || EQUALS(*r,"bx",2) || EQUALS(*r,"ebx",3) || EQUALS(*r,"rbx",3) || EQUALS(*r,"st3",3)
	 || EQUALS(*r,"mmx3",4) || EQUALS(*r,"xmm3",4) || EQUALS(*r,"ymm3",4) || EQUALS(*r,"ds",2) || EQUALS(*r,"cr3",3) || EQUALS(*r,"dr3",3)) {
		return 3;
	}
	else if (EQUALS(*r,"ah",2) || EQUALS(*r,"spl",3) || EQUALS(*r,"sp",2) || EQUALS(*r,"esp",3) || EQUALS(*r,"rsp",3) || EQUALS(*r,"st4",3)
	 || EQUALS(*r,"mmx4",4) || EQUALS(*r,"xmm4",4) || EQUALS(*r,"ymm4",4) || EQUALS(*r,"fs",2) || EQUALS(*r,"cr4",3) || EQUALS(*r,"dr4",3)) {
		return 4;
	}
	else if (EQUALS(*r,"ch",2) || EQUALS(*r,"bpl",3) || EQUALS(*r,"bp",2) || EQUALS(*r,"ebp",3) || EQUALS(*r,"rbp",3) || EQUALS(*r,"st5",3)
	 || EQUALS(*r,"mmx5",4) || EQUALS(*r,"xmm5",4) || EQUALS(*r,"ymm5",4) || EQUALS(*r,"gs",2) || EQUALS(*r,"cr5",3) || EQUALS(*r,"dr5",3)) {
		return 5;
	}
	else if (EQUALS(*r,"dh",2) || EQUALS(*r,"sil",3) || EQUALS(*r,"si",2) || EQUALS(*r,"esi",3) || EQUALS(*r,"rsi",3) || EQUALS(*r,"st6",3)
	 || EQUALS(*r,"mmx6",4) || EQUALS(*r,"xmm6",4) || EQUALS(*r,"ymm6",4) || EQUALS(*r,"cr6",3) || EQUALS(*r,"dr6",3)) {
		return 6;
	}
	else if (EQUALS(*r,"bh",2) || EQUALS(*r,"dil",3) || EQUALS(*r,"di",2) || EQUALS(*r,"edi",3) || EQUALS(*r,"rdi",3) || EQUALS(*r,"st7",3)
	 || EQUALS(*r,"mmx7",4) || EQUALS(*r,"xmm7",4) || EQUALS(*r,"ymm7",4) || EQUALS(*r,"cr7",3) || EQUALS(*r,"dr7",3)) {
		return 7;
	}
	else if (EQUALS(*r,"r8l",3) || EQUALS(*r,"r8w",3) || EQUALS(*r,"r8d",3) || EQUALS(*r,"r8",2) 
	 || EQUALS(*r,"mmx8",4) || EQUALS(*r,"xmm8",4) || EQUALS(*r,"ymm8",4) || EQUALS(*r,"cr8",3) || EQUALS(*r,"dr8",3)) {
		return 8;
	}
	else if (EQUALS(*r,"r9l",3) || EQUALS(*r,"r9w",3) || EQUALS(*r,"r9d",3) || EQUALS(*r,"r9",2) 
	 || EQUALS(*r,"mmx9",4) || EQUALS(*r,"xmm9",4) || EQUALS(*r,"ymm9",4) || EQUALS(*r,"cr9",3) || EQUALS(*r,"dr9",3)) {
		return 9;
	}
	else if (EQUALS(*r,"r10l",3) || EQUALS(*r,"r10w",3) || EQUALS(*r,"r10d",3) || EQUALS(*r,"r10",2) 
	 || EQUALS(*r,"mmx10",4) || EQUALS(*r,"xmm10",4) || EQUALS(*r,"ymm10",4) || EQUALS(*r,"cr10",3) || EQUALS(*r,"dr10",3)) {
		return 10;
	}
	else if (EQUALS(*r,"r11l",3) || EQUALS(*r,"r11w",3) || EQUALS(*r,"r11d",3) || EQUALS(*r,"r11",2) 
	 || EQUALS(*r,"mmx11",4) || EQUALS(*r,"xmm11",4) || EQUALS(*r,"ymm11",4) || EQUALS(*r,"cr11",3) || EQUALS(*r,"dr11",3)) {
		return 11;
	}
	else if (EQUALS(*r,"r12l",3) || EQUALS(*r,"r12w",3) || EQUALS(*r,"r12d",3) || EQUALS(*r,"r12",2) 
	 || EQUALS(*r,"mmx12",4) || EQUALS(*r,"xmm12",4) || EQUALS(*r,"ymm12",4) || EQUALS(*r,"cr12",3) || EQUALS(*r,"dr12",3)) {
		return 10;
	}
	else if (EQUALS(*r,"r13l",3) || EQUALS(*r,"r13w",3) || EQUALS(*r,"r13d",3) || EQUALS(*r,"r13",2) 
	 || EQUALS(*r,"mmx13",4) || EQUALS(*r,"xmm13",4) || EQUALS(*r,"ymm13",4) || EQUALS(*r,"cr13",3) || EQUALS(*r,"dr13",3)) {
		return 13;
	}
	else if (EQUALS(*r,"r14l",3) || EQUALS(*r,"r14w",3) || EQUALS(*r,"r14d",3) || EQUALS(*r,"r14",2) 
	 || EQUALS(*r,"mmx14",4) || EQUALS(*r,"xmm14",4) || EQUALS(*r,"ymm14",4) || EQUALS(*r,"cr14",3) || EQUALS(*r,"dr14",3)) {
		return 14;
	}
	else if (EQUALS(*r,"r15l",3) || EQUALS(*r,"r15w",3) || EQUALS(*r,"r15d",3) || EQUALS(*r,"r15",2) 
	 || EQUALS(*r,"mmx15",4) || EQUALS(*r,"xmm15",4) || EQUALS(*r,"ymm15",4) || EQUALS(*r,"cr15",3) || EQUALS(*r,"dr15",3)) {
		return 15;
	}
	else {
		return INVALID_REGISTER;
	}
}

bool encodeInstruction(Block *curr_block, char *operands, uint8_t opcode) {
	String src, dest;
	getIdentifier(operands, &dest);
	char *s = dest.d + dest.len;
	if (*s != ',') {
		fprintf(stderr, "Assembler Error (%s:%lu): Expected comma\n", infile_name, line_num);
		return false;
	}
	getIdentifier(s+1, &src);
	Constant *c = ConstantMapGet(&constants, &src);

	// Immediate or constant source
	if (c || isdigit(src.d[0])) {
		ssize_t val;
		if (c) {
			val = c->val;
		}
		else {
			sscanf(src.d, "%li", &val);
		}
		if (EQUALS(dest,"al",2)) {
			curr_block = makeRoom(curr_block, 2);
			((uint8_t*)curr_block->data)[curr_block->size] = opcode | AL_I;
			*((int8_t*)(curr_block->data + curr_block->size + 1)) = (int8_t)val;
			curr_block->size += 2;
		}
		else if (EQUALS(dest,"ax",2)) {
			curr_block = makeRoom(curr_block, 3);
			((uint8_t*)curr_block->data)[curr_block->size] = opcode | AX_I;
			*((int16_t*)(curr_block->data + curr_block->size + 1)) = (int16_t)val;
			curr_block->size += 3;
		}
		else if (EQUALS(dest,"eax",3)) {
			curr_block = makeRoom(curr_block, 5);
			((uint8_t*)curr_block->data)[curr_block->size] = opcode | EAX_I;
			*((int32_t*)(curr_block->data + curr_block->size + 1)) = (int32_t)val;
			curr_block->size += 5;
		}
		else {
			int8_t instruction_size;
			int8_t dest_reg = encodeRegister(&dest);
			switch(getRegisterWidth(&dest)) {
				case 8: {
					curr_block = makeRoom(curr_block, 3);
					int16_t inst = ((int16_t)(REG_DEST | opcode | dest_reg) << 8) | IB;
					*((int16_t*)(curr_block->data + curr_block->size)) = inst;
					*((int8_t*)(curr_block->data + curr_block->size + 2)) = (int8_t)val;
					curr_block->size += 3;
					break;
				}
				case 16: {
					curr_block = makeRoom(curr_block, 4);
					int16_t inst = ((int16_t)(REG_DEST | opcode | dest_reg) << 8) | IW;
					*((int16_t*)(curr_block->data + curr_block->size)) = inst;
					*((int16_t*)(curr_block->data + curr_block->size + 2)) = (int16_t)val;
					curr_block->size += 4;
					break;
				}
				case 32: {
					curr_block = makeRoom(curr_block, 6);
					int16_t inst = ((int16_t)(REG_DEST | opcode | dest_reg) << 8) | IL;
					*((int16_t*)(curr_block->data + curr_block->size)) = inst;
					*((int32_t*)(curr_block->data + curr_block->size + 2)) = (int32_t)val;
					curr_block->size += 6;
					break;
				}
				default: {
					printf("Assembler Error(%s:%lu): Unsupported register width\n",
					       infile_name, line_num);
					return false;
				}
			}
		}
	}

	// Register source
	else {
		int8_t encoded_dest = encodeRegister(&dest);
		int8_t encoded_src = encodeRegister(&src);
		if (encoded_dest == INVALID_REGISTER) {
			fprintf(stderr, "Assembler Error (%s:%lu): Invalid register name: %s\n", infile_name, line_num, dest);
			return SYNTAX_ERROR;
		}
		else if (encoded_src == INVALID_REGISTER) {
			fprintf(stderr, "Assembler Error (%s:%lu): Invalid register name: %s\n", infile_name, line_num, src);
			return SYNTAX_ERROR;
		}
		else if (encoded_dest > 7 || encoded_src > 7) {
			fprintf(stderr, "Assembler Error (%s:%lu): Extended register set not supported\n", infile_name, line_num);
		}
		int8_t operands = DIRECT | (encoded_src << 3) | encoded_dest;
		int16_t width = getRegisterWidth(&dest);
		curr_block = makeRoom(curr_block, 2);
		if (width == 8) {
			((uint8_t*)curr_block->data)[curr_block->size] = opcode;
		}
		else if (width == 16 || width == 32) {
			((uint8_t*)curr_block->data)[curr_block->size] = opcode | L;
		}
		else {
			fprintf(stderr, "Assembler Error (%s:%lu): Unsupported width: %hi\n", infile_name, line_num, width);
			return FEATURE_NOT_IMPLEMENTED_YET;
		}
		((uint8_t*)curr_block->data)[curr_block->size+1] = operands;
		curr_block->size += 2;
	}
	return true;
}


bool moveConstant(Block *curr_block, size_t value, int16_t width, int8_t encoded_dest) {
	if (width == 8) {
		curr_block = makeRoom(curr_block, 2);
		((uint8_t*)curr_block->data)[curr_block->size] = MOVB_I + encoded_dest;
		*(int8_t*)(curr_block->data+curr_block->size+1) = value;
		curr_block->size += 2;
	}
	else if (width == 16) {
		curr_block = makeRoom(curr_block, 3);
		((uint8_t*)curr_block->data)[curr_block->size] = MOVW_I + encoded_dest;
		*(int16_t*)(curr_block->data+curr_block->size+1) = value;
		curr_block->size += 3;
	}
	else if (width == 32) {
		curr_block = makeRoom(curr_block, 5);
		((uint8_t*)curr_block->data)[curr_block->size] = MOVL_I + encoded_dest;
		*(int32_t*)(curr_block->data+curr_block->size+1) = value;
		curr_block->size += 5;
	}
	else {
		fprintf(stderr, "Assembler Error (%s:%lu): Unsupported width: %hi\n", infile_name, line_num, width);
		return false;
	}
	return true;
}


/*
 * Encodes a move instruction between registers or from register to memory
 * Param curr_block:      The current instruction block being written to
 * Param src:             The name of the src register
 * Param dest:            The name of the destination register
 * Param encoded_dest:    The encoding for the destination register
 * Param addressing_mode: The value for the MOD bits (11 for move, 00 for store)
 * Returns:               true if the move was successfully encoded
 */
bool moveRegister(Block *curr_block, String *src, String *dest, int8_t encoded_dest,
                  int8_t addressing_mode) {

	int8_t encoded_src = encodeRegister(src);
	
	// Invalid instructions
	if (encoded_dest == INVALID_REGISTER) {
		fprintf(stderr, "Assembler Error (%s:%lu): Invalid register name: %s\n",
		        infile_name, line_num, dest->d);
		return false;
	}
	else if (encoded_src == INVALID_REGISTER) {
		fprintf(stderr, "Assembler Error (%s:%lu): Invalid register name: %s\n", infile_name,
		        line_num, src->d);
		return false;
	}
	else if (encoded_src > 7) {
		fprintf(stderr, "Assembler Error (%s:%lu): Extended register set not supported\n",
		        infile_name, line_num);
	}

	// To/From control registers
	if (!strncmp(src->d, "cr", 2)) {
		curr_block = makeRoom(curr_block, 3);
		uint16_t *d = curr_block->data + curr_block->size;
		*d = MOV_R_CR;
		*(uint8_t*)(d+1) = DIRECT | (encoded_src << 3) | encoded_dest;
		curr_block->size += 3;
	}
	else if (!strncmp(dest->d, "cr", 2)) {
		curr_block = makeRoom(curr_block, 3);
		uint16_t *d = curr_block->data + curr_block->size;
		*d = MOV_CR_R;
		*(uint8_t*)(d+1) = DIRECT | (encoded_dest << 3) | encoded_src;
		curr_block->size += 3;
	}

	// Regular registers
	else {
		curr_block = makeRoom(curr_block, 2);
		int16_t width = getRegisterWidth(dest);
		if (width == 8) {
			((uint8_t*)curr_block->data)[curr_block->size] = MOVB;
		}
		else if (width == 16 || width == 32) {
			((uint8_t*)curr_block->data)[curr_block->size] = MOVL;
		}
		else {
			fprintf(stderr, "Assembler Error (%s:%lu): Unsupported width: %hi\n", infile_name,
			        line_num, width);
			return false;
		}
		if (addressing_mode == INDIRECT) {
			int8_t tmp = encoded_src;
			encoded_src = encoded_dest;
			encoded_dest = tmp;
		}
		int8_t mod_reg_rm = addressing_mode | (encoded_dest << 3) | encoded_src;
		((uint8_t*)curr_block->data)[curr_block->size+1] = mod_reg_rm;
		curr_block->size += 2;
	}
	return true;
}


/*
 * Encodes a jump or jump conditional instruction
 * Param curr_block: The current instruction block being written to
 * Param dest:       The destination for the jump
 * Param opcode:     The instruction opcode
 * Returns:          The new block to write to
*/
Block* encodeJump(Block *curr_block, char *dest, uint8_t opcode) {
	if (curr_block->capacity) {
		Block *new_block = malloc(sizeof(Block));
		new_block->next = NULL;
		new_block->address = curr_block->address + curr_block->size;
		curr_block->next = new_block;
		curr_block = new_block;
	}

	curr_block->size = 2;
	curr_block->opcode = opcode;
	curr_block->line_num = line_num;
	curr_block->long_mode = long_mode;
	
	String target;
	getIdentifier(dest, &target);
	curr_block->capacity = target.len;
	curr_block->data = malloc(target.len);
	memcpy(curr_block->data, target.d, target.len);
	return curr_block;
}


int main(int argc, char **argv) {
	char *outfile_name = NULL;
	bool o_flag = false;
	for (size_t i = 1; i < argc; i++) {
		if (o_flag) {
			outfile_name = argv[i];
			o_flag = false;
		}
		else if (argv[i][0] == '-') {
			for (char *j = argv[i] + 1; *j; j++) {
				if (*j == 'o') {
					o_flag = true;
				}
			}
		}
		else {
			infile_name = argv[i];
		}
	}
	if (!infile_name) {
		fprintf(stderr, "Assembler Error: No input file\n");
		return USAGE_ERROR;
	}
	FILE *infile = fopen(infile_name, "r");
	if (!infile) {
		fprintf(stderr, "Assembler Error (%s:1): cannot open file for reading\n", infile_name);
		return IO_ERROR;
	}

	Block *text_segment = calloc(1, sizeof(Block));
	Block *curr_block = text_segment;

	LabelMap labels;
	LabelMapInit(&labels);
	ConstantMapInit(&constants);

	char *buffer = NULL;
	size_t buffer_size = 0;

	ssize_t n;
	while ((n = getline(&buffer, &buffer_size, infile)) >= 1) {

		line_num++;
		String opcode;
		size_t offset = getIdentifier(buffer, &opcode);
		char *operands = opcode.d + opcode.len;

		// dw
		if (EQUALS(opcode,"dw",2)) {
			curr_block = makeRoom(curr_block, 2);
			String value;
			getIdentifier(operands, &value);
			Constant *c = ConstantMapGet(&constants, &value);
			uint16_t *target = curr_block->data + curr_block->size;
			if (c) {
				*target = c->val;
			}
			else if (sscanf(operands, " %hi", target) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dw\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			curr_block->size += 2;
		}

		// dd
		else if (EQUALS(opcode,"dd",2)) {
			curr_block = makeRoom(curr_block, 4);
			String value;
			getIdentifier(operands, &value);
			Constant *c = ConstantMapGet(&constants, &value);
			uint32_t *target = curr_block->data + curr_block->size;
			if (c) {
				*target = c->val;
			}
			else if (sscanf(operands, " %i", target) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dd\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			curr_block->size += 4;
		}

		// jmp
		else if (EQUALS(opcode,"jmp",3)) {
			curr_block = encodeJump(curr_block, operands, SHORT_JMP);
		}

		// jnz
		else if (EQUALS(opcode,"jnz",3)) {
			curr_block = encodeJump(curr_block, operands, SHORT_JNZ);
		}

		// mov
		else if (EQUALS(opcode,"mov",3)) {
			String src, dest;
			getIdentifier(operands, &dest);
			int16_t width;
			bool storeImmediate = EQUALS(dest,"DWORD",5);
			bool store = storeImmediate || *(dest.d) == '[';
			int8_t encoded_dest;
			char *s = dest.d + dest.len;
			if (store) {
				width = 32;
				while (isspace(*s)) s++;
				if (*s != '[') {
					fprintf(stderr, "Assembler Error (%s:%lu): Invalid Address Format\n",
					        infile_name, line_num);
					return SYNTAX_ERROR;
				}
				s++;
				s += getIdentifier(s, &dest);
				s += dest.len;
				encoded_dest = encodeRegister(&dest);
				while (isspace(*s)) s++;
				if (*s != ']') {
					fprintf(stderr, "Assembler Error (%s,%lu): Invalid Address Format\n", infile_name, line_num);
					return SYNTAX_ERROR;
				}
				s++;
			}
			else {
				width = getRegisterWidth(&dest);
				encoded_dest = encodeRegister(&dest);
			}
			if (*s != ',') {
				fprintf(stderr, "Assembler Error (%s:%lu): Expected comma\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			getIdentifier(s+1, &src);
			Constant *v = ConstantMapGet(&constants, &src);
			if (encoded_dest > 7) {
				fprintf(stderr, "Assembler Error (%s:%lu): Extended register set not supported\n",
				        infile_name, line_num);
				return FEATURE_NOT_IMPLEMENTED_YET;
			}
			else if (store) {

				// Store from constant
				if (v) {
					switch (width) {
						case 8: {
							curr_block = makeRoom(curr_block, 3);
							uint16_t inst = ((int16_t)encoded_dest << 8) | STB_I;
							*(uint16_t*)(curr_block->data+curr_block->size) = inst;
							*(int8_t*)(curr_block->data+curr_block->size+2) = (int8_t)(v->val);
							curr_block->size += 3;
							break;
						}
						case 16: {
							curr_block = makeRoom(curr_block, 4);
							uint16_t inst = ((int16_t)encoded_dest << 8) | STW_I;
							*(uint16_t*)(curr_block->data+curr_block->size) = inst;
							*(int16_t*)(curr_block->data+curr_block->size+2) = (int16_t)(v->val);
							curr_block->size += 4;
							break;
						}
						case 32: {
							curr_block = makeRoom(curr_block, 6);
							uint16_t inst = ((int16_t)encoded_dest << 8) | STL_I;
							*(uint16_t*)(curr_block->data+curr_block->size) = inst;
							*(int32_t*)(curr_block->data+curr_block->size+2) = (int32_t)(v->val);
							curr_block->size += 6;
							break;
						}
						default: {
							fprintf(stderr, "Assembler Error (%s:%lu): Unsupported width: %hi\n",
									infile_name, line_num, width);
							return false;
						}
					}
				}
				else if (storeImmediate){
					fprintf(stderr,
					        "Assembler Error (%s:%lu): Storing from immediate not supported\n",
							infile_name, line_num);
					return FEATURE_NOT_IMPLEMENTED_YET;
				}

				// Store from register
				else {
					if (!moveRegister(curr_block, &src, &dest, encoded_dest, INDIRECT)) {
						return SYNTAX_ERROR;
					}
				}
			}

			// Move from immediate (constant)
			else if (v) {
				if (!moveConstant(curr_block, v->val, width, encoded_dest)) {
					return FEATURE_NOT_IMPLEMENTED_YET;
				}
			}

			// Move from immediate (literal)
			else if (isdigit(src.d[0])) {
				if (width == 8) {
					curr_block = makeRoom(curr_block, 2);
					((uint8_t*)curr_block->data)[curr_block->size] = MOVB_I + encoded_dest;
					sscanf(src.d, "%hhi", (int8_t*)(curr_block->data+curr_block->size+1));
					curr_block->size += 2;
				}
				else if (width == 16) {
					curr_block = makeRoom(curr_block, 3);
					((uint8_t*)curr_block->data)[curr_block->size] = MOVW_I + encoded_dest;
					sscanf(src.d, "%hi", (int16_t*)(curr_block->data+curr_block->size+1));
					curr_block->size += 3;
				}
				else if (width == 32) {
					curr_block = makeRoom(curr_block, 5);
					((uint8_t*)curr_block->data)[curr_block->size] = MOVL_I + encoded_dest;
					sscanf(src.d, "%i", (int32_t*)(curr_block->data+curr_block->size+1));
					curr_block->size += 5;
				}
				else {
					fprintf(stderr, "Assembler Error (%s:%lu): Unsupported width: %hi\n", infile_name, line_num, width);
					return FEATURE_NOT_IMPLEMENTED_YET;
				}
			}

			// Move from register
			else {
				if (!moveRegister(curr_block, &src, &dest, encoded_dest, DIRECT)) {
					return SYNTAX_ERROR;
				}
			}
		}

		else if (EQUALS(opcode,"and",3)) {
			if (!encodeInstruction(curr_block, operands, AND)) {
				return ERROR;
			}
		}

		else if (EQUALS(opcode,"add",3)) {
			if (!encodeInstruction(curr_block, operands, ADD)) {
				return ERROR;
			}
		}

		else if (EQUALS(opcode,"xor",3)) {
			if (!encodeInstruction(curr_block, operands, XOR)) {
				return ERROR;
			}
		}

		else if (EQUALS(opcode,"dec",3)) {
			curr_block = makeRoom(curr_block, 1);
			String dest;
			getIdentifier(operands, &dest);
			uint8_t encoded_dest = encodeRegister(&dest);
			((uint8_t*)(curr_block->data))[curr_block->size] = DEC | encoded_dest;
			curr_block->size++;
		}

		else if (EQUALS(opcode,"rep",3)) {
			String command;
			getIdentifier(operands, &command);
			curr_block = makeRoom(curr_block, 2);
			if (EQUALS(command,"stosb",5)) {
				*(int16_t*)(curr_block->data+curr_block->size) = REP_STOSB;
			}
			else if (EQUALS(command,"stosw",5) || EQUALS(command,"stosd",5)) {
				*(int16_t*)(curr_block->data+curr_block->size) = REP_STOSD;
			}
			else {
				fprintf(stderr, "Assembler Error (%s:%lu): Unsuported instruction: %s\n", infile_name, line_num, opcode);
				return FEATURE_NOT_IMPLEMENTED_YET;
			}
			curr_block->size += 2;
		}

		// Not recognized instruction, check if it's a label or constant
		else if (opcode.len) {
			size_t remaining = n - offset - opcode.len;
			if (remaining && opcode.d[opcode.len] == ':') {
				Label new_label;
				new_label.name = malloc(opcode.len);
				memcpy(new_label.name, opcode.d, opcode.len);
				new_label.name_len = opcode.len;
				Block *new_block = malloc(sizeof(Block));
				new_block->next = NULL;
				new_block->data = NULL;
				new_block->capacity = 0;
				new_block->size = 0;
				new_block->address = curr_block->address + curr_block->size;
				new_block->line_num = line_num;
				new_block->opcode = BLOCK;
				curr_block->next = new_block;
				curr_block = new_block;
				new_label.target = curr_block;

				LabelMapInsert(&labels, &new_label);
			}
			else {
				String equ;
				getIdentifier(operands, &equ);
				if (EQUALS(equ,"equ",3)) {
					Constant new_const;
					new_const.name = malloc(opcode.len);
					memcpy(new_const.name, opcode.d, opcode.len);
					new_const.name_len = opcode.len;
					sscanf(equ.d+equ.len, " %li", &(new_const.val));
					ConstantMapInsert(&constants, &new_const);
				}
				else {
					fprintf(stderr, "Assembler Error (%s:%lu): unknown instruction \"", infile_name, line_num);
					fwrite((void*)opcode.d, sizeof(char), opcode.len, stderr);
					fputs("\"\n", stderr);
					return SYNTAX_ERROR;
				}
			}
		}

		// Check if it's an assembly directive
		else if (n > offset && *operands == '[') {
			operands++;
			getIdentifier(operands, &opcode);
			if (EQUALS(opcode, "bits", 4)) {
				operands = opcode.d + opcode.len;
				uint8_t mode;
				if (sscanf(operands, " %hhi", &mode) != 1) {
					fprintf(stderr, "Assembler Error (%s:%lu): Directive \"BITS\" requires an argument\n", infile_name, line_num);
					return SYNTAX_ERROR;
				}
				if (mode == 32) {
					long_mode = false;
				}
				else if (mode == 64) {
					long_mode = true;
				}
				else {
					fprintf(stderr, "Assembler Error (%s:%lu): %hhi bit mode is not supported\n", infile_name, line_num, mode);
					return SEMANTIC_ERROR;
				}
			}
		}
	}
	
	free(buffer);
	fclose(infile);

	Block *end = curr_block;
	size_t offset = 0;
	for (curr_block = text_segment; curr_block; curr_block = curr_block->next) {
		curr_block->address += offset;
		int16_t op = curr_block->opcode;
		if (op == SHORT_JMP || op == SHORT_JNZ) {
			Label *lab = LabelMapGet(&labels, (String*)&(curr_block->data));
			if (lab == NULL) {
				fprintf(stderr, "Assembler Error(%s:%lu): unknown label \"", infile_name, curr_block->line_num);
				fwrite(curr_block->data, sizeof(char), curr_block->capacity, stderr);
				fputs("\"\n", stderr);
				return SEMANTIC_ERROR;
			}
			else {
				free(curr_block->data);
				curr_block->data = lab->target;
				offset += setJmpOperand(curr_block);
			}
		}
	}

	while (offset) {
		offset = 0;
		for (curr_block = text_segment; curr_block; curr_block = curr_block->next) {
			curr_block->address += offset;
			switch (curr_block->opcode) {
				case (SHORT_JMP) :
				case (NEAR_JMP) : 
				case (SHORT_JNZ) :
				case (NEAR_JNZ) : {
					offset += setJmpOperand(curr_block);
				}
				default : {}
			}
		}
	}

	// Write ELF Header
	ElfHeader header;
	header.ident_mag            = ELF_MAGIC_NUMBER;
	header.ident_class          = SIXTY_FOUR_BIT;
	header.ident_data           = LITTLE;
	header.ident_version        = ORIGINAL_ELF;
	header.ident_os_abi         = SYSTEM_V;
	header.ident_abi_version    = 0;
	header.type                 = ET_EXEC;
	header.machine              = X86_64;
	header.version              = ORIGINAL_ELF;
	header.entry                = 0;
	header.phoff                = ELF_HEADER_SIZE;
	header.shoff                = 0;
	header.flags                = 0;
	header.ehsize               = ELF_HEADER_SIZE;
	header.phentsize            = PH_ENTRY_SIZE;
	header.phnum                = 1;
	header.shentsize            = 0;
	header.shnum                = 0;
	header.shstrndx             = 0;

	String start_str;
	start_str.d = "_start";
	start_str.len = 6;
	Label *start_label = LabelMapGet(&labels, &start_str);
	if (start_label) {
		header.entry = start_label->target->address;
	}

	LabelMapFree(&labels);

	size_t segment_size = end->address + end->size;

	// Write Program Header for text segment
	ElfProgramHeader text_header;
	text_header.type    = LOAD;
	text_header.flags   = R_X;
	text_header.offset  = 0x78;
	text_header.vaddr   = 0;
	text_header.paddr   = 0;
	text_header.filesz  = segment_size;
	text_header.memsz   = segment_size;
	text_header.align   = 8;

	FILE *outfile = fopen(outfile_name ? outfile_name : "out.elf", "w");
	if (!outfile) {
		fprintf(stderr, "Assembler Error (%s:1): cannot open file for writing\n", outfile_name);
		return IO_ERROR;
	}
	
	fwrite((void*) &header, ELF_HEADER_SIZE, 1, outfile);
	fwrite((void*) &text_header, PH_ENTRY_SIZE, 1, outfile);
	
	for (curr_block = text_segment; curr_block; curr_block = curr_block->next) {
		int op_size = sizeof(int8_t);
		switch (curr_block->opcode) {
			case (BLOCK) : {
				fwrite((void*) curr_block->data, 1, curr_block->size, outfile);
				break;
			}
			case (SHORT_JMP) :
			case (SHORT_JNZ) : {
				int16_t to_write = curr_block->opcode | ((curr_block->operand & 0xFF) << 8);
				fwrite((void*) &to_write, sizeof(int16_t), 1, outfile);
				break;
			}
			case (NEAR_JNZ) : {
				op_size = sizeof(int16_t);
			}
			case (NEAR_JMP) : {
				fwrite((void*) &(curr_block->opcode), op_size, 1, outfile);
				if (curr_block->long_mode) {
					fwrite((void*) &(curr_block->operand), sizeof(int32_t), 1, outfile);
				}
				else {
					fwrite((void*) &(curr_block->operand), sizeof(int16_t), 1, outfile);
				}
				break;
			}
		}
	}
	fclose(outfile);

	return SUCCESS;
}
