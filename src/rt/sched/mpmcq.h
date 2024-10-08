// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "epoch.h"

namespace verona::rt
{
  /**
   * Multiple Producer Multiple Consumer Queue.
   *
   * This queue forms the primary scheduler queue for each thread to
   * schedule cowns.
   *
   * The queue has two ends.
   *
   *   - the back end can be used by multiple thread using
   *     `enqueue` to add elements to the queue in a FIFO way wrt to `dequeue`.
   *   - the front end can be used by multiple threads to `dequeue` elements
   *     and `enqueue_front` elements. `enqueue_front` behaves in a LIFO way wrt
   *     to `dequeue`.
   *
   * The queue uses an intrusive list in the elements of the queue.  (For
   * Verona this is the Cowns). To make this memory safe and ABA safe we use
   * two mechanisms.
   *
   *   - ABA protection from snmalloc - this will use LL/SC or Double-word
   *     compare and swap.  This ensures that the same element can be added
   *     to the queue multiple times without leading to ABA issues.
   *   - Memory safety, the underlying elements of the queue my also be
   *     deallocated however, if this occurs, then we could potentially access
   *     decommitted memory with the optimistic concurrency. To protect against
   *     this we use an epoch mechanism, that is, elements may only
   *     deallocated, if sufficient epochs have passed since it was last in
   *     the queue.
   *
   * Using two mechanisms means that we can have intrusive `next` fields,
   * which gives zero allocation scheduling, but don't have to wait for the
   * epoch to advance to reschedule.
   *
   * The queue also has a notion of a token. This is used to determine once
   * the queue has been flushed through.  The client can check if the value
   * popped is a token.  This is used to monitor how quickly this queue is
   * completed, and then can be used for fairness scheduling.
   *
   * The queue doesn't know which work elements are the token, but the non-empty
   * nature needs it to exist otherwise, we can't reach a quicent state.
   */
  template<class T>
  class MPMCQ
  {
  private:
    friend T;
    static constexpr uintptr_t BIT = 1;
    // Multi-threaded enqueue end of the "queue"
    // modified using exchange.
    std::atomic<T*> back;
    // Multi-threaded end of the "queue" requires ABA protection.
    // Used for work stealing and posting new work from another thread.
    snmalloc::ABA<T> front;

  public:
    explicit MPMCQ(T* token)
    {
      assert(token);
      token->next_in_queue = nullptr;
      back = token;
      front.init(token);
    }

    /**
     * Enqueue a node, this is not linearisable with respect
     * to dequeue.  That is a dequeue may not see this enqueue
     * once we return, due to other enqueues that have not
     * completed.
     */
    void enqueue(T* node)
    {
      node->next_in_queue.store(nullptr, std::memory_order_relaxed);
      auto b = back.exchange(node, std::memory_order_seq_cst);
      // The element we are writing into must have made its next pointer null
      // before exchanging into the structure, as the element cannot be removed
      // if it has a null next pointer, we know the write is safe.
      assert(b->next_in_queue == nullptr);
      b->next_in_queue.store(node, std::memory_order_release);
    }

    void enqueue_front(T* node)
    {
      auto cmp = front.read();

      do
      {
        node->next_in_queue.store(cmp.ptr(), std::memory_order_relaxed);
      } while (!cmp.store_conditional(node));
      // TODO: Add this into the ABA protection.
      // Requires snmalloc PR to add store_conditional to take a memory_order.
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    /**
     * Take an element from the queue.
     * This may spuriosly fail and surrounding code should be prepared for that.
     */
    T* dequeue()
    {
      T* next;
      T* fnt;

      // Hold epoch to ensure that the value read from `front` cannot be
      // deallocated during this operation.  This must occur before read of
      // front.
      Epoch e;
      uint64_t epoch = e.get_local_epoch_epoch();

      auto cmp = front.read();
      do
      {
        fnt = cmp.ptr();
        // This operation is memory safe due to holding the epoch.
        next = fnt->next_in_queue.load(std::memory_order_acquire);

        // If next is nullptr, then this is most likely the next entry has not
        // been enqueued.  Due to the non-linearisable nature, there may be
        // completed enqueues that are not visible.  This means we can get
        // spurious failures and the context must cope with this. It may also
        // return nullptr, due to an ABA where next is observed to be nullptr
        // after the element has been removed.  This spurious nullptr could be
        // removed by adding ABA protection, however, as the context must
        // already deal with spurious failure, we do not bother with that check.
        if (next == nullptr)
          return nullptr;
      } while (!cmp.store_conditional(next));

      assert(epoch != T::NO_EPOCH_SET);

      fnt->epoch_when_popped = epoch;

      return fnt;
    }

    // The callers are expected to guarantee no one is attempting to access the
    // queue concurrently.
    void destroy()
    {
      assert(front.peek() == back);
      auto b = back.load();
      assert(b->next_in_queue == nullptr);

      heap::dealloc(b);
    }

    /**
     * Returns true if nothing older than this call is in the queue.
     *
     * This is not linearisable, so a linearisable is_empty check is not
     * possible.
     *
     * We use a happens-before semantics to explain its behaviour. If this
     * returns true, then all enqueues that 'happened-before' this call, have
     * been dequeued by the time this call returns. Parallel enqueues may or
     * may-not be observed, so it may not be empty when it returns.
     *
     * The precise semantics of this are required for `unpause`/`pause`.  If
     * during `pause`, we observe `nothing_old` to be true, then anything that
     * is in the queue after this function returns must have been added not
     * happens-before this call. As any addition must call `unpause` afterwards,
     * we know that we can't read `nothing_old` as true, and then go to sleep
     * with stuff still in our queue, as the `unpause` is guaranteed to wake us
     * up.
     */
    bool nothing_old()
    {
      auto local_back = back.load(std::memory_order_acquire);
      // Check if last element is the token cown.
      // Last element should be the token work, but we don't need to check that
      // as something else will have two things if we don't have the token.

      // Check first element is the last, hence if true, then all elements
      // in the queue have been enqueued since this call started.
      return local_back == front.peek();
    }
  };
} // namespace verona::rt
