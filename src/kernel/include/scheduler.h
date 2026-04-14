#ifndef TERMOB_SCHEDULER_H
#define TERMOB_SCHEDULER_H

#include <stdint.h>

typedef void (*scheduler_task_entry_t)(void* context);

typedef struct {
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
    uint32_t eip;
} scheduler_context_t;

typedef enum {
    SCHEDULER_TASK_IDLE = 0,
    SCHEDULER_TASK_READY = 1,
    SCHEDULER_TASK_RUNNING = 2,
    SCHEDULER_TASK_EXITED = 3
} scheduler_task_state_t;

typedef struct scheduler_task scheduler_task_t;

struct scheduler_task {
    uint32_t task_id;
    const char* name;
    scheduler_task_entry_t entry;
    void* context;
    scheduler_task_state_t state;
    uint32_t timeslice_ticks;
    uint32_t slice_ticks;
    uint32_t total_ticks;
    uint32_t dispatch_count;
    uint32_t yield_count;
    uint32_t exit_count;
    uint32_t stack_size_bytes;
    uint8_t* stack_base;
    scheduler_context_t cpu_context;
    uint8_t is_idle;
    scheduler_task_t* next;
};

void scheduler_init(void);
int scheduler_is_initialized(void);
scheduler_task_t* scheduler_create_kernel_thread(const char* name,
                                                 scheduler_task_entry_t entry,
                                                 void* context,
                                                 uint32_t stack_size_bytes);
scheduler_task_t* scheduler_create_kernel_task(const char* name,
                                              scheduler_task_entry_t entry,
                                              void* context);
void scheduler_handle_tick(void);
void scheduler_dispatch(void);
void scheduler_yield(void);
int scheduler_should_yield(void);
int scheduler_is_running_in_task(void);

const scheduler_task_t* scheduler_current_task(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_ready_task_count(void);
uint32_t scheduler_tick_count(void);
uint32_t scheduler_timeslice_ticks(void);
uint32_t scheduler_dispatch_count(void);
uint32_t scheduler_yield_total_count(void);
void scheduler_dump_to_terminal(void);

#endif
