/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include <vector>
#include <climits>

#include "interface.hh"

const uint32_t TABLE_SIZE = 73; // The full table size, including the Tier1-elements if in Tiered mode.
#define TIER1_SIZE 91
#define NUM_DELTAS 23
// These should in _theory_ be sizeofs, but since Addr is larger than what we actually need, we just lie
#define TIER1_ENTRY_SIZE 8
#define TIER3_ENTRY_SIZE (8+4+NUM_DELTAS*2+1)
#define TIER3_RATIO (TIER3_ENTRY_SIZE/TIER1_ENTRY_SIZE) // Make sure this is an integral ratio.
#define TIER3_REDUCTION (TABLE_SIZE - (TIER1_SIZE/TIER3_RATIO))

#define BUFFER_TOLERANCE 0.70
#define BUFFER_DEADZONE 0.10

typedef int16_t delta_t;

// Global counting of hits //
int64_t t1_hit = 0;
int64_t prefetch_count = 0; 

///////////////////////////
////// DeltaArray   ///////
///////////////////////////

class DeltaArray
{
private:
  delta_t* _arr;
  int _size;

public:
  DeltaArray (int n);
  DeltaArray (const DeltaArray &rhs);
  DeltaArray& operator= (const DeltaArray &rhs);
  delta_t get(int index) { return (*this)[index]; }
  delta_t& operator[](int index);
  void zero(void);
};

DeltaArray::DeltaArray (int n) :
  _arr(NULL),
  _size(n)
{
  _arr = new delta_t[n];

  for (int i = 0; i < n; i++)
  {
    _arr[i] = 0;
  }
}

DeltaArray::DeltaArray(const DeltaArray &rhs)
{
  _size = rhs._size;
  _arr = new delta_t[_size];
  for (int i = 0; i < _size; i++)
  {
    _arr[i] = rhs._arr[i];
  }
}

DeltaArray& DeltaArray::operator=(const DeltaArray &rhs)
{
  delete[] _arr;
  _size = rhs._size;
  _arr = new delta_t[_size];
  for (int i = 0; i < _size; i++)
  {
    _arr[i] = rhs._arr[i];
  }
  return *this;
}

delta_t& DeltaArray::operator[](int index)
{
  if (index >= 0 && index < _size)
  {
    return _arr[index];
  }
  else if (index < 0)
  {
    return _arr[_size + (index % _size)];
  }
  else
  {
    return _arr[index % _size];
  }
}

void DeltaArray::zero (void)
{
  for (int i = 0; i < _size; i++)
  {
    _arr[i] = 0;
  }
}

///////////////////////////
////// DeltaEntry   ///////
///////////////////////////

class DeltaEntry
{
private:
  Addr _pc;
  Addr _last_address;
  Addr _last_prefetch;
  DeltaArray _data;
  int _data_size;
  int _delta_index;

public:
  DeltaEntry (void);
  DeltaEntry& operator=(const DeltaEntry &rhs);
  void correlation (Addr *candidates);
  void filter (Addr *candidates);
  void initialize (Addr PC, Addr last_address);
  void insert (Addr current_address);
  void print();

  Addr pc() const { return _pc; };
  Addr last_address() const { return _last_address; };
};

DeltaEntry::DeltaEntry () :
  _pc(0),
  _last_address(0),
  _last_prefetch(0),
  _data(NUM_DELTAS),
  _data_size(NUM_DELTAS),
  _delta_index(0)
{}

DeltaEntry& DeltaEntry::operator=(const DeltaEntry &rhs)
{
  _pc = rhs._pc;
  _last_address = rhs._last_address;
  _last_prefetch = rhs._last_prefetch;
  _data = rhs._data;
  _data_size = rhs._data_size;
  _delta_index = rhs._delta_index;

  return *this;
}

void DeltaEntry::correlation (Addr *candidates)
{
  int candidate_index = 0;
  delta_t d1 = _data[_delta_index];
  delta_t d2 = _data[_delta_index - 1];
  Addr address = _last_address;

  for (int i = 0; i < _data_size; i++)
  {
      candidates[i] = 0;
  }

  for (int i = _delta_index - 2, j = 0; j < _data_size; i--, j++)
  {
    delta_t u, v;
    u = _data[i - 1];
    v = _data[i];
    if (u == d1 && v == d2)
    {
      int k = i;
      while (j >= 0)
      {
        address += _data[k];
        candidates[candidate_index++] = address;

        if (candidate_index == _data_size)
        {
          break;
        }

        j--;
        k++;
      }
      break;
    }
	break;
  }
}

