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
            slot_type successor_class = NONE;
        }
    }
}

struct lock {
    struct slot tail;
    int reader_count;
    struct slot next_writer;
};

within_2pl_acquire_phase(cown, curr_slot) {
    if(curr_slot.is_read_only()) {
        prev_slot = XCHG(cown->tail, curr_slot);
        if(prev_slot == NULL) {
            cown->read_count++;
            curr_slot->blocked = false;
            dec_dependency(curr_slot);
            acquire_cown();
        } else {
            if(prev_slot->class = WRITER || CAS(prev_slot->state, [true, NONE], [true, READER])) {
                prev_slot->next = curr_slot;
                repeat while blocked();
            } else {
                cown->read_count++;
                prev_slot->next = curr_slot;
                curr_slot->blocked = false;
                dec_dependency(curr_slot);
            }
        }
        if(curr_slot->successor_class = READER) {
            repeat while curr_slot->next == NULL;
            cown->read_count++; // Wakeup next reader and increment count for it.
            curr_slot->next->blocked = false;
            dec_dependency(curr_slot->next);
        }
    } else {
        prev_slot = XCHG(cown->tail, curr_slot);
        if(prev_slot == NULL) {
            cown->next_writer = curr_slot;
            if(cown->reader_count == 0 && XCHG(cown->next_writer, NULL) == curr_slot) {
                blocked = false;
                dec_dependency(curr_slot);
                acquire_cown();
            }
        } else {
            prev_slot->successor_class = WRITER;
            prev_slot->next = curr_slot;
        }
        repeat while blocked();
    }
}
```

## Unlocking

```c
within_release_slot(curr_slot) {
    if(curr_slot.type == READER) {
        if(curr_slot.next == NULL) {
            if(CAS(cown->tail, curr_slot, NULL) == curr_slot) {
                if(atomic_dec(cown->read_count) == 0) {
                    // Last reader
                    w = XCHG(cown->next_writer, NULL);
                    if(w != NULL) {
                        w->blocked = false;
                        dec_dependency(w);
                    } else
                        release_cown();
                }
                return;
            }
            repeat while curr_slot.next() == NULL;
        }    
        if(curr_slot.successor_class == WRITER)
            cown.next_writer = curr_slot.next;
        if(atomic_dec(cown->read_count) == 0) {
            // Last reader
            w = XCHG(cown->next_writer, NULL);
            if(w != NULL) {
                w->blocked = false;
                dec_dependency(w);
            } else
                release_cown();
        }
    } else {
        if(curr_slot.next == NULL) {
            if(CAS(cown->tail, curr_slot, NULL) == curr_slot) {
                release_cown();
                return;
            }
            repeat while curr_slot.next() == NULL;
        }
        if(curr_slot.next.class == READER)
            cown->read_count++;
        curr_slot.next.blocked = false;
        dec_dependency(curr_slot.next);
    }
}
```