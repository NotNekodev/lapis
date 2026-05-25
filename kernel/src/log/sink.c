#include <log/sink.h>

#include <util/spinlock.h>
#include <util/memory.h>

static int id_counter = 0;
log_sink_t *sinks_head = NULL;

spinlock_t output_lock = SPINLOCK_INIT("sink_output_lock");

int register_sink(log_sink_t *sink) { // returns the id of the registered sink, or -1 on failure
    if (sink == NULL || sink->write == NULL || sink->flush == NULL) {
        return -1;
    }

    sink->id = id_counter++;
    sink->next = NULL;

    while (sinks_head != NULL) {
        if (sinks_head->id == sink->id) {
            return -1; // son, how did you do this :sob:
        }

        if (sinks_head->next == NULL) {
            break;
        }

        sinks_head = sinks_head->next;
    }

    if (sinks_head == NULL) {
        sinks_head = sink;
    } else {
        sinks_head->next = sink;
    }

    return sink->id;
}

void unregister_sink(int id) {
    log_sink_t *current = sinks_head;
    log_sink_t *prev = NULL;

    while (current != NULL) {
        if (current->id == id) {
            if (prev == NULL) {
                sinks_head = current->next;
            } else {
                prev->next = current->next;
            }
            return;
        }
        prev = current;
        current = current->next;
    }
}

void log_to_sinks(const char *data, size_t len, int level) {
    spinlock_acquire(&output_lock);
    log_to_sinks_unlocked(data, len, level);
    spinlock_release(&output_lock);
}

void log_to_sinks_unlocked(const char *data, size_t len, int level) {
    log_sink_t *current = sinks_head;

    while (current != NULL) {
        if ((current->level_mask & (1 << level)) != 0) {
            current->write(data, len, level);
        }
        current = current->next;
    }
}

int get_id_by_name(const char *name) {
    log_sink_t *current = sinks_head;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current->id;
        }
        current = current->next;
    }

    return -1; // not found
}