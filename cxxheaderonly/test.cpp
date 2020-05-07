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
   
   switch (id)
   {
      default: throw;
      case 1:
         pa::partition(input.begin(), input.end(), [=](const value_type& y) { return y<x; }, threadpool, chunksize);
         return;
      case 2:
         __gnu_parallel::partition(input.begin(), input.end(), [=](const value_type& y) { return y<x; });
         return;
         
      case 3:
         pa::nth_element(input.begin(), input.begin()+i, input.end(), threadpool, chunksize);
         return;
      case 4:
         __gnu_parallel::nth_element(input.begin(), input.begin()+i, input.end());
         return;
         
      case 5:
         input2 = input;
         pa::merge(input.begin(), input.begin()+i, input.begin()+i, input.end(), input2.begin(), threadpool);
         input.swap(input2);
         return;
         
      case 6:
         input2 = input;
         __gnu_parallel::merge(input.begin(), input.begin()+i, input.begin()+i, input.end(), input2.begin());
         input.swap(input2);
         return;
   };
}

int main(int argc, char** argv)
{
   tp::thread_pool threadpool(37);
   std::cout << threadpool.size() << std::endl;
   
   omp_set_dynamic(false);
   omp_set_num_threads(38);
   
   std::random_device r;
   std::default_random_engine prng(r());
   
   
   for (std::size_t chunksize = 256; chunksize <= 8192; chunksize *=2)
   {
   std::cout << chunksize << std::endl;

   double tottime1 = 0, tottime2 = 0;
   int timecnt = 0;

   for (int cn = 0; cn < 128; ++cn)
   {
      std::size_t size = 1 << 25;//(20+ (prng() % 10));
      std::size_t nth = prng() % size;
      std::vector<unsigned> rndvec(size);
      for (auto& x : rndvec)
         x = prng();
      
      unsigned x = rndvec[nth];

//      int testid = 1; // partition
//      int testid = 3; // nth_element
      int testid = 5; // merge
      
      if (testid == 5)
      {
         __gnu_parallel::sort(rndvec.begin(), rndvec.begin()+nth);
         __gnu_parallel::sort(rndvec.begin()+nth, rndvec.end());
      }

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

      switch (testid) {
         default: throw;
         case 1:
            for (std::size_t i = 0; i < rndvec.size(); ++i)
               if ((rndvec[i]<x) != (rndvec2[i]<x))
                  throw;
            break;
            
         case 3:
            if (rndvec[nth] != rndvec2[nth])
               throw;
            break;
            
         case 5:
            if (rndvec != rndvec2)
               throw;
            break;
      };
      
   }
      std::cout << "\n" << chunksize << ": " << tottime1/double(timecnt) << " " << tottime2/double(timecnt) << std::endl;
   }
   return 0;
}
