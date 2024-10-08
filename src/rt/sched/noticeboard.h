// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../debug/logging.h"
#include "../ds/forward_list.h"
#include "../region/region.h"
#include "../sched/epoch.h"
#include "../sched/schedulerthread.h"

#include <queue>

namespace verona::rt
{
  template<typename T>
  class Noticeboard : public BaseNoticeboard
  {
  public:
    Noticeboard(T content_)
    {
      is_fundamental = std::is_fundamental_v<T>;
      put(content_);
    }

    void trace(ObjectStack& st) const
    {
      if constexpr (!std::is_fundamental_v<T>)
      {
#ifdef USE_SYSTEMATIC_TESTING_WEAK_NOTICEBOARDS
        for (auto p : update_buffer)
        {
          st.push((T)p);
        }
#endif
        auto p = get<T>();
        if (p)
          st.push(p);
      }
      else
      {
        UNUSED(st);
      }
    }

    // NOTE: the rc of new_o is not incremented
    void update(T new_o)
    {
      if constexpr (!std::is_fundamental_v<T>)
      {
        assert(new_o->debug_is_immutable());
      }
#ifdef USE_SYSTEMATIC_TESTING_WEAK_NOTICEBOARDS
      update_buffer_push(new_o);
      flush_some();
      yield();
#else
      if constexpr (!std::is_fundamental_v<T>)
      {
        auto local_content = get<T>();
        Logging::cout() << "Updating noticeboard " << this << " old value "
                        << local_content << " new value " << new_o
                        << Logging::endl;

        put(new_o);
        yield();
        Epoch e;
        e.dec_in_epoch(local_content);
        Logging::cout() << "Dec ref from noticeboard update" << local_content
                        << Logging::endl;
      }
      else
      {
        put(new_o);
      }
      yield();
#endif
    }

    T peek()
    {
      if constexpr (std::is_fundamental_v<T>)
      {
        return get<T>();
      }
      else
      {
        T local_content;
        {
          // only protect incref with epoch
          Epoch e;
          local_content = get<T>();
          yield();
          Logging::cout() << "Inc ref from noticeboard peek" << local_content
                          << Logging::endl;
          local_content->incref();
        }
        return local_content;
      }
    }
  };
} // namespace verona::rt
