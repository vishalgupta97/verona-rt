// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../ds/stackarray.h"
#include "../object/object.h"
#include "cown.h"

#include <snmalloc/snmalloc.h>

namespace verona::rt
{
  using namespace snmalloc;

  class Request
  {
    Cown* _cown;

    static constexpr uintptr_t READ_FLAG = 0x1;
    static constexpr uintptr_t MOVE_FLAG = 0x2;

    Request(Cown* cown) : _cown(cown) {}

  public:
    Request() : _cown(nullptr)
    {
      Logging::cout() << "Inside request constructor" << Logging::endl;
    }

    Cown* cown()
    {
      return (Cown*)((uintptr_t)_cown & ~(READ_FLAG | MOVE_FLAG));
    }

    bool is_read()
    {
      Logging::cout() << "is_read" << Logging::endl;
      return ((uintptr_t)_cown & READ_FLAG);
    }

    bool is_move()
    {
      return ((uintptr_t)_cown & MOVE_FLAG);
    }

    void mark_move()
    {
      _cown = (Cown*)((uintptr_t)_cown | MOVE_FLAG);
    }

    static Request write(Cown* cown)
    {
      Logging::cout() << "Write request" << Logging::endl;
      return Request(cown);
    }

    static Request read(Cown* cown)
    {
      Logging::cout() << "Read request" << Logging::endl;
      return Request((Cown*)((uintptr_t)cown | READ_FLAG));
    }
  };

  struct BehaviourCore;

  struct Slot
  {
    Cown* cown;
    /**
     * Possible values before scheduling to communicate memory management
     * options:
     *   0 - Borrow
     *   1 - Move
     *
     * Possible vales after scheduling:
     *   0 - Wait
     *   1 - Ready
     *   Behaviour* - Next write
     *
     * TODO Read-only When we extend to read-only we will need the following
     * additional state
     *   Slot* - Next Read
     *   2 - Ready and Read available
     */
    std::atomic<uintptr_t> status;

    enum class SlotType
    {
      None, // 0
      Reader, // 1
      Writer // 2
    };

    SlotType slot_type;

    union
    {
      std::atomic<uint16_t> state;
      struct
      {
        uint8_t blocked;
        uint8_t next_slot_type;
      };
    };

#define STATE_TRUE_NONE 0x0001 // blocked = True, next_slot_type = None
#define STATE_TRUE_READER 0x0101 // blocked = False, next_slot_type = Reader

    Slot* next_slot;

    /**
     * Only used by readers to point to the next writer behaviour if any.
     */
    BehaviourCore* next_writer_behaviour;

    Slot(Cown* cown)
    : cown(cown),
      status(0),
      slot_type(SlotType::Writer),
      blocked(true),
      next_slot_type((uint8_t)SlotType::None),
      next_slot(nullptr),
      next_writer_behaviour(nullptr)
    {}

    bool is_read_only()
    {
      return slot_type == SlotType::Reader;
    }

    bool is_next_slot_read_only()
    {
      return next_slot_type == (uint8_t)SlotType::Reader;
    }

    bool is_next_slot_writer()
    {
      return next_slot_type == (uint8_t)SlotType::Writer;
    }

    void set_read_only()
    {
      slot_type = SlotType::Reader;
    }

    void set_next_slot_writer()
    {
      next_slot_type = (uint8_t)SlotType::Writer;
    }
    bool is_ready()
    {
      return status.load(std::memory_order_acquire) == 1;
    }

    void set_move()
    {
      status.store(1, std::memory_order_relaxed);
    }

    void reset_status()
    {
      status.store(0, std::memory_order_relaxed);
    }

    void set_ready()
    {
      status.store(1, std::memory_order_release);
    }

    bool is_wait()
    {
      return status.load(std::memory_order_relaxed) == 0;
    }

    bool is_behaviour()
    {
      return status.load(std::memory_order_relaxed) > 1;
    }

    BehaviourCore* get_behaviour()
    {
      return (BehaviourCore*)status.load(std::memory_order_acquire);
    }

    void set_behaviour(BehaviourCore* b)
    {
      status.store((uintptr_t)b, std::memory_order_release);
    }

    Slot* get_next_slot()
    {
      return next_slot;
    }

    void set_next_slot(Slot* n)
    {
      next_slot = n;
    }

    void release();

    void reset()
    {
      status.store(0, std::memory_order_release);
    }
  };

