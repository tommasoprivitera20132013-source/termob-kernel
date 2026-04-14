#include "../include/scheduler.h"

#include "../include/heap.h"
#include "../include/klog.h"
#include "../include/serial.h"
#include "../include/terminal.h"

#define SCHEDULER_DEFAULT_TIMESLICE_TICKS 5U
#define SCHEDULER_DEFAULT_STACK_BYTES     4096U
#define SCHEDULER_MIN_STACK_BYTES         2048U
#define SCHEDULER_STACK_ALIGNMENT         16U
#define SCHEDULER_STACK_GUARD_MAGIC       0x54424B54U

extern void scheduler_context_switch(scheduler_context_t* from, scheduler_context_t* to);

static scheduler_task_t scheduler_idle_task;
static scheduler_context_t scheduler_kernel_context;
static scheduler_task_t* scheduler_task_list_head;
static scheduler_task_t* scheduler_task_list_tail;
static scheduler_task_t* scheduler_current_task_ptr;
static scheduler_task_t* scheduler_last_task_ptr;
static uint32_t scheduler_next_task_id;
static uint32_t scheduler_total_tasks;
static uint32_t scheduler_tick_counter;
static uint32_t scheduler_total_dispatches;
static uint32_t scheduler_total_yields;
static uint8_t scheduler_ready;
static uint8_t scheduler_reschedule_pending;
static uint8_t scheduler_in_thread_context;

