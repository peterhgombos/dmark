#ifndef __DELTA_ENTRY_
#define __DELTA_ENTRY_

#include "DeltaArray.hh"

#define TABLE_SIZE 60
#define NUM_DELTAS 20

class DeltaEntry
{
private:
  Addr _PC;
  Addr _last_address;
  Addr _last_prefetch;
  DeltaArray* _data;
  int _data_size;
  int _delta_index;

public:
  DeltaEntry (void);
  DeltaEntry (int n);
  ~DeltaEntry (void);
  void correlation (Addr *candidates);
  void filter (Addr *candidates);
  void initialize (Addr PC, Addr last_address);
  void insert (Addr current_address);
  Addr getPC (void);
  Addr getLastAddress (void);
};

#endif /* __DELTA_ENTRY_ */