  /**
   * @brief This class implements the core logic for the `when` construct in the
   * runtime.
   *
   * It is based on the using the class MCS Queue Lock to build a dag of
   * behaviours.  Each cown can be seen as a lock, and each behaviour is
   * like a wait node in the queues for multiple cowns.
   *
   * Unlike, the MCS Queue Lock, we do not spin waiting for the behaviour
   * to be actionable, but instead the behaviour carries the code, that
   * can be scheduled when it has no predecessors in the dag.
   *
   * MCS Queue Lock paper:
   *   J. M. Mellor-Crummey and M. L. Scott. Algorithms for
   *   scalable synchronization on shared-memory
   *   multiprocessors. ACM TOCS, 9(1):21–65, Feb. 1991
   *
   * The class does not instiate the behaviour fully which is done by
   * the `Behaviour` class. This allows for code reuse with a notification
   * mechanism.
   */
  struct BehaviourCore
  {
    std::atomic<size_t> exec_count_down;
    size_t count;

    /**
     * @brief Construct a new Behaviour object
     *
     * @param count - number of cowns to acquire
     *
     * Note that exec_count_down is initialised to count + 1. This is because
     * we use the additional count to protect against the behaviour being
     * completely executed, before we have finished setting Ready on all the
     * slots. Two phase locking needs to complete before we can execute the
     * behaviour.
     */
    BehaviourCore(size_t count) : exec_count_down(count + 1), count(count) {}

    Work* as_work()
    {
      return pointer_offset_signed<Work>(
        this, -static_cast<ptrdiff_t>(sizeof(Work)));
    }

    /**
     * @brief Given a pointer to a work object converts it to a
     * BehaviourCore pointer.
     *
     * This is inherently unsafe, and should only be used when it is known the
     * work object was constructed using `make`.
     */
    static BehaviourCore* from_work(Work* w)
    {
      return pointer_offset<BehaviourCore>(w, sizeof(Work));
    }

    /**
     * Remove `n` from the exec_count_down.
     *
     * Returns true if this call makes the count_down_zero
     */
    bool resolve(size_t n = 1, bool fifo = true, bool schedule = true)
    {
      Logging::cout() << "Behaviour::resolve " << n << " for behaviour " << this
                      << Logging::endl;
      // Note that we don't actually perform the last decrement as it is not
      // required.
      if (
        (exec_count_down.load(std::memory_order_acquire) == n) ||
        (exec_count_down.fetch_sub(n) == n))
      {
        Logging::cout() << "Scheduling Behaviour " << this << Logging::endl;
        if(schedule)
          Scheduler::schedule(as_work(), fifo);
        return true;
      }

      return false;
    }

    // TODO: When C++ 20 move to span.
    Slot* get_slots()
    {
      return pointer_offset<Slot>(this, sizeof(BehaviourCore));
    }

    template<typename T = void>
    T* get_body()
    {
      Slot* slots = pointer_offset<Slot>(this, sizeof(BehaviourCore));
      return pointer_offset<T>(slots, sizeof(Slot) * count);
    }

    /**
     * @brief Constructs a behaviour.  Leaves space for the closure.
     *
     * @param count - Number of slots to allocate, i.e. how many cowns to wait
     * for.
     * @param f - The function to execute once all the behaviours dependencies
     * are ready.
     * @param payload - The size of the payload to allocate.
     * @return BehaviourCore* - the pointer to the behaviour object.
     */
    static BehaviourCore* make(size_t count, void (*f)(Work*), size_t payload)
    {
      // Manual memory layout of the behaviour structure.
      //   | Work | Behaviour | Slot ... Slot | Body |
      size_t size =
        sizeof(Work) + sizeof(BehaviourCore) + (sizeof(Slot) * count) + payload;
      void* base = ThreadAlloc::get().alloc(size);

      Work* work = new (base) Work(f);
      void* base_behaviour = from_work(work);
      BehaviourCore* behaviour = new (base_behaviour) BehaviourCore(count);

      // These assertions are basically checking that we won't break any
      // alignment assumptions on Be.  If we add some actual alignment, then
      // this can be improved.
      static_assert(
        sizeof(Slot) % sizeof(void*) == 0,
        "Slot size must be a multiple of pointer size");
      static_assert(
        sizeof(BehaviourCore) % sizeof(void*) == 0,
        "Behaviour size must be a multiple of pointer size");
      static_assert(
        sizeof(Work) % sizeof(void*) == 0,
        "Work size must be a multiple of pointer size");

      return behaviour;
    }

