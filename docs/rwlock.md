# Reader-Writer lock

## Locking

```c
within_2pl_acquire_phase(cown, curr_slot) {
    prev_slot = XCHG(cown->tail, curr_slot)

    if(prev_slot == NULL) {
        if(reader(curr_slot)) {
            cown->is_writer_at_head = false;
            cown->read_count++;
        }
        else {
            cown->is_writer_at_head = true;
        }
        dec_dependency();
    } else {
        if(reader(curr_slot)) {
            if(!cown->is_writer_at_head && !cown->writer_waiting) {
                cown->read_count++;
                dec_dependency();
            }
        } else {
            cown->writer_waiting = true;
        }
        prev_slot->next = curr_slot;
    }
}
```

## Unlocking

```c
within_release_slot(curr_slot) {
    if(curr_slot->no_writer_waiting) {
        cown->read_count--;
        return;
    }
    if(reader(curr_slot)) {
        cown->read_count--;
        if(cown->read_count == 0) {
            cown->is_writer_at_head = true;
            wakeup_next_behaviour(curr_slot);
        }
    } else {
        if(!reader(curr_slot->next)) {
            cown->is_writer_at_head = true;
            wakeup_next_behaviour(curr_slot);
        } else {
            cown->is_writer_at_head = false;
            vector pending_readers;
            next_slot = this;
            writer_slot = nullptr;

            while(true) {
                if(next_slot->is_ready()) {
                    if (CAS(cown->last_slot, next_slot, NULL) == next_slot) {
                        writer_slot = nullptr;
                        next_slot->no_writer_waiting = true;
                        cown->writer_waiting = false;
                        break;
                    }
                    assert(false); // TODO: Fix this to address case when the queue is extended while being traversed.
                    while (next_slot->is_ready());
                    break;
                }
                
                assert(next_slot->is_behaviour());
                if(next_slot->next->is_read_only())
                    pending_readers.push_back(next_slot);
                else {
                    writer_slot = next_slot;
                    break;
                }
                next_slot = next_slot->next;
            }

            for(auto reader: pending_readers) {
                cown->read_ref_count.add_read();
                wakeup_next_behaviour(reader);
                reader->next = writer_slot;
            }
        }
    }
}
```