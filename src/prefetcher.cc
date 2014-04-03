/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include <vector>

#include "interface.hh"
#include "DeltaEntry.hh"
#include "DeltaArray.hh"

Addr prefetch_queue[32]; // Size taken from paper by Grannæs et al.
int prefetch_queue_pos = 0;

bool in_prefetch_queue(Addr address) {
  for (int i = 0; i < 32; i++)
  {
    if (prefetch_queue[i] == address)
	{
      return true;
    }
  }
  return false;
}

void prepare_prefetch(Addr address)
{
  for (int i = 0; i < 32; i++)
  {
    if (prefetch_queue[i] == 0)
	{
      prefetch_queue_pos = i;
	}
  }
  prefetch_queue[prefetch_queue_pos++] = address; 
  if (prefetch_queue_pos == 32)
  {
    prefetch_queue_pos--;
  }
}


DeltaArray::DeltaArray (int n) :
  _arr(NULL),
  _size(n)
{
  _arr = new int16_t[n];

  for (int i = 0; i < n; i++)
  {
    _arr[i] = 0;
  }
}

delta_t DeltaArray::get(int index) {
	return (*this)[index];
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

DeltaEntry::DeltaEntry () :
  _pc(0),
  _last_address(0),
  _last_prefetch(0),
  _data(NUM_DELTAS),
  _data_size(NUM_DELTAS),
  _delta_index(0)
{}

DeltaEntry::DeltaEntry (int n) :
  _pc(0),
  _last_address(0),
  _last_prefetch(0),
  _data(n),
  _data_size(n),
  _delta_index(0)
{}

DeltaEntry::~DeltaEntry () {}

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
        if (_data[k] > 1000)
        {
          break;
        }

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
    if (!in_cache(candidates[i]) && !in_mshr_queue(candidates[i]) && !in_prefetch_queue(candidates[i]))
    {
      prepare_prefetch(candidates[i]);
	  toBePrefetched[index++] = candidates[i];
      _last_prefetch = candidates[i];
    }
  }
  for (int i = 0; i < NUM_DELTAS; i++) 
  {
    if (toBePrefetched[i] != 0)
	{
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

int lru_index = 0;
std::vector<DeltaEntry> entries(TABLE_SIZE, DeltaEntry());

DeltaEntry* locate_entry_for_pc(Addr pc)
{
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (entries[i].pc() == pc) {
			return &(entries[i]);
		}
	}
	if (lru_index == TABLE_SIZE) {
		lru_index = 0;
	}
	return &(entries[lru_index++]);
}

void prefetch_init(void)
{
  /* Called before any calls to prefetch_access. */
  /* This is the place to initialize data structures. */
  for (int i = 0; i < 32; i++) {
    prefetch_queue[i] = 0;
  }
  prefetch_queue_pos = 0;
  DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

void prefetch_access(AccessStat stat)
{
  /* pf_addr is now an address within the _next_ cache block */
  Addr curr_addr = stat.mem_addr;
  DeltaEntry *entry = locate_entry_for_pc(stat.pc);
  Addr candidates[NUM_DELTAS*10];

  // From pseudocode in paper (Grannæs et al, Algorithm 1)
  if (stat.pc != entry->pc())
  {
      entry->initialize(stat.pc, curr_addr);
  }
  else if (curr_addr - entry->last_address() != 0)
  {
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
  for (int i = 0; i < 32; i++) {
    if (prefetch_queue[i] == addr) {
      prefetch_queue[i] = 0;
	}
  }
}