static void scheduler_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static uint32_t scheduler_align_up_u32(uint32_t value, uint32_t alignment) {
    if (alignment == 0U) {
        return value;
    }

    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t scheduler_align_down_u32(uint32_t value, uint32_t alignment) {
    if (alignment == 0U) {
        return value;
    }

    return value & ~(alignment - 1U);
}

static void scheduler_link_task(scheduler_task_t* task) {
    if (scheduler_task_list_head == 0) {
        scheduler_task_list_head = task;
        scheduler_task_list_tail = task;
        task->next = 0;
        return;
    }

    scheduler_task_list_tail->next = task;
    scheduler_task_list_tail = task;
    task->next = 0;
}

static scheduler_task_t* scheduler_next_task_after(scheduler_task_t* task) {
    if (task == 0 || task->next == 0) {
        return scheduler_task_list_head;
    }

    return task->next;
}

static const uint32_t* scheduler_stack_guard_slot_const(const scheduler_task_t* task) {
    if (task == 0 || task->stack_base == 0 || task->stack_size_bytes < sizeof(uint32_t)) {
        return 0;
    }

    return (const uint32_t*)task->stack_base;
}

static int scheduler_stack_guard_ok(const scheduler_task_t* task) {
    const uint32_t* guard;

    if (task == 0 || task->is_idle) {
        return 1;
    }

    guard = scheduler_stack_guard_slot_const(task);
    if (guard == 0) {
        return 0;
    }

    return *guard == SCHEDULER_STACK_GUARD_MAGIC;
}

static void scheduler_mark_task_corrupt(scheduler_task_t* task, const char* message) {
    if (task == 0 || task->state == SCHEDULER_TASK_EXITED) {
        return;
    }

    task->state = SCHEDULER_TASK_EXITED;
    scheduler_reschedule_pending = 1U;
    scheduler_log_event(message);
}

static int scheduler_has_ready_non_idle_task(void) {
    scheduler_task_t* task;
    uint32_t count;

    task = scheduler_task_list_head;
    count = 0U;
    while (task != 0 && count < scheduler_total_tasks) {
        if (!task->is_idle && task->state == SCHEDULER_TASK_READY) {
            return 1;
        }

        task = task->next;
        count++;
    }

    return 0;
}

static scheduler_task_t* scheduler_pick_next_task(void) {
    scheduler_task_t* task;
    uint32_t count;
    int prefer_non_idle;

    if (scheduler_task_list_head == 0) {
        return &scheduler_idle_task;
    }

    prefer_non_idle = scheduler_has_ready_non_idle_task();
    task = scheduler_next_task_after(scheduler_last_task_ptr);
    count = 0U;

    while (task != 0 && count < scheduler_total_tasks) {
        if (task->state == SCHEDULER_TASK_READY) {
            if (!prefer_non_idle || !task->is_idle) {
                return task;
            }
        }

        task = scheduler_next_task_after(task);
        count++;
    }

    return &scheduler_idle_task;
}

static const char* scheduler_task_state_name(const scheduler_task_t* task) {
    if (task == 0) {
        return "none";
    }

    switch (task->state) {
        case SCHEDULER_TASK_IDLE:
            return "idle";
        case SCHEDULER_TASK_READY:
            return "ready";
        case SCHEDULER_TASK_RUNNING:
            return "running";
        case SCHEDULER_TASK_EXITED:
            return "exited";
        default:
            return "unknown";
    }
}

static void scheduler_terminal_write_u32(uint32_t value) {
    char digits[10];
    int i;

    if (value == 0U) {
        terminal_write("0");
        return;
    }

    i = 0;
    while (value > 0U && i < 10) {
        digits[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        terminal_putchar(digits[--i]);
    }
}

static __attribute__((noreturn)) void scheduler_exit_current_task(void) {
    scheduler_task_t* task;

    task = scheduler_current_task_ptr;
    if (task == 0 || task->is_idle) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    task->state = SCHEDULER_TASK_EXITED;
    task->slice_ticks = 0U;
    task->exit_count++;
    scheduler_reschedule_pending = 1U;
    scheduler_in_thread_context = 0U;
    scheduler_current_task_ptr = &scheduler_idle_task;
    scheduler_context_switch(&task->cpu_context, &scheduler_kernel_context);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static __attribute__((noreturn)) void scheduler_task_bootstrap(void) {
    scheduler_task_t* task;

    task = scheduler_current_task_ptr;
    if (task == 0 || task->entry == 0) {
        scheduler_log_event("TERMOB: scheduler bootstrap missing task entry");
        scheduler_exit_current_task();
    }

    task->entry(task->context);
    scheduler_exit_current_task();
}

static void scheduler_prepare_task_context(scheduler_task_t* task) {
    uint32_t stack_top;

    stack_top = (uint32_t)(task->stack_base + task->stack_size_bytes);
    stack_top = scheduler_align_down_u32(stack_top, SCHEDULER_STACK_ALIGNMENT);

    task->cpu_context.esp = stack_top;
    task->cpu_context.ebp = 0U;
    task->cpu_context.ebx = 0U;
    task->cpu_context.esi = 0U;
    task->cpu_context.edi = 0U;
    task->cpu_context.eflags = 0x202U;
    task->cpu_context.eip = (uint32_t)scheduler_task_bootstrap;
}

void scheduler_init(void) {
    scheduler_task_list_head = 0;
    scheduler_task_list_tail = 0;
    scheduler_current_task_ptr = 0;
    scheduler_last_task_ptr = 0;
    scheduler_next_task_id = 1U;
    scheduler_total_tasks = 0U;
    scheduler_tick_counter = 0U;
    scheduler_total_dispatches = 0U;
    scheduler_total_yields = 0U;
    scheduler_ready = 0U;
    scheduler_reschedule_pending = 0U;
    scheduler_in_thread_context = 0U;

    scheduler_idle_task.task_id = 0U;
    scheduler_idle_task.name = "idle";
    scheduler_idle_task.entry = 0;
    scheduler_idle_task.context = 0;
    scheduler_idle_task.state = SCHEDULER_TASK_READY;
    scheduler_idle_task.timeslice_ticks = SCHEDULER_DEFAULT_TIMESLICE_TICKS;
    scheduler_idle_task.slice_ticks = 0U;
    scheduler_idle_task.total_ticks = 0U;
    scheduler_idle_task.dispatch_count = 0U;
    scheduler_idle_task.yield_count = 0U;
    scheduler_idle_task.exit_count = 0U;
    scheduler_idle_task.stack_size_bytes = 0U;
    scheduler_idle_task.stack_base = 0;
    scheduler_idle_task.cpu_context.esp = 0U;
    scheduler_idle_task.cpu_context.ebp = 0U;
    scheduler_idle_task.cpu_context.ebx = 0U;
    scheduler_idle_task.cpu_context.esi = 0U;
    scheduler_idle_task.cpu_context.edi = 0U;
    scheduler_idle_task.cpu_context.eflags = 0x202U;
    scheduler_idle_task.cpu_context.eip = 0U;
    scheduler_idle_task.is_idle = 1U;
    scheduler_idle_task.next = 0;

    scheduler_link_task(&scheduler_idle_task);
    scheduler_total_tasks = 1U;
    scheduler_current_task_ptr = &scheduler_idle_task;
    scheduler_last_task_ptr = &scheduler_idle_task;
    scheduler_ready = 1U;
    scheduler_reschedule_pending = 1U;

    scheduler_log_event("TERMOB: scheduler foundation online");
}

int scheduler_is_initialized(void) {
    return scheduler_ready != 0U;
}

scheduler_task_t* scheduler_create_kernel_thread(const char* name,
                                                 scheduler_task_entry_t entry,
                                                 void* context,
                                                 uint32_t stack_size_bytes) {
    scheduler_task_t* task;
    uint32_t alloc_size;

    if (!scheduler_ready || name == 0 || entry == 0) {
        return 0;
    }

    if (stack_size_bytes == 0U) {
        stack_size_bytes = SCHEDULER_DEFAULT_STACK_BYTES;
    }

    alloc_size = scheduler_align_up_u32(stack_size_bytes, SCHEDULER_STACK_ALIGNMENT);
    if (alloc_size < SCHEDULER_MIN_STACK_BYTES) {
        alloc_size = SCHEDULER_MIN_STACK_BYTES;
    }

    task = (scheduler_task_t*)kcalloc(1U, sizeof(scheduler_task_t));
    if (task == 0) {
        scheduler_log_event("TERMOB: scheduler task allocation failed");
        return 0;
    }

    task->stack_base = (uint8_t*)kcalloc(1U, alloc_size);
    if (task->stack_base == 0) {
        scheduler_log_event("TERMOB: scheduler task stack allocation failed");
        kfree(task);
        return 0;
    }

    *((uint32_t*)task->stack_base) = SCHEDULER_STACK_GUARD_MAGIC;

    task->task_id = scheduler_next_task_id++;
    task->name = name;
    task->entry = entry;
    task->context = context;
    task->state = SCHEDULER_TASK_READY;
    task->timeslice_ticks = SCHEDULER_DEFAULT_TIMESLICE_TICKS;
    task->slice_ticks = 0U;
    task->total_ticks = 0U;
    task->dispatch_count = 0U;
    task->yield_count = 0U;
    task->exit_count = 0U;
    task->stack_size_bytes = alloc_size;
    task->is_idle = 0U;
    task->next = 0;

    scheduler_prepare_task_context(task);
    scheduler_link_task(task);
    scheduler_total_tasks++;
    scheduler_reschedule_pending = 1U;
    scheduler_log_event("TERMOB: scheduler task registered");
    return task;
}

scheduler_task_t* scheduler_create_kernel_task(const char* name,
                                              scheduler_task_entry_t entry,
                                              void* context) {
    return scheduler_create_kernel_thread(name, entry, context, SCHEDULER_DEFAULT_STACK_BYTES);
}

void scheduler_handle_tick(void) {
    if (!scheduler_ready || scheduler_current_task_ptr == 0) {
        return;
    }

    scheduler_tick_counter++;
    scheduler_current_task_ptr->total_ticks++;
    scheduler_current_task_ptr->slice_ticks++;

    if (scheduler_current_task_ptr->timeslice_ticks != 0U &&
        scheduler_current_task_ptr->slice_ticks >= scheduler_current_task_ptr->timeslice_ticks) {
        scheduler_current_task_ptr->slice_ticks = 0U;
        scheduler_reschedule_pending = 1U;
    }
}

void scheduler_dispatch(void) {
    scheduler_task_t* next_task;

    if (!scheduler_ready || scheduler_in_thread_context || !scheduler_reschedule_pending) {
        return;
    }

    next_task = scheduler_pick_next_task();
    if (next_task == 0 || next_task->is_idle) {
        scheduler_current_task_ptr = &scheduler_idle_task;
        scheduler_reschedule_pending = 0U;
        return;
    }

    if (!scheduler_stack_guard_ok(next_task)) {
        scheduler_mark_task_corrupt(next_task, "TERMOB: scheduler stack guard corrupted");
        return;
    }

    scheduler_last_task_ptr = next_task;
    scheduler_current_task_ptr = next_task;
    scheduler_current_task_ptr->state = SCHEDULER_TASK_RUNNING;
    scheduler_current_task_ptr->slice_ticks = 0U;
    scheduler_current_task_ptr->dispatch_count++;
    scheduler_total_dispatches++;
    scheduler_reschedule_pending = 0U;
    scheduler_in_thread_context = 1U;

    scheduler_context_switch(&scheduler_kernel_context, &scheduler_current_task_ptr->cpu_context);

    scheduler_in_thread_context = 0U;
    scheduler_current_task_ptr = &scheduler_idle_task;

    if (scheduler_has_ready_non_idle_task()) {
        scheduler_reschedule_pending = 1U;
    }
}

void scheduler_yield(void) {
    scheduler_task_t* task;

    if (!scheduler_ready || !scheduler_in_thread_context) {
        return;
    }

    task = scheduler_current_task_ptr;
    if (task == 0 || task->is_idle) {
        return;
    }

    if (!scheduler_stack_guard_ok(task)) {
        scheduler_mark_task_corrupt(task, "TERMOB: scheduler stack guard corrupted");
        scheduler_exit_current_task();
    }

    task->yield_count++;
    scheduler_total_yields++;
    task->state = SCHEDULER_TASK_READY;
    task->slice_ticks = 0U;
    scheduler_reschedule_pending = 1U;
    scheduler_in_thread_context = 0U;
    scheduler_current_task_ptr = &scheduler_idle_task;
    scheduler_context_switch(&task->cpu_context, &scheduler_kernel_context);

    scheduler_in_thread_context = 1U;
    scheduler_current_task_ptr = task;
    if (task->state != SCHEDULER_TASK_EXITED) {
        task->state = SCHEDULER_TASK_RUNNING;
    }
}

int scheduler_should_yield(void) {
    return scheduler_in_thread_context != 0U && scheduler_reschedule_pending != 0U;
}

int scheduler_is_running_in_task(void) {
    return scheduler_in_thread_context != 0U;
}

const scheduler_task_t* scheduler_current_task(void) {
    return scheduler_current_task_ptr;
}

uint32_t scheduler_task_count(void) {
    return scheduler_total_tasks;
}

uint32_t scheduler_ready_task_count(void) {
    scheduler_task_t* task;
    uint32_t ready_count;
    uint32_t count;

    if (!scheduler_ready) {
        return 0U;
    }

    ready_count = 0U;
    task = scheduler_task_list_head;
    count = 0U;
    while (task != 0 && count < scheduler_total_tasks) {
        if (task->state == SCHEDULER_TASK_READY || task->state == SCHEDULER_TASK_RUNNING) {
            ready_count++;
        }

        task = task->next;
        count++;
    }

    return ready_count;
}

uint32_t scheduler_tick_count(void) {
    return scheduler_tick_counter;
}

uint32_t scheduler_timeslice_ticks(void) {
    return SCHEDULER_DEFAULT_TIMESLICE_TICKS;
}

uint32_t scheduler_dispatch_count(void) {
    return scheduler_total_dispatches;
}

uint32_t scheduler_yield_total_count(void) {
    return scheduler_total_yields;
}

void scheduler_dump_to_terminal(void) {
    scheduler_task_t* task;
    uint32_t count;

    terminal_writeline("Scheduler foundation:");
    terminal_write("  State      : ");
    terminal_writeline(scheduler_ready ? "ready" : "offline");
    terminal_write("  Mode       : ");
    terminal_writeline("cooperative kernel threads");
    terminal_write("  In task    : ");
    terminal_writeline(scheduler_in_thread_context ? "yes" : "no");
    terminal_write("  Current    : ");
    terminal_writeline(scheduler_current_task_ptr != 0 ? scheduler_current_task_ptr->name : "none");
    terminal_write("  Tasks      : ");
    scheduler_terminal_write_u32(scheduler_total_tasks);
    terminal_writeline(" total");
    terminal_write("  Ready      : ");
    scheduler_terminal_write_u32(scheduler_ready_task_count());
    terminal_writeline(" runnable");
    terminal_write("  Timeslice  : ");
    scheduler_terminal_write_u32(SCHEDULER_DEFAULT_TIMESLICE_TICKS);
    terminal_writeline(" ticks");
    terminal_write("  Dispatches : ");
    scheduler_terminal_write_u32(scheduler_total_dispatches);
    terminal_putchar('\n');
    terminal_write("  Yields     : ");
    scheduler_terminal_write_u32(scheduler_total_yields);
    terminal_putchar('\n');

    task = scheduler_task_list_head;
    count = 0U;
    while (task != 0 && count < scheduler_total_tasks) {
        terminal_write("  - ");
        terminal_write(task->name);
        terminal_write("  [");
        terminal_write(scheduler_task_state_name(task));
        terminal_write("]  dispatch ");
        scheduler_terminal_write_u32(task->dispatch_count);
        terminal_write("  yield ");
        scheduler_terminal_write_u32(task->yield_count);
        if (!task->is_idle) {
            terminal_write("  stack ");
            scheduler_terminal_write_u32(task->stack_size_bytes);
        }
        if (!scheduler_stack_guard_ok(task)) {
            terminal_write("  guard BAD");
        }
        terminal_putchar('\n');

        task = task->next;
        count++;
    }
}
