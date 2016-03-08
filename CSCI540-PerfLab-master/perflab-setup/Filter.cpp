#include "Filter.h"

Filter::Filter(int _dim)
{
  divisor = 1;
  dim = _dim;
  data = new int[dim * dim];
}
