// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include <cpp/when.h>
#include <debug/harness.h>
#include <memory>

using namespace verona::cpp;

class Bucket
{
  public:
    std::vector<size_t> list;

    Bucket(std::vector<size_t> list) : list(list) {}

    uint64_t get_addr() {
      return (uint64_t)this;
    }
};

void test_hash_table(std::shared_ptr<std::array<std::atomic<size_t>,4>> stats)
{
    auto t1 = high_resolution_clock::now();
    size_t num_buckets = 128;
    size_t num_entries_per_bucket = 4;
    size_t num_operations = 100000000;
    size_t rw_ratio = 90; // X% readers
    
    std::shared_ptr<std::vector<cown_ptr<Bucket>>> buckets = std::make_shared<std::vector<cown_ptr<Bucket>>>();

    for(size_t i = 0; i < num_buckets; i++) {
        std::vector<size_t> list;
        for(size_t j = 0; j < (num_entries_per_bucket); j++)
            list.push_back((num_entries_per_bucket * i) + j);
        buckets->push_back(make_cown<Bucket>(list));
    }

    for(size_t i = 0; i < num_operations; i++) {
        volatile size_t key = rand() % (num_buckets * num_entries_per_bucket * 2);
        volatile size_t idx = key % num_buckets;
        if(rand() % 100 < rw_ratio) {
            when(read((*buckets)[idx])) << [key, stats] (acquired_cown<const Bucket> bucket) {
                volatile bool found = false;
                for(auto it: bucket->list) {
                    if(it == key) {
                        found = true;
                        break;
                    }
                }
                // if(found) {
                //     //Logging::cout() << "Key for read found " << key << Logging::endl; 
                //     (*stats)[0].fetch_add(1);
                // }
                // else {
                //     //Logging::cout() << "Key for read not found " << key << Logging::endl;
                //     (*stats)[1].fetch_add(1);
                // }
            };
        } else {
            when((*buckets)[idx]) << [key, stats] (acquired_cown<Bucket> bucket) {
                volatile bool found = false;
                for(auto it: bucket->list) {
                    if(it == key) {
                        found = true;
                        break;
                    }
                }
                // if(found) {
                //     //Logging::cout() << "Key for write found " << key << Logging::endl;
                //     (*stats)[2].fetch_add(1);
                // }
                // else {
                //     //Logging::cout() << "Key for write not found " << key << Logging::endl;
                //     (*stats)[3].fetch_add(1);
                // }
            };
        }
        Logging::cout() << "Index added: " << i << Logging::endl; 
    }
    auto t2 = high_resolution_clock::now();
    auto ns_int = duration_cast<nanoseconds>(t2 - t1);
    auto us_int = duration_cast<microseconds>(t2 - t1);
    auto ms_int = duration_cast<milliseconds>(t2 - t1);
    std::cout << "Behaviour generation Elapsed time: " << ms_int.count() << "ms " <<  us_int.count() << "us " << ns_int.count() << "ns" << std::endl;
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);
  auto stats = std::make_shared<std::array<std::atomic<size_t>,4>>();

  auto t1 = high_resolution_clock::now();
  harness.run(test_hash_table, stats);
  auto t2 = high_resolution_clock::now();

  for(int i = 0; i < 4; i++)
    std::cout << i << " " << (*stats)[i].load() << std::endl;

  auto ns_int = duration_cast<nanoseconds>(t2 - t1);
  auto us_int = duration_cast<microseconds>(t2 - t1);
  auto ms_int = duration_cast<milliseconds>(t2 - t1);
  std::cout << "Elapsed time: " << ms_int.count() << "ms " <<  us_int.count() << "us " << ns_int.count() << "ns" << std::endl;
}
