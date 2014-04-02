/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include "interface.hh"

#define TABLE_SIZE 60
#define NUM_DELTAS 20

typedef int16_t deltaType;


class wrappingArray
{
	deltaType data[NUM_DELTAS];
public:
	deltaType& operator[](int index) {
		if (index >= 0 && index < NUM_DELTAS) {
			return data[index];	
		} else if (index < 0) {
			return data[NUM_DELTAS + (index % NUM_DELTAS)];
		} else {
			return data[index % NUM_DELTAS];
		}
	}
}; 

struct delta_entry
{
	Addr PC;
	Addr last_adress;
	Addr last_prefetch;	
	wrappingArray deltas;
	//deltaType deltas[NUM_DELTAS];
	int delta_index;

	void init_delta_entry(Addr PC, Addr last_adress) {
		this->PC = PC;
		this->last_adress = last_adress;
		last_prefetch = 0;
		for (int i = 0; i < NUM_DELTAS; i++) {
			deltas[i] = 0;
		}
		delta_index = 0;
	}

	void insert_delta_into_entry(Addr curr_addr) {	
		deltas[delta_index++] = curr_addr - last_adress;
		last_adress = curr_addr;
		if (delta_index == NUM_DELTAS) {
			delta_index = 0;
		}
	}

};

int lru_index = 0;

delta_entry entries[TABLE_SIZE];

delta_entry *locate_entry_for_PC(Addr PC)
{
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (entries[i].PC == PC) {
			return entries + i;
		}
	}
	if (lru_index == TABLE_SIZE) {
		lru_index = 0;
	}
	return entries + lru_index++;
}

void prefetch_init(void)
{
	for (int i = 0; i < TABLE_SIZE; i++) {
		entries[i].init_delta_entry(0, 0);
	}
    /* Called before any calls to prefetch_access. */
    /* This is the place to initialize data structures. */

    DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

void delta_correlation(delta_entry *entry, Addr *candidates)
{
	for (int i = 0; i < NUM_DELTAS; i++) {
		candidates[i] = 0;
	}

	deltaType d1 = entry->deltas[entry->delta_index];
	deltaType d2 = entry->deltas[entry->delta_index - 1];
	Addr adress = entry->last_adress;

	int candidate_index = 0;
	// TODO: Circular buffer.
	for (int i = entry->delta_index - 2, j = 0; j < NUM_DELTAS; i--, j++) {
		deltaType u, v;
		u = entry->deltas[i - 1];
		v = entry->deltas[i];
		if (u == d1 && v == d2) {
			// TODO: Circular buffer
			int k = i;
			for (; j >= 0; j--) {
				// TODO: Dirty hack, we seem to get an overflow SOMEWHERE, so to avoid those entries:
				if (entry->deltas[k] > 1000) {
					break;
				}
				adress += entry->deltas[k];
				candidates[candidate_index++] = adress;
				if (candidate_index == NUM_DELTAS) {
					break;
				}
				k++;
			}
			break;
		}
	}
}

void prefetch_filter(delta_entry *entry, Addr *candidates)
{
	for (int i = 0; i < NUM_DELTAS; i++) {
		if (candidates[i] == 0) {
			return;
		}
		if (!in_cache(candidates[i])) { // TODO: More tests
			issue_prefetch(candidates[i]);
		}
		entry->last_prefetch = candidates[i]; // TODO: Implement usage of this
	}
}

void prefetch_access(AccessStat stat)
{
    /* pf_addr is now an address within the _next_ cache block */
	Addr curr_addr = stat.mem_addr;
	delta_entry *entry = locate_entry_for_PC(stat.pc);

	Addr candidates[NUM_DELTAS];

	// From pseudocode in paper (Grannæs et al, Algorithm 1)
	if (stat.pc != entry->PC) {
		entry->init_delta_entry(stat.pc, curr_addr);
	} else if (curr_addr - entry->last_adress != 0)  {
		entry->insert_delta_into_entry(curr_addr);

		delta_correlation(entry, candidates);
		prefetch_filter(entry, candidates); 
	}
}

void prefetch_complete(Addr addr)
{
    /*
     * Called when a block requested by the prefetcher has been loaded.
     */
}
