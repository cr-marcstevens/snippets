#include <iostream>
#include <random>
#include <ctime>
#include <chrono>
#include <parallel/algorithm>
#include <omp.h>

#include "parallel_algorithms.hpp"

namespace tp = thread_pool;
namespace pa = parallel_algorithms;

template<typename Cont, typename Threadpool>
void test(int id, Cont& input, std::size_t i, Threadpool& threadpool, std::size_t chunksize)
{
   typedef typename Cont::value_type value_type;
   value_type x = input[i];

   Cont input2;

//   namespace def = std;
   namespace def = __gnu_parallel;

   switch (id)
   {
      default: throw;

      case 1:
         pa::partition(input.begin(), input.end(), [=](const value_type& y) { return y<x; }, threadpool, chunksize);
         return;
      case 2:
         def::partition(input.begin(), input.end(), [=](const value_type& y) { return y<x; });
         return;

      case 3:
         pa::nth_element(input.begin(), input.begin()+i, input.end(), threadpool, chunksize);
         return;
      case 4:
         def::nth_element(input.begin(), input.begin()+i, input.end());
         return;

      case 5:
         input2 = input;
         pa::merge(input.begin(), input.begin()+i, input.begin()+i, input.end(), input2.begin(), threadpool);
         input.swap(input2);
         return;
      case 6:
         input2 = input;
         def::merge(input.begin(), input.begin()+i, input.begin()+i, input.end(), input2.begin());
         input.swap(input2);
         return;

      case 7:
         pa::sort(input.begin(), input.end(), threadpool, chunksize);
         return;
      case 8:
         def::sort(input.begin(), input.end());
         return;
   };
}

int main(int argc, char** argv)
{
 tp::thread_pool threadpool(79);
 std::cout << threadpool.size() << std::endl;

 omp_set_dynamic(false);
 omp_set_num_threads(80);

 std::random_device r;
 std::size_t seed = r();
 std::cout << "seed=" << seed << std::endl;
 std::default_random_engine prng(seed);


 for (std::size_t chunksize = 1024; chunksize <= 8192; chunksize *=2)
 {
   std::cout << chunksize << std::endl;

   double tottime1 = 0, tottime2 = 0;
   int timecnt = 0;

   for (int cn = 0; cn < 128; ++cn)
   {
      std::size_t size = 1 << 25;
//      size = 1 << (15 + (prng() % 10));
//	size = prng() % size;
      std::size_t nth = (prng() % size) /2 + size/4;
      std::vector<unsigned> rndvec(size);
      for (auto& x : rndvec)
         x = prng();

//      int testid = 1; // partition
//      int testid = 3; // nth_element
//      int testid = 5; // merge
      int testid = 7; // sort

      if (testid == 5)
      {
         __gnu_parallel::sort(rndvec.begin(), rndvec.begin()+nth);
         __gnu_parallel::sort(rndvec.begin()+nth, rndvec.end());
      }

      unsigned x = rndvec[nth];
      auto rndvec2 = rndvec;

      auto begt =  std::chrono::high_resolution_clock::now();
      test(testid,   rndvec,  nth, threadpool, chunksize);
      auto midt =  std::chrono::high_resolution_clock::now();
      test(testid+1, rndvec2, nth, threadpool, chunksize);
      auto endt = std::chrono::high_resolution_clock::now();

//      std::cout << size << " \t " << nth << " \t " << rndvec[nth] << " \t " << rndvec2[nth] << std::endl;
      double time1 = std::chrono::duration<double, std::milli>(midt-begt).count();
      double time2 = std::chrono::duration<double, std::milli>(endt-midt).count();
      tottime1 += time1; tottime2 += time2; ++timecnt;
//      std::cout << time1 << " " << std::flush;

      if (1)
      {
         std::size_t bad = 0;
         switch (testid) {
            default: throw;
            case 1:
               for (std::size_t i = 0; i < rndvec.size(); ++i)
                  if ((rndvec[i]<x) != (rndvec2[i]<x))
                  {
                     std::cout << "i=" << i << " x=" << x << " rvi=" << rndvec[i] << " rv2i=" << rndvec2[i] << " " << (rndvec[i]<x) << (rndvec2[i]<x) << std::endl;
                     ++bad;
                  }
               break;
            
            case 3:
               if (rndvec[nth] != rndvec2[nth])
                     throw std::runtime_error("test failed");
               break;
            
            case 5:
            case 7:
               if (rndvec != rndvec2)
                     throw std::runtime_error("test failed");
               break;

         };
         if (bad)
         {
            std::cout << "bad=" << bad << std::endl;
            throw std::runtime_error("test failed");
         }
      }
   }
   std::cout << "\n" << chunksize << ": " << tottime1/double(timecnt) << " " << tottime2/double(timecnt) << std::endl;
//   break;
 }
 return 0;
}
