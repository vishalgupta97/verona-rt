// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include <cpp/when.h>
#include <debug/harness.h>
#include <memory>

using namespace verona::cpp;

class Bucket
{
  public:
    std::vector<int> list;

    Bucket(std::vector<int> list) : list(list) {}

    uint64_t get_addr() {
      return (uint64_t)this;
    }
};

void test_hash_table()
{
    size_t num_buckets = 128;
    size_t num_entries_per_bucket = 4;
    size_t num_operations = 1000;
    size_t rw_ratio = 10; // X% readers

    std::shared_ptr<std::vector<cown_ptr<Bucket>>> buckets = std::make_shared<std::vector<cown_ptr<Bucket>>>();

    for(size_t i = 0; i < num_buckets; i++) {
        std::vector<int> list;
        for(size_t j = 0; j < (num_entries_per_bucket); j++)
            list.push_back((num_entries_per_bucket * i) + j);
        buckets->push_back(make_cown<Bucket>(list));
    }

    when() << [buckets, num_buckets, num_entries_per_bucket, rw_ratio, num_operations](){
        for(size_t i = 0; i < num_operations; i++) {
            size_t key = rand() % (num_buckets * num_entries_per_bucket * 2);
            size_t idx = key % num_buckets;
            if(rand() % 100 < rw_ratio) {
                when(read((*buckets)[idx])) << [key] (acquired_cown<const Bucket> bucket) {
                    bool found = false;
                    for(auto it: bucket->list) {
                        if(it == key) {
                            Logging::cout() <<  "Read Found key " << key << Logging::endl;
                            found = true;
                            break;
                        }
                    }
                    if(!found)
                        Logging::cout() <<  "Read Not found key " << key << Logging::endl;
                };
            } else {
                when((*buckets)[idx]) << [key] (acquired_cown<Bucket> bucket) {
                    bool found = false;
                    for(auto it: bucket->list) {
                        if(it == key) {
                            Logging::cout() <<  "Write Found key " << key << Logging::endl;
                            found = true;
                            break;
                        }
                    }
                    if(!found)
                        Logging::cout() <<  "Write Not found key " << key << Logging::endl;
                };
            }
        }
    };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);

  auto t1 = high_resolution_clock::now();
  harness.run(test_hash_table);
  auto t2 = high_resolution_clock::now();

  auto ns_int = duration_cast<nanoseconds>(t2 - t1);
  auto us_int = duration_cast<microseconds>(t2 - t1);
  auto ms_int = duration_cast<milliseconds>(t2 - t1);
  Logging::cout() << "Elapsed time: " << ms_int.count() << "ms " <<  us_int.count() << "us " << ns_int.count() << "ns" << Logging::endl;
}