    /**
     * @brief Schedule a behaviour for execution.
     *
     * @param bodies  The behaviours to schedule.
     *
     * @param body_count The number of behaviours to schedule.
     *
     * @note
     *
     * *** Single behaviour scheduling ***
     *
     * To correctly implement the happens before order, we need to ensure that
     * one when cannot overtake another:
     *
     *   when (a, b, d) { B1 } || when (a, c, d) { B2 }
     *
     * Where we assume the underlying acquisition order is alphabetical.
     *
     * Let us assume B1 exchanges on `a` first, then we need to ensure that
     * B2 cannot acquire `d` before B1 as this would lead to a cycle.
     *
     * To achieve this we effectively do two phase locking.
     *
     * The first (acquire) phase performs exchange on each cown in same
     * global assumed order.  It can only move onto the next cown once the
     * previous behaviour on that cown specifies it has completed its scheduling
     * by marking itself ready, `set_ready`.
     *
     * The second (release) phase is simply making each slot ready, so that
     * subsequent behaviours can continue scheduling.
     *
     * Returning to our example earlier, if B1 exchanges on `a` first, then
     * B2 will have to wait for B1 to perform all its exchanges, and mark the
     * appropriate slot ready in phase two. Hence, it is not possible for any
     * number of behaviours to form a cycle.
     *
     *
     * Invariant: If the cown is part of a chain, then the scheduler holds an RC
     * on the cown. This means the first behaviour to access a cown will need to
     * perform an acquire. When the execution of a chain completes, then the
     * scheduler will release the RC.
     *
     * *** Extension to Many ***
     *
     * This code additional can schedule a group of behaviours atomically.
     *
     *   when (a) { B1 } + when (b) { B2 } + when (a, b) { B3 }
     *
     * This will cause the three behaviours to be scheduled in a single atomic
     * step using the two phase commit.  This means that no other behaviours can
     * access a between B1 and B3, and no other behaviours can access b between
     * B2 and B3.
     *
     * This extension is implemented by building a mapping from each request
     * to the sorted order of.  In this case that produces
     *
     *  0 -> 0, a
     *  1 -> 1, b
     *  2 -> 2, a
     *  3 -> 2, b
     *
     * which gets sorted to
     *
     *  0 -> 0, a
     *  1 -> 2, a
     *  2 -> 1, b
     *  3 -> 2, b
     *
     * We then link the (0,a) |-> (2,a), and enqueue the segment atomically onto
     * cown a, and then link (1,b) |-> (2,b) and enqueue the segment atomically
     * onto cown b.
     *
     * By enqueuing segments we ensure nothing can get in between the
     * behaviours.
     *
     * *** Duplicate Cowns ***
     *
     * The final complication the code must deal with is duplicate cowns.
     *
     * when (a, a) { B1 }
     *
     * To handle this we need to detect the duplicate cowns, and mark the slot
     * as not needing a successor.  This is done by setting the cown pointer to
     * nullptr.
     *
     * Consider the following complex example
     *
     * when (a) { ... } + when (a,a) { ... } + when (a) { ... }
     *
     * This will produce the following mapping
     *
     * 0 -> 0, a
     * 1 -> 1, a (0)
     * 2 -> 1, a (1)
     * 3 -> 2, a
     *
     * This is sorted already, so we can just link the segments
     *
     * (0,a) |-> (1, a (0)) |-> (2, a)
     *
     * and mark (a (1)) as not having a successor.
     */
    static void schedule_many(BehaviourCore** bodies, size_t body_count)
    {
      Logging::cout() << "BehaviourCore::schedule_many" << body_count
                      << Logging::endl;

      size_t count = 0;
      for (size_t i = 0; i < body_count; i++)
        count += bodies[i]->count;

      // Execution count - we will remove at least
      // one from the execution count on finishing phase 2 of the
      // 2PL. This ensures that the behaviour cannot be
      // deallocated until we finish phase 2.
      StackArray<size_t> ec(body_count);
      for (size_t i = 0; i < body_count; i++)
        ec[i] = 1;

      // Need to sort the cown requests across the co-scheduled collection of
      // cowns We first construct an array that represents pairs of behaviour
      // number and slot pointer. Note: Really want a dynamically sized stack
      // allocation here.
      StackArray<std::tuple<size_t, Slot*>> indexes(count);
      size_t idx = 0;
      for (size_t i = 0; i < body_count; i++)
      {
        auto slots = bodies[i]->get_slots();
        for (size_t j = 0; j < bodies[i]->count; j++)
        {
          std::get<0>(indexes[idx]) = i;
          std::get<1>(indexes[idx]) = &slots[j];
          idx++;
        }
      }

      // Sort the indexing array so we make the requests in the correct order
      // across the whole set of behaviours.  A consistent order is required to
      // avoid deadlock.
      // We sort first by cown, and then by behaviour number.
      // These means overlaps will be in a sequence in the array in the correct
      // order with respect to the order of the group of behaviours.
      auto compare = [](
                       const std::tuple<size_t, Slot*> i,
                       const std::tuple<size_t, Slot*> j) {
#ifdef USE_SYSTEMATIC_TESTING
        return std::get<1>(i)->cown->id() == std::get<1>(j)->cown->id() ?
          std::get<0>(i) < std::get<0>(j) :
          std::get<1>(i)->cown->id() < std::get<1>(j)->cown->id();
#else
        return std::get<1>(i)->cown == std::get<1>(j)->cown ?
          std::get<0>(i) < std::get<0>(j) :
          std::get<1>(i)->cown < std::get<1>(j)->cown;
#endif
      };
      if (count > 1)
        std::sort(indexes.get(), indexes.get() + count, compare);

      // First phase - Acquire phase.
      size_t i = 0;
      while (i < count)
      {
        auto cown = std::get<1>(indexes[i])->cown;
        auto body = bodies[std::get<0>(indexes[i])];
        auto curr_slot = std::get<1>(indexes[i]);
        size_t first_chain_index = i;

        // The number of RCs provided for the current cown by the when.
        // I.e. how many moves of cown_refs there were.
        size_t transfer_count = curr_slot->status;

        Logging::cout() << "Processing cown " << cown
                        << " Behaviour " << body
                        << " Behaviour cown count " << body->count
                        << " Behaviour exec count down " << body->exec_count_down
                        << " Slot " << curr_slot
                        << " Index " << i << Logging::endl;

        // Detect duplicates for this cown.
        // This is required in two cases:
        //  * overlaps within a single behaviour.
        while (((++i) < count) && (cown == std::get<1>(indexes[i])->cown))
        {
          // If the body is the same, then we have an overlap within a single
          // behaviour.
          auto body_next = bodies[std::get<0>(indexes[i])];
          if (body_next == body)
          {
            // Check if the caller passed an RC and add to the total.
            transfer_count += std::get<1>(indexes[i])->status;

            Logging::cout() << "Duplicate cown " << cown << " for behaviour "
                            << body << "Index " << i << Logging::endl;
            // We need to reduce the execution count by one, as we can't wait
            // for ourselves.
            ec[std::get<0>(indexes[i])]++;

            // We need to mark the slot as not having a cown associated to it.
            std::get<1>(indexes[i])->cown = nullptr;
            continue;
          } 
          else
          {
            Logging::cout() << "Duplicate cown " << cown << " for behaviour "
                            << body_next 
                            << " previous behaviour " << body << Logging::endl;
            break;
          }
        }

        curr_slot->reset_status();
        yield();
        curr_slot->set_behaviour(body);
        yield();

        if (curr_slot->is_read_only())
        {
          auto prev_slot =
            cown->last_slot.exchange(curr_slot, std::memory_order_acq_rel);
          yield();
          if (prev_slot == nullptr)
          {
            yield();
            bool first_reader = cown->read_ref_count.add_read();
            Logging::cout()
              << curr_slot << " Reader at head of queue and got the cown "
              << cown << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << " behaviour " << body
              << Logging::endl;
            yield();
            curr_slot->blocked = false;
            ec[std::get<0>(indexes[first_chain_index])]++;
            yield();
            if (transfer_count)
            {
              Logging::cout()
                << "Releasing reader transferred count " << transfer_count
                << " -1 for first in queue cown " << cown << Logging::endl;
              // Release transfer_count - 1 times, we needed one as we woke up
              // the cown, but the rest were not required.
              for (int j = 0; j < transfer_count - 1; j++)
                Cown::release(ThreadAlloc::get(), cown);
            }
            else

            {
              Logging::cout() << "Acquiring reader reference count for first "
                                 "in queue on cown "
                              << cown << Logging::endl;
              // We didn't have any RCs passed in, so we need to acquire one.
              Cown::acquire(cown);
            }

            if (first_reader)
            {
              Logging::cout()
                << "Acquiring reference count for first reader on cown " << cown
                << Logging::endl;
              Cown::acquire(cown);
            }

            continue;
          }
          else
          {
            uint16_t state_true_none = STATE_TRUE_NONE;
            uint16_t state_true_reader = STATE_TRUE_READER;
            yield();
            Logging::cout()
              << curr_slot << " Waiting for previous slot cown " << cown
              << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << "prev slot " << prev_slot
              << " behaviour " << body << " prev slot blocked "
              << (int)prev_slot->blocked << " prev slot read only "
              << prev_slot->is_read_only() << " prev slot next read only "
              << prev_slot->is_next_slot_read_only() << Logging::endl;
            if (
              !prev_slot->is_read_only() ||
              prev_slot->state.compare_exchange_strong(
                state_true_none, state_true_reader, std::memory_order_acq_rel))
            {
              Logging::cout()
                << curr_slot
                << " Previous slot is a writer or pending reader cown " << cown
                << " read_ref_count " << cown->read_ref_count.count
                << " next_writer " << cown->next_writer << " last_slot "
                << cown->last_slot << " " << "prev slot " << prev_slot
                << " behaviour " << body << " prev slot blocked "
                << (int)prev_slot->blocked << " prev slot read only "
                << prev_slot->is_read_only() << " prev slot next read only "
                << prev_slot->is_next_slot_read_only() << Logging::endl;
              yield();
              prev_slot->set_next_slot(curr_slot);
              Logging::cout()
                << curr_slot << " Reader Set next of previous slot cown "
                << cown << " prev slot " << prev_slot << " behaviour "
                << body << Logging::endl;
              yield();
              continue;
            }
            else
            {
              yield();
              bool first_reader = cown->read_ref_count.add_read();
              Logging::cout()
                << curr_slot << " Reader got the cown " << cown
                << " read_ref_count " << cown->read_ref_count.count
                << " next_writer " << cown->next_writer << " last_slot "
                << cown->last_slot << " " << " behaviour " << body
                << Logging::endl;
              yield();
              prev_slot->set_next_slot(curr_slot);
              Logging::cout()
                << curr_slot << " Reader Set next of previous slot cown "
                << cown << " prev slot " << prev_slot << " behaviour "
                << body << Logging::endl;
              yield();
              curr_slot->blocked = false;
              ec[std::get<0>(indexes[first_chain_index])]++;
              if (first_reader)
              {
                Logging::cout()
                  << "Acquiring reference count for first reader on cown "
                  << cown << Logging::endl;
                Cown::acquire(cown);
              }
              continue;
            }
          }
        }
        else
        {
          auto prev_slot =
            cown->last_slot.exchange(curr_slot, std::memory_order_acq_rel);
          yield();
          if (prev_slot == nullptr)
          {
            cown->next_writer = curr_slot;
            yield();
            if (transfer_count)

            {
              Logging::cout()
                << "Releasing writer transferred count " << transfer_count
                << " -1 for first in queue cown " << cown << Logging::endl;
              // Release transfer_count - 1 times, we needed one as we woke up
              // the cown, but the rest were not required.
              for (int j = 0; j < transfer_count - 1; j++)
                Cown::release(ThreadAlloc::get(), cown);
            }
            else

            {
              Logging::cout() << "Acquiring writer reference count on cown "
                              << cown << Logging::endl;
              // We didn't have any RCs passed in, so we need to acquire one.
              Cown::acquire(cown);
            }

            if (
              !cown->read_ref_count.any_reader() &&
              cown->next_writer.exchange(nullptr, std::memory_order_acq_rel) ==
                curr_slot)
            {
              yield();
              Logging::cout()
                << curr_slot << " Writer at head of queue and got the cown "
                << cown << " read_ref_count " << cown->read_ref_count.count
                << " next_writer " << cown->next_writer << " last_slot "
                << cown->last_slot << " " << " behaviour " << body
                << Logging::endl;
              curr_slot->blocked = false;
              ec[std::get<0>(indexes[first_chain_index])]++;
              yield();
              continue;
            }
            Logging::cout()
              << curr_slot << " Writer waiting for previous reader cown "
              << cown << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << " behaviour " << body
              << Logging::endl;
          }
          else
          {
            Logging::cout() << curr_slot << " Writer waiting for cown " << cown
                            << " read_ref_count " << cown->read_ref_count.count
                            << " next_writer " << cown->next_writer
                            << " last_slot " << cown->last_slot << " "
                            << " behaviour " << body << Logging::endl;
            prev_slot->set_next_slot_writer();
            yield();
            prev_slot->set_next_slot(curr_slot);
            Logging::cout()
              << curr_slot << " Writer Set next of previous slot cown " << cown
              << " prev slot " << prev_slot << " behaviour " << body
              << Logging::endl;
          }
        }

        yield();
      }

      // Second phase - Release phase.
      for (size_t i = 0; i < body_count; i++)
      {
        Logging::cout() << "Release phase for behaviour " << bodies[i]
                        << Logging::endl;
      }
      for (size_t i = 0; i < count; i++)
      {
        yield();
        auto slot = std::get<1>(indexes[i]);
        Logging::cout() << "Setting slot " << slot << " to ready"
                        << Logging::endl;
        if (slot->is_wait())
          slot->set_ready();
      }

      for (size_t i = 0; i < body_count; i++)
      {
        yield();
        bodies[i]->resolve(ec[i]);
      }
    }

