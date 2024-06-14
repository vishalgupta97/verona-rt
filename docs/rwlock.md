# Reader-Writer lock

## Locking

```c
enum slot_type {
    NONE,
    READER,
    WRITER
};

struct slot {
    slot_type type; 
    struct slot next_slot = NULL;
    union {
        atomic_t state;
        struct {
            bool blocked = true;
            slot_type next_slot_type = NONE;
        }
    }
}

struct lock {
    struct slot tail;
    int reader_count;
    struct slot next_writer;
};

within_2pl_acquire_phase(cown, curr_slot) {
    auto curr_slot = last_slot;
    curr_slot->set_behaviour(first_body);

    if(curr_slot->is_read_only()) {
        prev_slot = XCHG(cown->last_slot, curr_slot);
        if(prev_slot == NULL) {
            cown->read_ref_count.atomic_inc();
            curr_slot->blocked = false;
            dec_dependency(curr_slot);
            Cown::acquire(cown);
        } else {
            uint16_t state_true_none = STATE_TRUE_NONE; // [Blocked, type]
            uint16_t state_true_reader = STATE_TRUE_READER; // [Blocked, type]
            if(!prev_slot->is_read_only() || CAS(prev_slot->state, state_true_none, state_true_reader)) {
                prev_slot->set_next_slot(curr_slot); 
            } else {
                cown->read_ref_count.atomic_inc();
                prev_slot->set_next_slot(curr_slot); 
                curr_slot->blocked = false;
                dec_dependency(curr_slot);
            }
        }
    } else {
        prev_slot = XCHG(cown->last_slot, curr_slot);
        if(prev_slot == NULL) {
            cown->next_writer = curr_slot;
            if(cown->read_ref_count == 0 && XCHG(cown->next_writer, nullptr) == curr_slot) {
                curr_slot->blocked = false;
                dec_dependency(curr_slot);
                Cown::acquire(cown);
            }
        } else {
            prev_slot->set_next_slot_writer();
            prev_slot->set_next_slot(curr_slot);
        }
    }
}
```

## Unlocking

```c
within_release_slot(curr_slot) {

    if(curr_slot->is_read_only()) {
        if(curr_slot->next_slot == NULL) {
            if(CAS(cown->last_slot, curr_slot, NULL)) {
                if(cown->read_ref_count.release_read()) {
                    // Last reader
                    auto w = XCHG(cown->next_writer, NULL);
                    if(w != nullptr) {
                    w->blocked = false;
                    while (w->is_ready());
                    w->get_behaviour()->resolve();
                    }
                    // Release cown as this will be set by the new thread joining the queue.
                    shared::release(ThreadAlloc::get(), cown);
                }
                return;
            }
            // If we failed, then the another thread is extending the chain
            while (curr_slot->next_slot == NULL);
        }

        if(curr_slot->is_next_slot_writer()) 
            cown->next_writer = next_slot;

        if(cown->read_ref_count.release_read()) {
            // Last reader
            auto w = cown->next_writer;
            if(w != NULL && cown->read_ref_count == 0 && CAS(cown->next_writer, w, NULL)) {
                w->blocked = false;
                while(w->is_ready());
                dec_dependency(w);
            } else {
                shared::release(ThreadAlloc::get(), cown);
            }
        }
    } else {
        if(curr_slot->next_slot == NULL) {
            if(CAS(cown->last_slot, curr_slot, NULL)) {
                shared::release(ThreadAlloc::get(), cown);
                return;
            }
            // If we failed, then the another thread is extending the chain
            while (curr_slot->next_slot == nullptr);
        }

        std::vector<Slot*> next_pending_readers;
        if(curr_slot->next_slot->is_read_only()) {
            cown->read_ref_count.atomic_inc();
            next_pending_readers.push_back(next_slot);

            auto curr_slot = next_slot;
            while(curr_slot->is_next_slot_read_only()) {
                while (curr_slot->next_slot == NULL); 
                cown->read_ref_count.add_read();
                next_pending_readers.push_back(curr_slot->next_slot);
                curr_slot = curr_slot->next_slot;
            }

            for(auto reader: next_pending_readers) {
                reader->blocked = false;
                while(reader->is_ready()); 
                dec_dependency(reader);
            }

        } else {
            curr_slot->next_slot->blocked = false;
            while(curr_slot->next_slot->is_ready()); 
            dec_dependency(curr_slot->next_slot)
        }
    }
}
```