#pragma once

#include <string>
#include <iostream>

class Task {
public:
    Task(std::string name, size_t time, size_t period): name(name), first_interval(time), period(period) {}
    void set() {}
    size_t getFirstInterval() const { return first_interval; }
    size_t getPeriod() const { return period; }
    void execute() { std::cout << "Task: " << name << " excuted" << std::endl; }
    static void incTime() {}
    static size_t getCnt() { return 0; }

private:
    std::string name;
    const size_t first_interval;
    const size_t period;
};
