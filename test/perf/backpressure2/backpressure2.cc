// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

/**
 * This test involves many small sets of `Sender` cowns repeatedly selecting a
 * small random subset of `Receiver` cowns and sending them a message.
 *
 * A correct backpressure system should ensure that the receivers do not
 * experience runaway message queue growth with constantly changing
 * relationships between senders and receivers. All receivers must also maintain
 * high load signals despite constantly participating in multi-messages with
 * different sets of cowns.
 */

#include "debug/log.h"
#include "test/opt.h"
#include "verona.h"

#include <chrono>

using namespace verona::rt;
using timer = std::chrono::high_resolution_clock;

struct Receiver : public VCown<Receiver>
{
  size_t msgs = 0;
  timer::time_point prev = timer::now();
};

struct Receive
{
  Receiver** receivers;
  size_t receiver_count;

  Receive(Receiver** receivers_, size_t receiver_count_)
  : receivers(receivers_), receiver_count(receiver_count_)
  {}

  Receive(Receive&& o) noexcept
  {
    receiver_count = o.receiver_count;
    receivers = o.receivers;

    o.receivers = nullptr;
  }

  ~Receive()
  {
    if (receivers)
      heap::dealloc(receivers, receiver_count * sizeof(Receiver*));
  }

  void trace(ObjectStack& st) const
  {
    for (size_t i = 0; i < receiver_count; i++)
      st.push(receivers[i]);
  }

  void operator()()
  {
    for (size_t i = 0; i < receiver_count; i++)
    {
      auto& r = *receivers[i];
      r.msgs++;
      const auto now = timer::now();
      const auto t =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - r.prev);
      if (t < std::chrono::milliseconds(1000))
        return;

      logger::cout() << &r << " received " << r.msgs << " messages"
                     << std::endl;
      r.prev = now;
      r.msgs = 0;
    }
  }
};

struct Sender : public VCown<Sender>
{
  timer::time_point start = timer::now();
  timer::duration duration;
  std::vector<Receiver*>& receivers;
  PRNG<> rng;

  Sender(
    timer::duration duration_, std::vector<Receiver*>& receivers_, size_t seed1)
  : duration(duration_), receivers(receivers_), rng(seed1)
  {}

  void trace(ObjectStack& st) const
  {
    for (auto* r : receivers)
      st.push(r);
  }
};

struct Send
{
  Sender* s;

  Send(Sender* s_) : s(s_) {}

  void operator()()
  {
    const size_t max_receivers = (std::min)(s->receivers.size(), (size_t)3);
    const size_t receiver_count = (s->rng.next() % max_receivers) + 1;

    auto** receivers =
      (Receiver**)heap::alloc(receiver_count * sizeof(Receiver*));

    for (size_t i = 0; i < receiver_count;)
    {
      receivers[i] = s->receivers[s->rng.next() % s->receivers.size()];
      if (std::find(receivers, &receivers[i], receivers[i]) == &receivers[i])
        i++;
    }

    schedule_lambda(
      receiver_count, (Cown**)receivers, Receive(receivers, receiver_count));

    if ((timer::now() - s->start) < s->duration)
      schedule_lambda(s, Send(s));
  }
};

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  const auto seed = opt.is<size_t>("--seed", 5489);
  const auto cores = opt.is<size_t>("--cores", 4);
  const auto senders = opt.is<size_t>("--senders", 100);
  const auto receivers = opt.is<size_t>("--receivers", 10);
  const auto duration = opt.is<size_t>("--duration", 10'000);
  logger::cout() << "cores: " << cores << ", senders: " << senders
                 << ", receivers: " << receivers << ", duration: " << duration
                 << "ms" << std::endl;

#if defined(USE_FLIGHT_RECORDER) || defined(CI_BUILD)
  Logging::enable_crash_logging();
#endif

#ifdef USE_SYSTEMATIC_TESTING
  Logging::enable_logging();
  Systematic::set_seed(seed);
#endif
  Scheduler::set_detect_leaks(true);
  auto& sched = Scheduler::get();
  sched.set_fair(true);
  sched.init(cores);

  static std::vector<Receiver*> receiver_set;
  for (size_t i = 0; i < receivers; i++)
    receiver_set.push_back(new Receiver);

  verona::rt::PRNG<> rng(seed);
  for (size_t i = 0; i < senders; i++)
  {
    for (auto* r : receiver_set)
      Cown::acquire(r);

    auto* s =
      new Sender(std::chrono::milliseconds(duration), receiver_set, rng.next());
    schedule_lambda<YesTransfer>(s, Send(s));
  }

  for (auto* r : receiver_set)
    Cown::release(r);

  sched.run();
}
