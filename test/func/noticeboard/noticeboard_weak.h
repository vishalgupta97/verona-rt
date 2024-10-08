// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
namespace noticeboard_weak
{
  struct C : public V<C>
  {
  public:
    int x = 0;

    C(int x_) : x(x_) {}
  };

  struct Writer : public VCown<Writer>
  {
  public:
    Noticeboard<Object*> box_0;
    Noticeboard<Object*> box_1;

    Writer(Object* c_0, Object* c_1) : box_0{c_0}, box_1{c_1}
    {
#ifdef USE_SYSTEMATIC_TESTING_WEAK_NOTICEBOARDS
      register_noticeboard(&box_0);
      register_noticeboard(&box_1);
#endif
    }

    void trace(ObjectStack& fields) const
    {
      box_0.trace(fields);
      box_1.trace(fields);
    }
  };

  struct WriterLoop
  {
    Writer* writer;
    WriterLoop(Writer* writer) : writer(writer) {}

    void operator()()
    {
      auto c_0 = new (RegionType::Trace) C(1);
      freeze(c_0);
      auto c_1 = new (RegionType::Trace) C(2);
      freeze(c_1);

      writer->box_0.update(c_0);
      writer->box_1.update(c_1);
    }
  };

  struct Reader : public VCown<Reader>
  {
  public:
    Noticeboard<Object*>* box_0;
    Noticeboard<Object*>* box_1;

    Reader(Noticeboard<Object*>* box_0_, Noticeboard<Object*>* box_1_)
    : box_0{box_0_}, box_1{box_1_}
    {}
  };

  Reader* g_reader = nullptr;
  Writer* g_writer = nullptr;

  struct ReaderLoop
  {
    Reader* reader;
    ReaderLoop(Reader* reader) : reader(reader) {}

    void operator()()
    {
      auto c_1 = (C*)reader->box_1->peek();
      auto c_0 = (C*)reader->box_0->peek();

      // expected assertion failure; write to c_1 was picked up before c_0
      // if (c_1->x == 2) {
      //   check(c_0->x == 1);
      // }

      // out of scope
      Immutable::release(c_0);
      Immutable::release(c_1);

      Cown::release(g_reader);
      Cown::release(g_writer);
    }
  };

  void run_test()
  {
    auto c_0 = new (RegionType::Trace) C(0);
    auto c_1 = new (RegionType::Trace) C(1);
    freeze(c_0);
    freeze(c_1);

    g_writer = new Writer(c_0, c_1);
    g_reader = new Reader(&g_writer->box_0, &g_writer->box_1);

    schedule_lambda(g_reader, ReaderLoop(g_reader));
    schedule_lambda(g_writer, WriterLoop(g_writer));
  }
}
