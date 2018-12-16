#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#define SUCCESS 0
#define USAGE_ERROR -1
#define IO_ERROR -2
#define SYNTAX_ERROR -3
#define SEMANTIC_ERROR -4
#define FEATURE_NOT_IMPLEMENTED_YET -5

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

#define EQUALS(left,right,size) ((left).len == (size) && !strncmp((left).d, (right), (size)))
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

// Can represent either a block of multiple instructions
// or a single control flow instruction
typedef struct Block {
	void   *next;		// Pointer to next block
	void   *data;		// For multi-instruction blocks, the encoded instructions; For single instructions, the target label
	size_t  capacity;	// Capacity of buffer pointed to by data
	size_t  size;		// Size of the block
	size_t  address;	// Starting address of the block
	size_t  line_num;	// Line number for first instruction of the block
	int32_t operand;	// Single instruction only: encoded operand
	uint8_t opcode;		// Opcode of the instruction; BLOCK for a multi-instruction block
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

typedef struct MapEntry {
	Label   l;
	size_t  hash;
} MapEntry;

typedef struct LabelMap {
	MapEntry   *data;
	size_t      mask;
	size_t      num_entries;
	size_t      max_probe_length;
} LabelMap;

size_t hashString(char *d, size_t len) {
	size_t hash = 5381;
	for (size_t i = 0; i < len; i++) {
		hash = ((hash << 5) + hash) + d[i];
	}
	return hash;
}

void init(LabelMap *m) {
	m->data = calloc(LABEL_MAP_START_SIZE, sizeof(MapEntry));
	m->mask = LABEL_MAP_START_MASK;
	m->num_entries = 0;
	m->max_probe_length = 0;
}

void freeData(LabelMap *m) {
	for (size_t i = 0; i <= m->mask; i++) {
		if (m->data[i].l.name) {
			free(m->data[i].l.name);
		}
	}
	free(m->data);
}

Label* get(LabelMap *map, String *l) {
	size_t mask = map->mask;
	size_t pos = hashString(l->d, l->len) & mask;
	MapEntry *d = map->data;
	size_t max_dist = map->max_probe_length;
	for (size_t distance = 0; d[pos].l.name && distance <= max_dist; distance++) {
		if (EQUALS(*l, d[pos].l.name, l->len)) {
			return &(d[pos].l);
		}
		pos = (pos+1) & mask;
	}
	return NULL;
}

void basicInsert(LabelMap *map, Label *entry, size_t hash) {
	size_t mask = map->mask;
	MapEntry *d = map->data;
	size_t pos = hash & mask;
	for (size_t distance = 0;; distance++) {
		if (d[pos].l.name == NULL) {
			d[pos].l = *entry;
			d[pos].hash = hash;
			if (distance > map->max_probe_length) {
				map->max_probe_length = distance;
			}
			return;
		}
		size_t prev_dist = (pos - d[pos].hash) & mask;
		if (prev_dist < distance) {
			Label tmp_l = *entry;
			*entry = d[pos].l;
			d[pos].l = tmp_l;
			size_t tmp_hash = hash;
			hash = d[pos].hash;
			d[pos].hash = tmp_hash;
			distance = prev_dist;
		}
		pos = (pos+1) & mask;
	}
}

void insert(LabelMap *map, Label *entry) {
	map->num_entries++;
	size_t mask = map->mask;
	if ((double)map->num_entries / mask > LABEL_MAX_LOAD_FACTOR) {
		size_t old_mask = map->mask;
		map->mask = (old_mask << LABEL_RESIZE_SHIFT) | LABEL_RESIZE_MASK;
		MapEntry *old_data = map->data;
		map->data = calloc(map->mask+1, sizeof(MapEntry));
		map->max_probe_length = 0;
		for (size_t i = 0; i <= old_mask; i++) {
			if (old_data[i].l.name) {
				basicInsert(map, &(old_data[i].l), old_data[i].hash);
			}
		}
		free(old_data);
	}
	size_t hash = hashString(entry->name, entry->name_len);
	basicInsert(map, entry, hash);
}

// This function is unsafe if buffer is not null terminated
size_t getIdentifier(char *buffer, String *id) {
	size_t ret = 0;
	for (id->d = buffer; isspace(*id->d); id->d++) ret++;
	for (id->len = 0; id->d[id->len] && (isalnum(id->d[id->len]) || id->d[id->len] == '_'); id->len++);
	return ret;
}

Block* makeRoom(Block *curr_block, size_t line_num, size_t size) {
	if (curr_block->opcode == BLOCK) {
		size_t new_capacity = MAX(curr_block->capacity << 1, BLOCK_START_SIZE);
		curr_block->data = realloc(curr_block->data, new_capacity);
		curr_block->capacity = new_capacity;
	}
	else if (curr_block->capacity < curr_block->size + size) {
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
	return curr_block;
}

size_t setJmpOperand(Block *curr_block) {
	curr_block->operand = ((Block*)(curr_block->data))->address - curr_block->address - curr_block->size;
	if (curr_block->operand > INT8_MAX) {
		size_t ret = curr_block->opcode == SHORT_JMP ? 1 : 0;
		curr_block->opcode = NEAR_JMP;
		curr_block->size = 2;
		return ret;
	}
	return 0;
}

int main(int argc, char **argv) {
	char *infile_name = NULL;
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
	init(&labels);

	char *buffer = NULL;
	size_t buffer_size = 0;
	size_t line_num = 0;

	for (ssize_t n = getline(&buffer, &buffer_size, infile); n >= 1; n = getline(&buffer, &buffer_size, infile)) {

		line_num++;
		String opcode;
		size_t offset = getIdentifier(buffer, &opcode);
		char *operands = opcode.d + opcode.len;

		// dw
		if (EQUALS(opcode,"dw",2)) {
			curr_block = makeRoom(curr_block, line_num, 2);
			if (sscanf(operands, " %hi", (uint16_t*)(curr_block->data + curr_block->size)) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dw\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			curr_block->size += 2;
		}

		// dd
		else if (EQUALS(opcode,"dd",2)) {
			curr_block = makeRoom(curr_block, line_num, 4);
			if (sscanf(operands, " %i", (uint32_t*)(curr_block->data + curr_block->size)) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dd\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			curr_block->size += 4;
		}

		// jmp
		else if (EQUALS(opcode,"jmp",3)) {

			if (curr_block->capacity) {
				Block *new_block = malloc(sizeof(Block));
				new_block->next = NULL;
				new_block->address = curr_block->address + curr_block->size;
				curr_block->next = new_block;
				curr_block = new_block;
			}

			curr_block->size = 2;
			curr_block->opcode = SHORT_JMP;
			curr_block->line_num = line_num;
			
			String target;
			getIdentifier(operands, &target);
			curr_block->capacity = target.len;
			curr_block->data = malloc(target.len);
			memcpy(curr_block->data, target.d, target.len);
		}

		// Not recognized instruction, check if it's a label
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

				insert(&labels, &new_label);
			}
			else {
				fprintf(stderr, "Assembler Error (%s:%lu): unknown instruction \"", infile_name, line_num);
				fwrite((void*)opcode.d, sizeof(char), opcode.len, stderr);
				fputs("\"\n", stderr);
				return SYNTAX_ERROR;
			}
		}
	}
	
	free(buffer);
	fclose(infile);

	Block *end = curr_block;
	size_t offset = 0;
	for (curr_block = text_segment; curr_block; curr_block = curr_block->next) {
		curr_block->address += offset;
		if (curr_block->opcode == SHORT_JMP) {
			Label *lab = get(&labels, (String*)&(curr_block->data));
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
				case (NEAR_JMP) : {
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
	Label *start_label = get(&labels, &start_str);
	if (start_label) {
		header.entry = start_label->target->address;
	}

	freeData(&labels);

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
		switch (curr_block->opcode) {
			case (BLOCK) : {
				fwrite((void*) curr_block->data, 1, curr_block->size, outfile);
				break;
			}
			case (SHORT_JMP) : {
				int16_t to_write = SHORT_JMP | ((curr_block->operand & 0xFF) << 8);
				fwrite((void*) &to_write, sizeof(int16_t), 1, outfile);
				break;
			}
			case (NEAR_JMP) : {
				fwrite((void*) &(curr_block->opcode), sizeof(uint8_t), 1, outfile);
				fwrite((void*) &(curr_block->operand), sizeof(int16_t), 1, outfile);
				break;
			}
		}
	}
	fclose(outfile);

	return SUCCESS;
}
