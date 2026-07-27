#include "crash_test.hpp"

#if QP_DEV_TOOLS

#include "logger.hpp"

#include <windows.h>

#include <cstdint>

namespace qp
{
namespace
{

// Deliberately deep call chain so crash stacks look realistic (not one frame).
struct CrashLadder
{
    __declspec(noinline) void Rung0_Entry() { Rung1_Prepare(); }

    __declspec(noinline) void Rung1_Prepare()
    {
        volatile int sink = 1;
        sink += Rung2_Validate(sink);
        (void)sink;
    }

    __declspec(noinline) int Rung2_Validate(int x) { return Rung3_Dispatch(x + 1); }

    __declspec(noinline) int Rung3_Dispatch(int x)
    {
        Rung4_Worker worker;
        return worker.Rung4_Run(x);
    }

    struct Rung4_Worker
    {
        __declspec(noinline) int Rung4_Run(int x) { return Rung5_Helper::Rung5_Compute(this, x); }
    };

    struct Rung5_Helper
    {
        __declspec(noinline) static int Rung5_Compute(Rung4_Worker* self, int x)
        {
            (void)self;
            return Rung6_Sink(x * 2);
        }
    };

    __declspec(noinline) static int Rung6_Sink(int x)
    {
        Rung7_Boom(static_cast<unsigned>(x));
        return 0;
    }

    __declspec(noinline) static void Rung7_Boom(unsigned seed)
    {
        (void)seed;
        // Noncontinuable AV — SEH filter logs a multi-frame stack then WER may run.
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
};

struct CrashThreadArgs
{
    unsigned delayMs = 2000;
};

DWORD WINAPI DeferredCrashThreadMain(void* param)
{
    CrashThreadArgs args{};
    if (param)
    {
        args = *static_cast<CrashThreadArgs*>(param);
        delete static_cast<CrashThreadArgs*>(param);
    }

    QP_LOG_WARN(L"crash-test: worker thread sleeping %u ms before intentional fault", args.delayMs);
    Sleep(args.delayMs);
    QP_LOG_ERROR(L"crash-test: waking — descending CrashLadder (expect CRASH in log)");

    CrashLadder ladder;
    ladder.Rung0_Entry();

    return 0;
}

} // namespace

void StartDeferredCrashTest(unsigned delayMs)
{
    auto* args = new CrashThreadArgs{delayMs};
    const HANDLE th = CreateThread(nullptr, 0, &DeferredCrashThreadMain, args, 0, nullptr);
    if (!th)
    {
        delete args;
        QP_LOG_ERROR(L"crash-test: CreateThread failed (%lu)", GetLastError());
        return;
    }
    // Detach — process will die when the thread faults (or be torn down on Exit).
    CloseHandle(th);
    QP_LOG_INFO(L"crash-test: deferred crash armed (delayMs=%u)", delayMs);
}

} // namespace qp

#endif // QP_DEV_TOOLS
