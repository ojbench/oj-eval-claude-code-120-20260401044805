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

    bool advance() {
        current_slot = (current_slot + 1) % size;
        return current_slot == 0; // Return true if wrapped around
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

        // Advance second wheel first
        bool minute_tick = second_wheel->advance();

        // Process tasks at current second slot
        TaskNode* current = second_wheel->getCurrentSlotTasks();
        TaskNode* next_node = nullptr;

        while (current != nullptr) {
            next_node = current->next;
            result.push_back(current->task);

            // Reschedule periodic task
            size_t period = current->task->getPeriod();
            if (period > 0 && period <= 24 * 3600) {
                current->time = period;
                current->next = nullptr;
                current->prev = nullptr;
                addTaskToWheel(current, period);
            } else {
                delete current;
            }

            current = next_node;
        }

        // Handle minute wheel tick
        if (minute_tick) {
            bool hour_tick = minute_wheel->advance();
            cascadeTasks(minute_wheel, result);

            // Handle hour wheel tick
            if (hour_tick) {
                hour_wheel->advance();
                cascadeTasks(hour_wheel, result);
            }
        }

        return result;
    }

private:
    TimingWheel* second_wheel;
    TimingWheel* minute_wheel;
    TimingWheel* hour_wheel;

    void cascadeTasks(TimingWheel* wheel, std::vector<Task*>& result) {
        // Move tasks from higher wheel to lower wheel
        TaskNode* current = wheel->getCurrentSlotTasks();
        TaskNode* next_node = nullptr;

        while (current != nullptr) {
            next_node = current->next;

            // Adjust time based on the interval: task.time %= interval
            current->time = current->time % wheel->interval;
            current->next = nullptr;
            current->prev = nullptr;

            // If time is 0, task should execute immediately
            if (current->time == 0) {
                result.push_back(current->task);
                // Reschedule if periodic
                size_t period = current->task->getPeriod();
                if (period > 0 && period <= 24 * 3600) {
                    current->time = period;
                    addTaskToWheel(current, period);
                } else {
                    delete current;
                }
            } else {
                addTaskToWheel(current, current->time);
            }

            current = next_node;
        }
    }

    void addTaskToWheel(TaskNode* node, size_t time) {
        if (time == 0) {
            delete node;
            return;
        }

        node->time = time;

        // Determine which wheel to use based on time
        if (time / second_wheel->interval <= second_wheel->size) {
            // Add to second wheel
            size_t slot = (second_wheel->current_slot + time / second_wheel->interval) % second_wheel->size;
            second_wheel->insertTask(node, slot);
        } else if (time / minute_wheel->interval <= minute_wheel->size) {
            // Add to minute wheel - need to adjust for current position
            // The formula from the algorithm: adjusted_time = time + current_slot * interval
            size_t adjusted_time = time + second_wheel->current_slot * second_wheel->interval;
            node->time = adjusted_time;
            size_t slot = (adjusted_time / minute_wheel->interval) % minute_wheel->size;
            minute_wheel->insertTask(node, slot);
        } else if (time / hour_wheel->interval <= hour_wheel->size) {
            // Add to hour wheel
            size_t adjusted_time = time + second_wheel->current_slot * second_wheel->interval;
            node->time = adjusted_time;
            size_t slot = (adjusted_time / hour_wheel->interval) % hour_wheel->size;
            hour_wheel->insertTask(node, slot);
        } else {
            // Time exceeds all wheels
            delete node;
        }
    }
};
