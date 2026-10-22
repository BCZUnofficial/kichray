#pragma once
#include <mutex>

struct TestDevice
{
    unsigned int dwTimeGlobal;
};
extern TestDevice Device;
void Msg(const char* format, ...);

class xrCriticalSection
{
public:
    void Enter() { mutex.lock(); }
    void Leave() { mutex.unlock(); }
private:
    std::mutex mutex;
};
