#ifndef __DELTA_T_
#define __DELTA_T_

#define TABLE_SIZE 60
#define NUM_DELTAS 20

typedef int16_t delta_t;

class DeltaArray
{
private:
  delta_t* _arr;
  int _size;

public:
  DeltaArray (int n);
  delta_t get(int index);
  delta_t& operator[](int index);
  void zero(void);
};

#endif /* __DELTA_T_ */
