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

spin_lock(cown) {
    while(CAS(cown.lock, false, true) != false);
}

spin_unlock(cown) {
    cown.lock = false;
}

within_2pl_acquire_phase(cown, curr_slot) {
    curr_slot->set_behaviour(first_body);

    if(curr_slot->is_read_only()) {
        spin_lock(cown);
        prev_slot = cown->last_slot;
        cown->last_slot = curr_slot;
        cown->reader_phase_counter++;
        spin_unlock(cown);
        if(prev_slot == NULL) {
            first_reader = cown->read_ref_count.atomic_inc();
            curr_slot->blocked = false;
            dec_dependency(curr_slot);
            Cown::acquire(cown);
            if(first_reader)
                Cown::acquire(cown);
        } else {
            uint16_t state_true_none = STATE_TRUE_NONE; // [Blocked, type]
            uint16_t state_true_reader = STATE_TRUE_READER; // [Blocked, type]
            if(!prev_slot->is_read_only() || CAS(prev_slot->state, state_true_none, state_true_reader)) {
                prev_slot->set_next_slot(curr_slot); 
            } else {
                first_reader = cown->read_ref_count.atomic_inc();
                prev_slot->set_next_slot(curr_slot); 
                curr_slot->blocked = false;
                dec_dependency(curr_slot);
                if(first_reader)
                    Cown::acquire(cown);
            }
        }
    } else {
        spin_lock(cown);
        prev_slot cown->last_slot;
        cown->last_slot = curr_slot;
        curr_slot->reader_phase_counter = cown->reader_phase_counter;
        cown->reader_phase_counter = 0;
        spin_unlock(cown);
        if(prev_slot == NULL) {
            cown->next_writer = curr_slot;
            Cown::acquire(cown);
            if(cown->read_ref_count == 0 && CAS(cown->next_writer, curr_slot, NULL) == curr_slot) {
                curr_slot->blocked = false;
                dec_dependency(curr_slot);
            }
        } else {
            prev_slot->set_next_slot_writer();
            CAS(cown->next_writer, NULL, curr_slot);
            prev_slot->set_next_slot(curr_slot);
            curr_slot->set_prev_slot(prev_slot);
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
                    writer = XCHG(cown->next_writer, NULL);
                    if(writer != NULL) {
                        writer->blocked = false;
                        while (writer->is_ready());
                        writer->get_behaviour()->resolve();
                    }
                    // Release cown as this will be set by the new thread joining the queue.
                    Cown::release(cown);
                }
                Cown::release(cown);
                return;
            }
            // If we failed, then the another thread is extending the chain
            while (curr_slot->next_slot == NULL);
        }

        if(curr_slot->is_next_slot_writer()) 
            cown->next_writer = next_slot;

        if(cown->read_ref_count.release_read()) {
            // Last reader
            writer = cown->next_writer;
            if(writer != NULL && cown->read_ref_count == 0 && CAS(cown->next_writer, writer, NULL)) {
                writer->blocked = false;
                while(writer->is_ready());
                dec_dependency(writer);
            }
            Cown::release(cown);
        }
    } else {
        if(curr_slot->next_slot == NULL) {
            if(CAS(cown->last_slot, curr_slot, NULL)) {
                Cown::release(cown);
                return;
            }
            // If we failed, then the another thread is extending the chain
            while (curr_slot->next_slot == NULL);
        }

        if(curr_slot->next_slot->is_read_only()) {
            std::vector<Slot*> next_pending_readers;
            cown->read_ref_count.atomic_inc();
            Cown::acquire(cown); // For first reader
            next_pending_readers.push_back(next_slot);

            curr_slot = next_slot;
            while(curr_slot->is_next_slot_read_only()) {
                while (curr_slot->next_slot == NULL); 
                cown->read_ref_count.add_read();
                curr_slot->next_slot->blocked = false;
                next_pending_readers.push_back(curr_slot->next_slot);
                curr_slot = curr_slot->next_slot;
            }

            for(reader: next_pending_readers) {
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