void DeltaEntry::filter (Addr *candidates)
{
  Addr toBePrefetched[NUM_DELTAS] = {0};
  int index = 0;
  for (int i = 0; i < _data_size; i++)
  {
    if (candidates[i] == 0)
    {
      break;
    }

    if (_last_prefetch == candidates[i]) 
	  {
      index = 0;
      toBePrefetched[0] = 0;
	  }
    if (!in_cache(candidates[i]) && !in_mshr_queue(candidates[i]))
    {
	    toBePrefetched[index++] = candidates[i];
      _last_prefetch = candidates[i];
    }
  }
  for (int i = 0; i < NUM_DELTAS; i++) 
  {
    if (toBePrefetched[i] != 0)
	  {
      DPRINTF(HWPrefetch, "Issuing prefetch for %d\n", toBePrefetched[i]);
      issue_prefetch(toBePrefetched[i]);
	  } 
	  else 
	  {
	    break;
	  }
  }
}

void DeltaEntry::initialize (Addr pc, Addr last_address)
{
  _pc = pc;
  _last_address = last_address;

  _data.zero();
}

void DeltaEntry::insert (Addr current_address)
{
  _data[_delta_index++] = current_address - _last_address;
  _last_address = current_address;

  if (_delta_index == _data_size)
  {
    _delta_index = 0;
  }
}

class Tier1Entry
{
  Addr _PC;
  Addr _last_address;
public:
  Tier1Entry() { initialize(0, 0); }
  void initialize(Addr PC, Addr address)
  {
    _PC = PC;
    _last_address = address;
  }
  Addr pc() { return _PC; }
  Addr last_address() { return _last_address; }
};

///////////////////////////
////// Prefetcher   ///////
///////////////////////////

enum bufferMode
{
  TIERED,
  TIER3_ONLY
};


int gCurrentTier3Size = TABLE_SIZE - TIER3_REDUCTION;
int gCurrentTier1Size = TIER1_SIZE; 

bufferMode gBufferMode = TIERED;
int lru_index = 0;
std::vector<DeltaEntry> entries(TABLE_SIZE, DeltaEntry());
int tier1_index = 0;
std::vector<Tier1Entry> t1Entries(TIER1_SIZE, Tier1Entry());

void switch_mode_to(bufferMode mode) 
{
  int num_to_compress = TIER3_REDUCTION;

  if (mode == gBufferMode)
  {
    return;
  }

  if (gBufferMode == TIER3_ONLY && mode == TIERED)
  {
    // Ready the t1-buffer.
    for (int i = 0; i < TIER1_SIZE; i++)
    {
      t1Entries[i].initialize(0, 0);
    }
    // We need to compress some elements down to their tiered equivalent.
    for (int i = 0; i < num_to_compress; i++)
    {
      DeltaEntry *to_compress = &(entries[lru_index++]);
      if (lru_index == TABLE_SIZE) 
      {
        lru_index = 0;
      }
      t1Entries[i].initialize(to_compress->pc(), to_compress->last_address());
      to_compress->initialize(0, 0);
    }

    // Now actually compress the buffer:
    for (int i = TABLE_SIZE - 1; i >= 0; i--)
    {
      if (num_to_compress == 0)
      {
        break;
      }
      if (entries[i].pc() == 0)
      {
        for (int j = i; j < TABLE_SIZE - 2; j++)
        {
          entries[j] = entries[j + 1];
        }
        num_to_compress--;
      }
    }
    gBufferMode = TIERED;
    gCurrentTier3Size = TABLE_SIZE - TIER3_REDUCTION;
    gCurrentTier1Size = TIER1_SIZE;
    lru_index = lru_index % gCurrentTier3Size;
  }
  else if (gBufferMode == TIERED && mode == TIER3_ONLY)
  {
    // Simulate expansion, this will potentially have some issues with the lru-position
    int num_to_decompress = TIER3_REDUCTION;
    for (int i = 0; i < num_to_decompress; i++) 
    {
      int offset = TABLE_SIZE - TIER3_REDUCTION + i;
      entries[offset].initialize(t1Entries[tier1_index].pc(), t1Entries[tier1_index].last_address());
      if (tier1_index == 0) 
      {
        tier1_index = TIER1_SIZE - 1;
      } 
      else 
      {
        tier1_index--;
      }
    }
    gBufferMode = TIER3_ONLY;
    gCurrentTier3Size = TABLE_SIZE;
    gCurrentTier1Size = 0;
  }
}

