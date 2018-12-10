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
#define THIRTY_TWO_BIT 1
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

#define TEXT_SIZE_GUESS 0x1000
#define LABEL_MAP_START_SIZE 0x40
#define LABEL_MAP_START_MASK 0x3F
#define LABEL_MAX_LOAD_FACTOR 0.9
#define LABEL_RESIZE_SHIFT 2
#define LABEL_RESIZE_MASK 3

#define START_NUM_LABEL_REFERENCE 0x80

#define JMP_REL8 0xEB

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

typedef struct String {
	char   *d;
	size_t  len;
} String;

typedef struct Label {
	char   *d;
	size_t  len;
	size_t  index;
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

typedef struct LabelReference {
	String  label;
	size_t  pc;
	size_t  index;
	size_t  line_num;
	uint8_t size;
} LabelReference;

typedef struct LabelReferenceContainer {
	LabelReference *data;
	size_t          capacity;
	size_t          size;
} LabelReferenceContainer;

void insertReference(LabelReferenceContainer *cont, LabelReference *ref) {
	if (cont->size == cont->capacity) {
		cont->capacity <<= 1;
		cont->data = realloc(cont->data, cont->capacity);
	}
	cont->data[cont->size] = *ref;
	cont->size++;
}

size_t hashString(char *d, size_t len) {
	size_t hash = 5381;
	for (size_t i = 0; i < len; i++) {
		hash = ((hash << 5) + hash) + d[i];
	}
	return hash;
}

Label* get(LabelMap *map, String *l) {
	size_t mask = map->mask;
	size_t pos = hashString(l->d, l->len) & mask;
	MapEntry *d = map->data;
	size_t max_dist = map->max_probe_length;
	for (size_t distance = 0; d[pos].l.d && distance <= max_dist; distance++) {
		if (d[pos].l.len == l->len || !strncmp(d[pos].l.d, l->d, l->len)) {
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
		if (d[pos].l.d == NULL) {
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
			if (old_data[i].l.d) {
				basicInsert(map, &(old_data[i].l), old_data[i].hash);
			}
		}
		free(old_data);
	}
	size_t hash = hashString(entry->d, entry->len);
	basicInsert(map, entry, hash);
}

// This function is unsafe if buffer is not null terminated
size_t getIdentifier(char *buffer, String *id) {
	size_t ret = 0;
	for (id->d = buffer; isspace(*id->d); id->d++) ret++;
	for (id->len = 0; id->d[id->len] && (isalnum(id->d[id->len]) || id->d[id->len] == '_'); id->len++);
	return ret;
}

size_t makeBigEnough(uint8_t **buf, size_t size, size_t cap) {
	if (size > cap) {
		cap <<= 1;
		*buf = (uint8_t*)realloc((void*)(*buf), cap);
	}
	return cap;
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

	size_t text_capacity = TEXT_SIZE_GUESS;
	uint8_t *text_segment = (uint8_t*) malloc(text_capacity);
	size_t text_size = 0;

	LabelReferenceContainer refs;
	refs.capacity = START_NUM_LABEL_REFERENCE;
	refs.data = malloc(refs.capacity * sizeof(LabelReference));
	refs.size = 0;

	LabelMap labels;
	labels.data = calloc(LABEL_MAP_START_SIZE, sizeof(MapEntry));
	labels.mask = LABEL_MAP_START_MASK;
	labels.num_entries = 0;
	labels.max_probe_length = 0;

	char *buffer = NULL;
	size_t buffer_size = 0;
	size_t line_num = 0;

	for (ssize_t n = getline(&buffer, &buffer_size, infile); n >= 1; n = getline(&buffer, &buffer_size, infile)) {

		line_num++;
		String opcode;
		size_t offset = getIdentifier(buffer, &opcode);
		char *operands = opcode.d + opcode.len;

		// dw
		if (opcode.len == 2 && !strncmp(opcode.d, "dw", 2)) {
			text_capacity = makeBigEnough(&text_segment, text_size + 2, text_capacity);
			if (sscanf(operands, " %hi", (uint16_t*)(text_segment + text_size)) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dw\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			text_size += 2;
		}

		// dd
		else if (opcode.len == 2 && !strncmp(opcode.d, "dd", 2)) {
			text_capacity = makeBigEnough(&text_segment, text_size + 4, text_capacity);
			if (sscanf(operands, " %i", (uint32_t*)(text_segment + text_size)) != 1) {
				fprintf(stderr, "Assembler Error (%s:%lu): Directive \"dd\" requires an argument\n", infile_name, line_num);
				return SYNTAX_ERROR;
			}
			text_size += 4;
		}

		// jmp
		else if (opcode.len == 3 && !strncmp(opcode.d, "jmp", 3)) {
			text_capacity = makeBigEnough(&text_segment, text_size + 2, text_capacity);
			LabelReference ref;
			size_t tgt_offset = getIdentifier(operands, &ref.label);
			ref.label.d = malloc(ref.label.len * sizeof(char));
			memcpy(ref.label.d, operands+tgt_offset, ref.label.len);
			ref.pc = text_size + 2;
			ref.index = text_size + 1;
			ref.line_num = line_num;
			ref.size = 1;
			insertReference(&refs, &ref);
			text_segment[text_size] = JMP_REL8;
			text_size += 2;
		}

		// Not recognized instruction, check if it's a label
		else if (opcode.len) {
			size_t remaining = n - offset - opcode.len;
			if (remaining && opcode.d[opcode.len] == ':') {
				Label new_label;
				new_label.d = malloc(opcode.len);
				memcpy(new_label.d, opcode.d, opcode.len);
				new_label.len = opcode.len;
				new_label.index = text_size;
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

	for (LabelReference *i = refs.data; i < refs.data + refs.size; i++) {
		Label *label = get(&labels, &(i->label));
		if (label == NULL) {
			fprintf(stderr, "Assembler Error(%s:%lu): unknown label \"", infile_name, i->line_num);
			fwrite(i->label.d, sizeof(char), i->label.len, stderr);
			fputs("\"\n", stderr);
			return SEMANTIC_ERROR;
		}
		int32_t offset = label->index - i->pc;
		int8_t small_offset = (int8_t)offset;
		if (offset != (int32_t)small_offset) {
			fprintf(stderr, "Assembler Error(%s:%lu): jump distances greater than 127 aren't supported yet\n", infile_name, i->line_num);
			return FEATURE_NOT_IMPLEMENTED_YET;
		}
		text_segment[i->index] = small_offset;
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
		header.entry = start_label->index;
	}

	// Write Program Header for text segment
	ElfProgramHeader text_header;
	text_header.type    = LOAD;
	text_header.flags   = R_X;
	text_header.offset  = 0x78;
	text_header.vaddr   = 0;
	text_header.paddr   = 0;
	text_header.filesz  = text_size;
	text_header.memsz   = text_size;
	text_header.align   = 8;

	FILE *outfile = fopen(outfile_name ? outfile_name : "out.elf", "w");
	if (!outfile) {
		fprintf(stderr, "Assembler Error (%s:1): cannot open file for writing\n", outfile_name);
		return IO_ERROR;
	}
	fwrite((void*) &header, ELF_HEADER_SIZE, 1, outfile);
	fwrite((void*) &text_header, PH_ENTRY_SIZE, 1, outfile);
	fwrite((void*) text_segment, 1, text_size, outfile);
	fclose(outfile);

	return SUCCESS;
}
