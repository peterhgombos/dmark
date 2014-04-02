#ifndef __DELTA_T_
#define __DELTA_T_

#define TABLE_SIZE 60
#define NUM_DELTAS 20

typedef int16_t delta_t;

class DeltaArray
{
private:
  delta_t* arr;
  int size;

public:
  DeltaArray (int n);
  delta_t get(int index);
};

#endif /* __DELTA_T_ */
