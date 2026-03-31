#pragma once

#include "Task.hpp"
#include <vector>

class TaskNode {
    friend class TimingWheel;
    friend class Timer;
public:
    TaskNode(Task* t, size_t tm) : task(t), next(nullptr), prev(nullptr), time(tm) {}

private:
    Task* task;
    TaskNode* next, *prev;
    size_t time;
};

class TimingWheel {
    friend class Timer;
public:
    TimingWheel(size_t size, size_t interval) : size(size), interval(interval), current_slot(0) {
        slots = new TaskNode*[size];
        for (size_t i = 0; i < size; i++) {
            slots[i] = nullptr;
        }
    }

    ~TimingWheel() {
        for (size_t i = 0; i < size; i++) {
            TaskNode* curr = slots[i];
            while (curr) {
                TaskNode* next = curr->next;
                delete curr;
                curr = next;
            }
        }
        delete[] slots;
    }

    void insertTask(TaskNode* node, size_t slot_index) {
        slot_index = slot_index % size;
        node->next = slots[slot_index];
        node->prev = nullptr;
        if (slots[slot_index]) {
            slots[slot_index]->prev = node;
        }
        slots[slot_index] = node;
    }

    void removeTask(TaskNode* node) {
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            // Node is head, find which slot
            for (size_t i = 0; i < size; i++) {
                if (slots[i] == node) {
                    slots[i] = node->next;
                    break;
                }
            }
        }
        if (node->next) {
            node->next->prev = node->prev;
        }
    }

    TaskNode* getCurrentSlotTasks() {
        TaskNode* tasks = slots[current_slot];
        slots[current_slot] = nullptr;
        return tasks;
    }

    void tick() {
        current_slot = (current_slot + 1) % size;
    }

private:
    const size_t size, interval;
    size_t current_slot;
    TaskNode** slots;
};

class Timer {
public:
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    Timer() {
        second_wheel = new TimingWheel(60, 1);      // 60 slots, 1s interval
        minute_wheel = new TimingWheel(60, 60);     // 60 slots, 60s interval
        hour_wheel = new TimingWheel(24, 3600);     // 24 slots, 3600s interval
    }

    ~Timer() {
        delete second_wheel;
        delete minute_wheel;
        delete hour_wheel;
    }

    TaskNode* addTask(Task* task) {
        size_t time = task->getFirstInterval();
        TaskNode* node = new TaskNode(task, time);
        addTaskToWheel(node, time);
        return node;
    }

    void cancelTask(TaskNode *p) {
        if (!p) return;

        // Find and remove from appropriate wheel
        if (p->prev) {
            p->prev->next = p->next;
        } else {
            // p is head of some slot
            for (size_t i = 0; i < second_wheel->size; i++) {
                if (second_wheel->slots[i] == p) {
                    second_wheel->slots[i] = p->next;
                    if (p->next) p->next->prev = nullptr;
                    delete p;
                    return;
                }
            }
            for (size_t i = 0; i < minute_wheel->size; i++) {
                if (minute_wheel->slots[i] == p) {
                    minute_wheel->slots[i] = p->next;
                    if (p->next) p->next->prev = nullptr;
                    delete p;
                    return;
                }
            }
            for (size_t i = 0; i < hour_wheel->size; i++) {
                if (hour_wheel->slots[i] == p) {
                    hour_wheel->slots[i] = p->next;
                    if (p->next) p->next->prev = nullptr;
                    delete p;
                    return;
                }
            }
        }

        if (p->next) {
            p->next->prev = p->prev;
        }
        delete p;
    }

    std::vector<Task*> tick() {
        std::vector<Task*> result;

        // Tick second wheel first
        second_wheel->tick();

        // Check if we need to cascade from higher wheels
        bool minute_tick = (second_wheel->current_slot == 0);
        bool hour_tick = false;

        if (minute_tick) {
            minute_wheel->tick();
            hour_tick = (minute_wheel->current_slot == 0);
            if (hour_tick) {
                hour_wheel->tick();
            }
        }

        // Cascade from hour wheel if needed
        if (hour_tick) {
            TaskNode* curr = hour_wheel->getCurrentSlotTasks();
            while (curr) {
                TaskNode* next = curr->next;
                curr->next = curr->prev = nullptr;

                // Calculate remaining time
                size_t remaining_time = curr->time % hour_wheel->interval;
                curr->time = remaining_time;

                if (remaining_time == 0) {
                    // Execute immediately
                    result.push_back(curr->task);
                    size_t period = curr->task->getPeriod();
                    if (period > 0 && period <= 24 * 3600) {
                        curr->time = period;
                        addTaskToWheel(curr, period);
                    } else {
                        delete curr;
                    }
                } else {
                    addTaskToWheel(curr, remaining_time);
                }
                curr = next;
            }
        }

        // Cascade from minute wheel if needed
        if (minute_tick) {
            TaskNode* curr = minute_wheel->getCurrentSlotTasks();
            while (curr) {
                TaskNode* next = curr->next;
                curr->next = curr->prev = nullptr;

                // Calculate remaining time
                size_t remaining_time = curr->time % minute_wheel->interval;
                curr->time = remaining_time;

                if (remaining_time == 0) {
                    // Execute immediately
                    result.push_back(curr->task);
                    size_t period = curr->task->getPeriod();
                    if (period > 0 && period <= 24 * 3600) {
                        curr->time = period;
                        addTaskToWheel(curr, period);
                    } else {
                        delete curr;
                    }
                } else {
                    addTaskToWheel(curr, remaining_time);
                }
                curr = next;
            }
        }

        // Process current second slot
        TaskNode* curr = second_wheel->getCurrentSlotTasks();
        while (curr) {
            TaskNode* next = curr->next;
            result.push_back(curr->task);

            // Handle periodic tasks
            size_t period = curr->task->getPeriod();
            if (period > 0 && period <= 24 * 3600) {
                curr->next = curr->prev = nullptr;
                curr->time = period;
                addTaskToWheel(curr, period);
            } else {
                delete curr;
            }
            curr = next;
        }

        return result;
    }

private:
    TimingWheel* second_wheel;
    TimingWheel* minute_wheel;
    TimingWheel* hour_wheel;

    void addTaskToWheel(TaskNode* node, size_t time) {
        if (time == 0) {
            delete node;
            return;
        }

        // Determine which wheel to use based on time
        if (time <= second_wheel->size * second_wheel->interval) {
            // Add to second wheel
            size_t slot = (second_wheel->current_slot + time / second_wheel->interval) % second_wheel->size;
            second_wheel->insertTask(node, slot);
        } else if (time <= minute_wheel->size * minute_wheel->interval) {
            // Add to minute wheel
            size_t slot = time / minute_wheel->interval;
            minute_wheel->insertTask(node, slot);
        } else if (time <= hour_wheel->size * hour_wheel->interval) {
            // Add to hour wheel
            size_t slot = time / hour_wheel->interval;
            hour_wheel->insertTask(node, slot);
        } else {
            // Time exceeds all wheels
            delete node;
        }
    }
};
