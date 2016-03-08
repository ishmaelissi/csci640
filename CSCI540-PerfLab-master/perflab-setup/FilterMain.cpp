#include <stdio.h>
#include "cs1300bmp.h"
#include <iostream>
#include <stdint.h>
#include <fstream>
#include "Filter.h"

using namespace std;

#include "rtdsc.h"

//
// Forward declare the functions
//
Filter * readFilter(string filename);
double applyFilter(Filter *filter, cs1300bmp *input, cs1300bmp *output);

	int
main(int argc, char **argv)
{

	if ( argc < 2) {
		fprintf(stderr,"Usage: %s filter inputfile1 inputfile2 .... \n", argv[0]);
	}

	//
	// Convert to C++ strings to simplify manipulation
	//
	string filtername = argv[1];

	//
	// remove any ".filter" in the filtername
	//
	string filterOutputName = filtername;
	string::size_type loc = filterOutputName.find(".filter");
	if (loc != string::npos) {
		//
		// Remove the ".filter" name, which should occur on all the provided filters
		//
		filterOutputName = filtername.substr(0, loc);
	}

	Filter *filter = readFilter(filtername);

	double sum = 0.0;
	int samples = 0;

	for (int inNum = 2; inNum < argc; inNum++) {
		string inputFilename = argv[inNum];
		string outputFilename = "filtered-" + filterOutputName + "-" + inputFilename;
		struct cs1300bmp *input = new struct cs1300bmp;
		struct cs1300bmp *output = new struct cs1300bmp;
		int ok = cs1300bmp_readfile( (char *) inputFilename.c_str(), input);

		if ( ok ) {
			double sample = applyFilter(filter, input, output);
			sum += sample;
			samples++;
			cs1300bmp_writefile((char *) outputFilename.c_str(), output);
		}
		delete input;
		delete output;
	}
	fprintf(stdout, "Average cycles per sample is %f\n", sum / samples);

}

	struct Filter *
readFilter(string filename)
{
	ifstream input(filename.c_str());

	if ( ! input.bad() ) {
		int size = 0;
		input >> size;
		Filter *filter = new Filter(size);
		int div;
		input >> div;
		filter -> setDivisor(div);
		for (int i=0; i < size; i++) {
			for (int j=0; j < size; j++) {
				int value;
				input >> value;
				filter -> set(i,j,value);
			}
		}
		return filter;
	}
}

#if defined(__arm__)
static inline unsigned int get_cyclecount (void)
{
	unsigned int value;
	// Read CCNT Register
	asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(value));
	return value;
}

static inline void init_perfcounters (int32_t do_reset, int32_t enable_divider)
{
	// in general enable all counters (including cycle counter)
	int32_t value = 1;

	// peform reset:
	if (do_reset)
	{
		value |= 2;     // reset all counters to zero.
		value |= 4;     // reset cycle counter to zero.
	}

	if (enable_divider)
		value |= 8;     // enable "by 64" divider for CCNT.

	value |= 16;

	// program the performance-counter control-register:
	asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(value));

	// enable all counters:
	asm volatile ("MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"(0x8000000f));

	// clear overflows:
	asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));
}



#endif



	double
applyFilter(struct Filter *filter, cs1300bmp *input, cs1300bmp *output)
{
#if defined(__arm__)
	init_perfcounters (1, 1);
#endif

	long long cycStart, cycStop;
	  double start,stop;
#if defined(__arm__)
	cycStart = get_cyclecount();
#else
	cycStart = rdtscll();
#endif
	output -> width = input -> width;
	output -> height = input -> height;
	int FilterSize=filter -> getSize();
	int dividerJ=filter -> getDivisor();
	int localFilter[3][3];
  //doing it without fors
/*	for (int j = 0; j < FilterSize; j++)
	{
		for (int i = 0; i < FilterSize; i++)
		{
			localFilter[i][j]=filter -> get(i, j);
		}
	}
  */
  //int i=0,j=0;
    localFilter[0][0]=filter -> get(0, 0);
    localFilter[0][1]=filter -> get(0, 1);
    localFilter[0][2]=filter -> get(0, 2);

    localFilter[1][0]=filter -> get(1, 0);
    localFilter[1][1]=filter -> get(1, 1);
    localFilter[1][2]=filter -> get(1, 2);

    localFilter[2][0]=filter -> get(2, 0);
    localFilter[2][1]=filter -> get(2, 1);
    localFilter[2][2]=filter -> get(2, 2);
  //
	int result0,result1,result2,UltimateResult; //we need to store results here
	int LocalHeight=(input -> height) - 1;
	int LocalWidth=(input -> width) - 1;
	//  cerr<<"Filter SIZE :"<<FilterSize<<endl;
	for(int plane = 0; plane < 3; plane++)
	{
		for(int row = 1; row <LocalHeight  ; row++)
		{
			for(int col = 1; col < LocalWidth; col++)
			{
				//  int t=0;//unused thing
				//  output -> color[plane][row][col] = 0; this is going to get over written
				//
				result0 = (input -> color[plane][row-1][col-1] * localFilter[0][0] );
				result1 = (input -> color[plane][row-1][col] * localFilter[0][1] );
				result2 = (input -> color[plane][row-1][col+1] * localFilter[0][2] );

				result0 += (input -> color[plane][row][col-1] * localFilter[1][0] );
				result1 += (input -> color[plane][row][col] * localFilter[1][1] );
				result2 += (input -> color[plane][row][col+1] * localFilter[1][2] );

				result0 += (input -> color[plane][row+1][col-1] * localFilter[2][0] );
				result1 += (input -> color[plane][row+1][col] * localFilter[2][1] );
				result2 +=(input -> color[plane][row+1][col+1] * localFilter[2][2] );
				//
				UltimateResult = result0 + result1 + result2;
				output -> color[plane][row][col] =UltimateResult / dividerJ;

				if((output -> color[plane][row][col]  < 0 ) ||( output -> color[plane][row][col]  > 255 ))
				{
          if ( output -> color[plane][row][col]  < 0 )
          {
            output -> color[plane][row][col] = 0;
          }
          else
          {
            output -> color[plane][row][col] = 255;
          }
				}
			}
		}
	}
#if defined(__arm__)
	cycStop = get_cyclecount();
#else
	cycStop = rdtscll();
#endif

	double diff = cycStop-cycStart;
	diff = diff * 64;
	double diffPerPixel = diff / (output -> width * output -> height);
	fprintf(stderr, "Took %f cycles to process, or %f cycles per pixel\n",
			diff, diff / (output -> width * output -> height));
	return diffPerPixel;
}
