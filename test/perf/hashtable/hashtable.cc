// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include <cpp/when.h>
#include <debug/harness.h>
#include <memory>

#define NUM_BUCKETS             1
#define NUM_ENTRIES_PER_BUCKET  128
#define NUM_OPERATIONS          100000000
#define RW_RATIO                90          // X% readers

#define DEBUG_RW                0

using namespace verona::cpp;

class Entry {
  public:
    union {
        size_t val;
        char alignment[64];
    };
    Entry(size_t val) : val(val) {}
};

class Bucket
{
  public:
    std::vector<Entry*> list;

    Bucket(std::vector<Entry*> list) : list(list) {}

    uint64_t get_addr() {
      return (uint64_t)this;
    }
};

thread_local long found_read_ops = 0;
thread_local long not_found_read_ops = 0;
thread_local long found_write_ops = 0;
thread_local long not_found_write_ops = 0;

std::atomic<long> total_found_read_ops = 0;
std::atomic<long> total_not_found_read_ops = 0;
std::atomic<long> total_found_write_ops = 0;
std::atomic<long> total_not_found_write_ops = 0;

void test_hash_table(std::shared_ptr<std::array<std::atomic<size_t>, NUM_BUCKETS * 2>> stats)
{
    auto t1 = high_resolution_clock::now();
    size_t num_buckets = NUM_BUCKETS;
    size_t num_entries_per_bucket = NUM_ENTRIES_PER_BUCKET;
    size_t num_operations = NUM_OPERATIONS;
    size_t rw_ratio = RW_RATIO; 
    
    std::shared_ptr<std::vector<cown_ptr<Bucket>>> buckets = std::make_shared<std::vector<cown_ptr<Bucket>>>();

    for(size_t i = 0; i < num_buckets; i++) {
        std::vector<Entry*> list;
        for(size_t j = 0; j < (num_entries_per_bucket); j++)
            list.push_back(new Entry((num_buckets * j) + i));
        buckets->push_back(make_cown<Bucket>(list));
    }

    for(size_t i = 0; i < num_operations; i++) {
        size_t key = rand() % (num_buckets * num_entries_per_bucket * 2);
        size_t idx = key % num_buckets;
        if(rand() % 100 < rw_ratio) {
            when(read((*buckets)[idx])) << [key] (acquired_cown<const Bucket> bucket) {
#if DEBUG_RW
                auto val = (*stats)[idx].fetch_add(2);
                Logging::cout() << "Reader idx:" << idx << " val: " << val << Logging::endl;
                check(val % 2 == 0);
                if((*stats)[idx + NUM_BUCKETS].load() < (val + 2)) {
                    (*stats)[idx + NUM_BUCKETS].store(val + 2);
                    printf("idx: %ld val: %ld\n", idx, val + 2);
                }
#endif

                bool found = false;
                for(auto it: bucket->list) {
                    if(it->val == key) {
                        found = true;
                        break;
                    }
                }

                if(found)
                    found_read_ops++;
                else
                    not_found_read_ops++;
#if DEBUG_RW
                (*stats)[idx].fetch_sub(2);
#endif
            };
        } else {
            when((*buckets)[idx]) << [key] (acquired_cown<Bucket> bucket) {
#if DEBUG_RW
                auto val = (*stats)[idx].fetch_add(1);
                Logging::cout() << "Writer idx:" << idx << " val: " << val << Logging::endl;
                check(val == 0);
#endif
                bool found = false;
                for(auto it: bucket->list) {
                    if(it->val == key) {
                        found = true;
                        break;
                    }
                }

                if(found)
                    found_write_ops++;
                else
                    not_found_write_ops++;
#if DEBUG_RW
                (*stats)[idx].fetch_sub(1);
#endif
            };
        }
        Logging::cout() << "Index added: " << i << Logging::endl; 
    }
    auto t2 = high_resolution_clock::now();
    auto ns_int = duration_cast<nanoseconds>(t2 - t1);
    auto us_int = duration_cast<microseconds>(t2 - t1);
    auto ms_int = duration_cast<milliseconds>(t2 - t1);
    std::cout << "Behaviour generation Elapsed time: " << ms_int.count() << "ms " <<  us_int.count() << "us " << ns_int.count() << "ns" << std::endl;

    std::cout << "Total ops: " << (total_found_read_ops.load() + total_found_write_ops.load() + total_not_found_read_ops.load() + total_not_found_write_ops.load())
                         << " found read ops: " << total_found_read_ops.load() 
                         << " not found read ops: " << total_not_found_read_ops.load()
                         << " found write ops: " << total_found_write_ops.load()
                         << " not found write ops: " << total_not_found_write_ops.load() << std::endl;
}

void finish(void) {
    std::stringstream ss;
    ss << "Thread: " << std::this_thread::get_id() << " found read ops: " << found_read_ops << " not found read ops: " << not_found_read_ops 
                                                   << " found write ops: " << found_write_ops << " not found write ops: " << not_found_write_ops<< "\n";
    total_found_read_ops.fetch_add(found_read_ops);
    total_not_found_read_ops.fetch_add(not_found_read_ops);
    total_found_write_ops.fetch_add(found_write_ops);
    total_not_found_write_ops.fetch_add(not_found_write_ops);
    std::cout << ss.str();
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);
  auto stats = std::make_shared<std::array<std::atomic<size_t>, NUM_BUCKETS * 2>>();
  
  harness.endf = finish;

  auto t1 = high_resolution_clock::now();
  harness.run(test_hash_table, stats);
  auto t2 = high_resolution_clock::now();

  for(int i = 0; i < NUM_BUCKETS; i++)
    assert((*stats)[i].load() == 0);

  std::cout << "Total ops: " << (total_found_read_ops.load() + total_found_write_ops.load() + total_not_found_read_ops.load() + total_not_found_write_ops.load())
                         << " found read ops: " << total_found_read_ops.load() 
                         << " not found read ops: " << total_not_found_read_ops.load()
                         << " found write ops: " << total_found_write_ops.load()
                         << " not found write ops: " << total_not_found_write_ops.load() << std::endl;

  auto ns_int = duration_cast<nanoseconds>(t2 - t1);
  auto us_int = duration_cast<microseconds>(t2 - t1);
  auto ms_int = duration_cast<milliseconds>(t2 - t1);
  std::cout << "Elapsed time: " << ms_int.count() << "ms " <<  us_int.count() << "us " << ns_int.count() << "ns" << std::endl;
}