DeltaEntry* locate_entry_for_pc(Addr pc)
{
	for (int i = 0; i < gCurrentTier3Size; i++)
    {
		if (entries[i].pc() == pc)
    {
			return &(entries[i]);
		}
	}

	if (lru_index == gCurrentTier3Size)
  {
		lru_index = 0;
	}

	return &(entries[lru_index++]);
}

Tier1Entry* locate_tier1_for_pc(Addr pc)
{
  if (gBufferMode == TIER3_ONLY) 
  {
    DPRINTF(HWPrefetch, "locate tier1 for PC called while running in TIER3_ONLY_MODE");    
  }
  /* Loop through all T1 items */
  for (int i = 0; i < gCurrentTier1Size; i++) 
  {
    if (t1Entries[i].pc() == pc) 
    {
      return &(t1Entries[i]);
    }
  }

  if (tier1_index == gCurrentTier1Size)
  {
    tier1_index = 0;
  }

  return &(t1Entries[tier1_index++]);
}

void prefetch_init(void)
{
  /* Called before any calls to prefetch_access. */
  /* This is the place to initialize data structures. */
  DPRINTF(HWPrefetch, "Initialized DCTP-prefetcher\n");
}

void prefetch_access(AccessStat stat)
{
  if (prefetch_count != LLONG_MAX)
  {
    prefetch_count++;
  }
  else
  {
    prefetch_count <<= 32;
    t1_hit <<= 32;
  }


  //DPRINTF(HWPrefetch, "Prefetch Access PC: %d Addr: %d Prefetch_count: %d LRU-index: %d\n", stat.pc, stat.mem_addr, prefetch_count, lru_index);
  DPRINTF(HWPrefetch, "Prefetch Access PC: %d t1_hit %d prefetch_count: %d ratiot1: %f\n", stat.pc, t1_hit, prefetch_count, ((double)t1_hit/prefetch_count));
  /* pf_addr is now an address within the _next_ cache block */
  Addr curr_addr = stat.mem_addr;
  DeltaEntry *entry = locate_entry_for_pc(stat.pc);
  Addr candidates[NUM_DELTAS];

  /* Entry is not in T3 */
  if (stat.pc != entry->pc())
  {
    /* We are in TIERED-mode, use T1 for initial-attempts */ 
    if (gBufferMode == TIERED) {
      Tier1Entry *t1Entry = locate_tier1_for_pc(stat.pc);

     /* Entry is already in T1, and should be upgraded to T3 */
      if (stat.pc == t1Entry->pc())
      {
        DPRINTF(HWPrefetch, "Entry is already in T1, and should be upgraded to T3\n");
        entry->initialize(stat.pc, t1Entry->last_address());
        entry->insert(curr_addr);
        t1Entry->initialize(0, 0);
      }
      /* Entry is new (not in either) */
      else
      {
        t1_hit++;

        DPRINTF(HWPrefetch, "Entry is new, not in either\n");
        t1Entry->initialize(stat.pc, curr_addr);
      }
    } 
    // We are in TIER3_ONLY-mode, and thus we must modify T3 directly without T1.
    else  
    {
      t1_hit++;

      /* Switch mode from tier 3 only to tiered */
      if ((gBufferMode == TIER3_ONLY) && (((double)t1_hit) / prefetch_count > BUFFER_TOLERANCE))
      {
        DPRINTF(HWPrefetch, "Switching mode to Tiered t1_hit: %d prefetch_count: %d ratio: %f\n", t1_hit, prefetch_count, ((double)t1_hit/prefetch_count));
        switch_mode_to(TIERED);
        Tier1Entry *t1Entry = locate_tier1_for_pc(stat.pc);
        t1Entry->initialize(stat.pc, curr_addr);
      }
      else
      {
        DPRINTF(HWPrefetch, "In T3-Mode: Initialize new\n");
        entry->initialize(stat.pc, curr_addr);
      }
    }
  }
  /* Entry is already in T3 */
  else if (curr_addr - entry->last_address() != 0)
  {
    /* Switch mode from tiered to tier 3 only */
    if ((gBufferMode == TIERED) && (((double)t1_hit) / prefetch_count < (BUFFER_TOLERANCE - BUFFER_DEADZONE)))
    {
        DPRINTF(HWPrefetch, "Switching mode to Tier3-only t1_hit: %d prefetch_count: %d ratio: %f\n", t1_hit, prefetch_count, ((double)t1_hit/prefetch_count));
        switch_mode_to(TIER3_ONLY);
    }
    entry->insert(curr_addr);
    entry->correlation(candidates);
    entry->filter(candidates);
  }
}

void prefetch_complete(Addr addr)
{
    /*
     * Called when a block requested by the prefetcher has been loaded.
     */
}
