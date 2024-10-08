// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
namespace noticeboard_primitive_weak
{
  struct Writer : public VCown<Writer>
  {
  public:
    Noticeboard<int> box_0;
    Noticeboard<int> box_1;

    Writer(int x_0, int x_1) : box_0{x_0}, box_1{x_1}
    {
#ifdef USE_SYSTEMATIC_TESTING_WEAK_NOTICEBOARDS
      register_noticeboard(&box_0);
      register_noticeboard(&box_1);
#endif
    }
  };

  struct WriterLoop
  {
    Writer* writer;
    WriterLoop(Writer* writer) : writer(writer) {}

    void operator()()
    {
      auto x_0 = 1;
      auto x_1 = 2;

      writer->box_0.update(x_0);
      writer->box_1.update(x_1);
    }
  };

  struct Reader : public VCown<Reader>
  {
  public:
    Noticeboard<int>* box_0;
    Noticeboard<int>* box_1;

    Reader(Noticeboard<int>* box_0_, Noticeboard<int>* box_1_)
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
      auto x_1 = reader->box_1->peek();
      auto x_0 = reader->box_0->peek();

      // expected assertion failure; write to x_1 was picked up before x_0
      // if (x_1 == 2) {
      //   check(x_0 == 1);
      // }

      UNUSED(x_0);
      UNUSED(x_1);

      Cown::release(g_reader);
      Cown::release(g_writer);
    }
  };

  void run_test()
  {
    auto x_0 = 0;
    auto x_1 = 1;

    g_writer = new Writer(x_0, x_1);
    g_reader = new Reader(&g_writer->box_0, &g_writer->box_1);

    schedule_lambda(g_reader, ReaderLoop(g_reader));
    schedule_lambda(g_writer, WriterLoop(g_writer));
  }
}
