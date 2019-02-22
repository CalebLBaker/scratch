#include <stdlib.h>

#ifndef DTYPE
#define DTYPE int
#endif

#ifndef QTYPE
#define QTYPE int
#endif

#ifndef D_HASH
#define D_HASH(x) (*(x))
#endif

#ifndef Q_HASH
#define Q_HASH(x) (*(x))
#endif

#ifndef DELETE
#define DELETE {}
#endif

#ifndef VALID
#define VALID(d) (d)
#endif

#ifndef EQ
#define EQ(d,q) (*(q) == (d))
#endif

#ifndef MAP_START_SIZE
#define MAP_START_SIZE 0x40
#define MAP_START_MASK 0x3F
#endif

#ifndef MAP_START_MASK
#define MAP_START_MASK MAP_START_SIZE - 1
#endif

#ifndef MAX_LOAD_FACTOR
#define MAX_LOAD_FACTOR 0.9
#endif

#ifndef RESIZE_SHIFT
#define RESIZE_SHIFT 2
#endif

#ifndef RESIZE_MASK
#define RESIZE_MASK 3
#endif

#define PASTER(x,y) x ## y
#define E(x,y) PASTER(x,y)

#define MAP E(DTYPE,Map)
#define MAP_ENTRY E(DTYPE,MapEntry)
#define INIT E(DTYPE,MapInit)
#define FREE_MAP E(DTYPE,MapFree)
#define GET E(DTYPE,MapGet)
#define BASIC_INSERT E(DTYPE,BasicInsert)
#define INSERT E(DTYPE,MapInsert)

typedef struct MAP_ENTRY {
	DTYPE   l;
	size_t  hash;
} MAP_ENTRY;

typedef struct MAP {
	MAP_ENTRY   *data;
	size_t      mask;
	size_t      num_entries;
	size_t      max_probe_length;
} MAP;

void INIT(MAP *m) {
	m->data = calloc(MAP_START_SIZE, sizeof(MAP_ENTRY));
	m->mask = MAP_START_MASK;
	m->num_entries = 0;
	m->max_probe_length = 0;
}

void FREE_MAP(MAP *m) {
	for (size_t i = 0; i <= m->mask; i++) {
		DELETE(m->data[i].l);
	}
	free(m->data);
}

DTYPE* GET(MAP *map, QTYPE *l) {
	size_t mask = map->mask;
	size_t pos = Q_HASH(l) & mask;
	MAP_ENTRY *d = map->data;
	size_t max_dist = map->max_probe_length;
	for (size_t distance = 0; VALID(d[pos].l) && distance <= max_dist; distance++) {
		if (EQ((d[pos].l),(l))) {
			return &(d[pos].l);
		}
		pos = (pos+1) & mask;
	}
	return NULL;
}

void BASIC_INSERT(MAP *map, DTYPE *entry, size_t hash) {
	size_t mask = map->mask;
	MAP_ENTRY *d = map->data;
	size_t pos = hash & mask;
	for (size_t distance = 0;; distance++) {
		if (!VALID(d[pos].l)) {
			d[pos].l = *entry;
			d[pos].hash = hash;
			if (distance > map->max_probe_length) {
				map->max_probe_length = distance;
			}
			return;
		}
		size_t prev_dist = (pos - d[pos].hash) & mask;
		if (prev_dist < distance) {
			DTYPE tmp = *entry;
			*entry = d[pos].l;
			d[pos].l = tmp;
			size_t tmp_hash = hash;
			hash = d[pos].hash;
			d[pos].hash = tmp_hash;
			distance = prev_dist;
		}
		pos = (pos+1) & mask;
	}
}

void INSERT(MAP *map, DTYPE *entry) {
	map->num_entries++;
	size_t mask = map->mask;
	if ((double)map->num_entries / mask > MAX_LOAD_FACTOR) {
		size_t old_mask = map->mask;
		map->mask = (old_mask << RESIZE_SHIFT) | RESIZE_MASK;
		MAP_ENTRY *old_data = map->data;
		map->data = calloc(map->mask+1, sizeof(MAP_ENTRY));
		map->max_probe_length = 0;
		for (size_t i = 0; i <= old_mask; i++) {
			if (old_data[i].l.name) {
				BASIC_INSERT(map, &(old_data[i].l), old_data[i].hash);
			}
		}
		free(old_data);
	}
	size_t hash = D_HASH(entry);
	BASIC_INSERT(map, entry, hash);
}
