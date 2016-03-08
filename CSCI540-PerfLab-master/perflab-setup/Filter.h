//-*-c++-*-
#ifndef _Filter_h_
#define _Filter_h_
#include <iostream>
using namespace std;

class Filter {
  int divisor;
  int dim;
  int *data;

public:
  Filter(int _dim);
  inline int get(int &r, int &c)
  {
    return data[ r * dim + c ];
  }
  inline void set(int &r, int &c, int &value)
  {
    data[ r * dim + c ] = value;
  }

  inline int getDivisor()
  {
    return divisor;
  }


  inline void setDivisor(int &value)
  {
    divisor = value;
  }
  inline int getSize()
  {
    return dim;
  }

  inline void info()
  {
    cout << "Filter is.." << endl;
    for (int col = 0; col < dim; col++) {
      for (int row = 0; row < dim; row++) {
        int v = get(row, col);
        cout << v << " ";
      }
      cout << endl;
    }
  }

};

#endif