    /**
     * @brief Release all slots in the behaviour.
     *
     * This is should be called when the behaviour has executed.
     */
    void release_all()
    {
      Logging::cout() << "Finished Behaviour " << this << Logging::endl;
      auto slots = get_slots();
      // Behaviour is done, we can resolve successors.
      for (size_t i = 0; i < count; i++)
      {
        slots[i].release();
      }
      Logging::cout() << "Finished Resolving successors " << this
                      << Logging::endl;
    }

    /**
     * Reset the behaviour to look like it has never been scheduled.
     */
    void reset()
    {
      // Reset status on slots.
      for (size_t i = 0; i < count; i++)
      {
        get_slots()[i].reset();
      }

      exec_count_down = count + 1;
    }
  };

  inline void Slot::release()
  {
    assert(!is_wait());

    // This slot represents a duplicate cown, so we can ignore releasing it.
    if (cown == nullptr)
    {
      Logging::cout() << "Duplicate cown slot " << this << Logging::endl;
      return;
    }

    if (is_read_only())
    {
      if (next_slot == nullptr)
      {
        auto slot_addr = this;
        if (cown->last_slot.compare_exchange_strong(
              slot_addr, nullptr, std::memory_order_acq_rel))
        {
          Logging::cout() << this << " Reader releasing the cown " << cown
                          << " read_ref_count " << cown->read_ref_count.count
                          << " next_writer " << cown->next_writer
                          << " last_slot " << cown->last_slot << " "
                          << " behaviour " << get_behaviour() << Logging::endl;
          if (cown->read_ref_count.release_read())
          {
            Logging::cout()
              << this << "Last Reader releasing the cown " << cown
              << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << " behaviour " << get_behaviour()
              << Logging::endl;
            // Last reader
            auto w =
              cown->next_writer.exchange(nullptr, std::memory_order_acq_rel);
            if (w != nullptr)
            {
              Logging::cout()
                << this << " Last Reader waking up next writer cown " << cown
                << " read_ref_count " << cown->read_ref_count.count
                << " next_writer " << cown->next_writer << " last_slot "
                << cown->last_slot << " " << " writer " << w << " behaviour "
                << get_behaviour() << Logging::endl;
              w->blocked = false;
              if (w->is_ready())
              {
                yield();
                while (w->is_ready())
                {
                  Systematic::yield_until([w]() { return !(w->is_ready()); });
                  Aal::pause();
                }
              }
              assert(w->is_behaviour());
              w->get_behaviour()->resolve();
            }
            yield();

            // Release cown as this will be set by the new thread joining the
            // queue.
            Logging::cout()
              << this << " CAS success No more work for cown " << cown
              << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << " behaviour " << get_behaviour()
              << Logging::endl;
            shared::release(ThreadAlloc::get(), cown);
          }
          yield();
          // Release cown as this will be set by the new thread joining the
          // queue.
          Logging::cout() << this << " Last reader No more work for cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " next_writer "
                          << cown->next_writer << " last_slot "
                          << cown->last_slot << " " << " behaviour "
                          << get_behaviour() << Logging::endl;
          shared::release(ThreadAlloc::get(), cown);
          return;
        }
        Logging::cout() << this
                        << " Reader another thread extending the chain cown "
                        << cown << " read_ref_count "
                        << cown->read_ref_count.count << " next_writer "
                        << cown->next_writer << " last_slot " << cown->last_slot
                        << " " << " behaviour " << get_behaviour()
                        << Logging::endl;
        // If we failed, then the another thread is extending the chain
        while (next_slot == nullptr)
        {
          Systematic::yield_until([this]() { return (next_slot != nullptr); });
          Aal::pause();
        }
      }

      if (is_next_slot_writer())
        cown->next_writer = next_slot;

      Logging::cout() << this << " Reader releasing the cown " << cown
                      << " read_ref_count " << cown->read_ref_count.count
                      << " next_writer " << cown->next_writer << " last_slot "
                      << cown->last_slot << " " << " behaviour "
                      << get_behaviour() << Logging::endl;

      if (cown->read_ref_count.release_read())
      {
        // Last reader
        yield();
        auto w = cown->next_writer.load();
        if (
          w != nullptr && !cown->read_ref_count.any_reader() &&
          cown->next_writer.compare_exchange_strong(
            w, nullptr, std::memory_order_acq_rel))
        {
          Logging::cout() << this << " Last Reader waking up next writer cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " next_writer "
                          << cown->next_writer << " last_slot "
                          << cown->last_slot << " " << " writer " << w
                          << " behaviour " << get_behaviour() << Logging::endl;
          w->blocked = false;
          if (w->is_ready())
          {
            yield();
            while (w->is_ready())
            {
              Systematic::yield_until([w]() { return !(w->is_ready()); });
              Aal::pause();
            }
          }
          assert(w->is_behaviour());
          w->get_behaviour()->resolve();
        }

        Logging::cout() << this << " Last reader releasing cown " << cown
                        << " read_ref_count " << cown->read_ref_count.count
                        << " next_writer " << cown->next_writer << " last_slot "
                        << cown->last_slot << " " << " behaviour "
                        << get_behaviour() << Logging::endl;
        shared::release(ThreadAlloc::get(), cown);
      }
    }
    else
    {
      if (next_slot == NULL)
      {
        auto slot_addr = this;
        if (cown->last_slot.compare_exchange_strong(
              slot_addr, nullptr, std::memory_order_acq_rel))
        {
          yield();
          Logging::cout() << this
                          << " No more work Last writer releasing the cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " next_writer "
                          << cown->next_writer << " last_slot "
                          << cown->last_slot << " " << " behaviour "
                          << get_behaviour() << Logging::endl;
          shared::release(ThreadAlloc::get(), cown);
          return;
        }
        // If we failed, then the another thread is extending the chain
        while (next_slot == nullptr)
        {
          Systematic::yield_until([this]() { return (next_slot != nullptr); });
          Aal::pause();
        }
      }

      if (next_slot->is_read_only())
      {
#define NUM_CORES 18 // TODO: Fix this to (number of thread)
#define WORK_BATCH_COUNT 5

        std::array<Slot*, NUM_CORES> reader_queue_head;
        std::array<Slot*, NUM_CORES> reader_queue_tail;
        std::array<int, NUM_CORES> reader_queue_size;
        for (int i = 0; i < NUM_CORES; i++)
        {
          reader_queue_head[i] = nullptr;
          reader_queue_tail[i] = nullptr;
          reader_queue_size[i] = 0;
        }

        int pos = 0;
        int index = 0;

        bool first_reader =
          cown->read_ref_count.add_read(2); // Hold extra rcount
        yield();
        Logging::cout() << this << " Writer waking up next reader cown " << cown
                        << " read_ref_count " << cown->read_ref_count.count
                        << " next_writer " << cown->next_writer << " last_slot "
                        << cown->last_slot << " " << " next slot " << next_slot
                        << " behaviour " << get_behaviour() << Logging::endl;
        assert(first_reader);
        Logging::cout() << "Acquiring reference count for first reader on cown "
                        << cown << Logging::endl;
        yield();
        Cown::acquire(cown);
        yield();
        next_slot->blocked = false;

        if (next_slot->is_ready())
        {
          yield();
          while (next_slot->is_ready())
          {
            Systematic::yield_until(
              [this]() { return !(next_slot->is_ready()); });
            Aal::pause();
          }
        }
        assert(next_slot->is_behaviour());
        if(next_slot->get_behaviour()->resolve(1, true, false)) {
          reader_queue_head[pos] = next_slot;
          reader_queue_tail[pos] = next_slot;
        } 
        index++;

        auto curr_slot = next_slot;
        while (curr_slot->is_next_slot_read_only())
        {
          yield();
          while (curr_slot->next_slot == nullptr)
          {
            Systematic::yield_until(
              [curr_slot]() { return (curr_slot->next_slot != nullptr); });
            Aal::pause();
          }
          auto reader = curr_slot->next_slot;
          yield();
          reader->blocked = false;
          Logging::cout() << this << " Writer loop waking up next reader cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " next_writer "
                          << cown->next_writer << " last_slot "
                          << cown->last_slot << " " << " Next slot "
                          << curr_slot->next_slot << " behaviour "
                          << curr_slot->next_slot->get_behaviour()
                          << Logging::endl;
          yield();
          if (reader->is_ready())
          {
            yield();
            while (reader->is_ready())
            {
              Systematic::yield_until(
                [curr_slot]() { return !(curr_slot->next_slot->is_ready()); });
              Aal::pause();
            }
          }
          assert(reader->is_behaviour());

          // if (index % WORK_BATCH_COUNT == 0)
          //   pos = (pos + 1) % NUM_CORES;

          // if (reader_queue_size[pos] == WORK_BATCH_COUNT)
          // {
          //   yield();
          //   cown->read_ref_count.add_read(reader_queue_size[pos]);
          //   Logging::cout()
          //     << this << " Writer loop scheduling readers cown " << cown
          //     << " read_ref_count " << cown->read_ref_count.count
          //     << " first reader " << reader_queue_head[pos] << " last reader "
          //     << reader_queue_tail[pos] << " num readers "
          //     << reader_queue_size[pos] << " last_slot " << cown->last_slot
          //     << Logging::endl;
          //   yield();
          //   if (reader_queue_head[pos] == reader_queue_tail[pos])
          //     Scheduler::schedule(
          //       reader_queue_head[pos]->get_behaviour()->as_work());
          //   else
          //     Scheduler::schedule_many(
          //       reader_queue_head[pos]->get_behaviour()->as_work(),
          //       reader_queue_tail[pos]->get_behaviour()->as_work(),
          //       reader_queue_size[pos]);
          //   yield();
          //   reader_queue_size[pos] = 0;
          //   reader_queue_head[pos] = nullptr;
          //   reader_queue_tail[pos] = nullptr;
          // }

          if(next_slot->get_behaviour()->resolve(1, true, false)) {
            if (reader_queue_head[pos] == nullptr)
            {
              reader_queue_head[pos] = reader;
              reader_queue_tail[pos] = reader;
            }
            else
            {
              yield();
              reader_queue_tail[pos]
                ->get_behaviour()
                ->as_work()
                ->next_in_queue.store(
                  reader->get_behaviour()->as_work(), std::memory_order_release);
              reader_queue_tail[pos] = reader;
            }
          }

          reader_queue_size[pos]++; // Don't count the first one outside the
                                    // loop, otherwise rcount will be wrong
          index++;

          yield();
          curr_slot = reader;
        }

        if (reader_queue_size[pos] > 0 || index == 1)
        {
          cown->read_ref_count.add_read(reader_queue_size[pos]);
          Logging::cout() << this << " Writer loop scheduling readers cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " first reader "
                          << reader_queue_head[pos] << " last reader "
                          << reader_queue_tail[pos] << " num readers "
                          << reader_queue_size[pos] << " last_slot "
                          << cown->last_slot << Logging::endl;
          yield();
          if (reader_queue_head[pos] == reader_queue_tail[pos])
            Scheduler::schedule(
              reader_queue_head[pos]->get_behaviour()->as_work());
          else
            Scheduler::schedule_many(
              reader_queue_head[pos]->get_behaviour()->as_work(),
              reader_queue_tail[pos]->get_behaviour()->as_work(),
              reader_queue_size[pos]);
        }

        // Release extra rcount when traversal is done
        if (cown->read_ref_count.release_read())
        {
          // Last reader
          yield();
          auto w = cown->next_writer.load();
          if (
            w != nullptr && !cown->read_ref_count.any_reader() &&
            cown->next_writer.compare_exchange_strong(
              w, nullptr, std::memory_order_acq_rel))
          {
            Logging::cout()
              << this << " Writer being last Reader waking up next writer cown "
              << cown << " read_ref_count " << cown->read_ref_count.count
              << " next_writer " << cown->next_writer << " last_slot "
              << cown->last_slot << " " << " writer " << w << " behaviour "
              << get_behaviour() << Logging::endl;
            w->blocked = false;
            if (w->is_ready())
            {
              yield();
              while (w->is_ready())
              {
                Systematic::yield_until([w]() { return !(w->is_ready()); });
                Aal::pause();
              }
            }
            assert(w->is_behaviour());
            w->get_behaviour()->resolve();
          }

          Logging::cout() << this << " Writer being last reader releasing cown "
                          << cown << " read_ref_count "
                          << cown->read_ref_count.count << " next_writer "
                          << cown->next_writer << " last_slot "
                          << cown->last_slot << " " << " behaviour "
                          << get_behaviour() << Logging::endl;
          shared::release(ThreadAlloc::get(), cown);
        }
        // cown->read_ref_count.release_read();

        // cown->read_ref_count.add_read(index - 1);

        // thread_local long max_next_pending_readers = 0;
        // thread_local long sum_next_pending_readers = 0;
        // thread_local long count_next_pending_readers = 0;

        // sum_next_pending_readers += index;
        // count_next_pending_readers += 1;

        // if (max_next_pending_readers < index)
        // {
        //   max_next_pending_readers = index;
        //   printf(
        //     "Pending readers size Max: %ld Average: %lf Count: %ld\n",
        //     max_next_pending_readers,
        //     (sum_next_pending_readers * 1.0 / count_next_pending_readers),
        //     count_next_pending_readers);
        // }

        // for (int i = 0; i < NUM_CORES; i++)
        // {
        //   if (reader_queue_head[i] == nullptr)
        //     break;

        //   if (reader_queue_head[i] == reader_queue_tail[i])
        //     Scheduler::schedule(
        //       reader_queue_head[i]->get_behaviour()->as_work());
        //   else
        //     Scheduler::schedule_many(
        //       reader_queue_head[i]->get_behaviour()->as_work(),
        //       reader_queue_tail[i]->get_behaviour()->as_work());
        // }
      }
      else
      {
        Logging::cout() << this << " Writer waking up next writer cown " << cown
                        << " read_ref_count " << cown->read_ref_count.count
                        << " next_writer " << cown->next_writer << " last_slot "
                        << cown->last_slot << " " << " next slot " << next_slot
                        << " behaviour " << get_behaviour() << Logging::endl;
        // Writer waking up next writer cown
        next_slot->blocked = false;
        yield();
        if (next_slot->is_ready())
        {
          while (next_slot->is_ready())
          {
            Systematic::yield_until(
              [this]() { return !(next_slot->is_ready()); });
            Aal::pause();
          }
        }
        assert(next_slot->is_behaviour());
        next_slot->get_behaviour()->resolve();
      }
    }
  }
} // namespace verona::rt
