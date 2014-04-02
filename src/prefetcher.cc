/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include <vector>

#include "interface.hh"
#include "DeltaEntry.hh"
#include "DeltaArray.hh"

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
  _PC(0),
  _last_address(0),
  _last_prefetch(0),
  _data(NUM_DELTAS),
  _data_size(NUM_DELTAS),
  _delta_index(0)
{}

DeltaEntry::DeltaEntry (int n) :
  _PC(0),
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

  for (int i = 0; i < _data_size; i++) {
      candidates[i] = 0;
  }

  delta_t d1 = _data[_delta_index];
  delta_t d2 = _data[_delta_index - 1];
  Addr address = _last_address;

  candidate_index = 0;

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
    }
  }
}

void DeltaEntry::filter (Addr *candidates)
{
  for (int i = 0; i < _data_size; i++)
  {
    if (candidates[i] == 0)
    {
      return;
    }

    if (!in_cache(candidates[i]))
    {
      issue_prefetch(candidates[i]);
    }
    _last_prefetch = candidates[i];
  }
}

void DeltaEntry::initialize (Addr PC, Addr last_address)
{
  _PC = PC;
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

Addr DeltaEntry::getPC ()
{
  return _PC;
}

Addr DeltaEntry::getLastAddress ()
{
  return _last_address;
}

int lru_index = 0;
DeltaEntry entries[TABLE_SIZE];

DeltaEntry* locate_entry_for_PC(Addr PC)
{
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (entries[i].getPC() == PC) {
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

  DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

void prefetch_access(AccessStat stat)
{
  /* pf_addr is now an address within the _next_ cache block */
  Addr curr_addr = stat.mem_addr;
  DeltaEntry *entry = locate_entry_for_PC(stat.pc);
  Addr candidates[NUM_DELTAS];

  // From pseudocode in paper (Grannæs et al, Algorithm 1)
  if (stat.pc != entry->getPC())
  {
      entry->initialize(stat.pc, curr_addr);
  }
  else if (curr_addr - entry->getLastAddress() != 0)
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
}
