#include <iostream>
#include <Windows.h>
#include <cstdint>


namespace
{
constexpr size_t TestMaxAlloc     = 1024 * 1024 * 200;
constexpr size_t HeapReserveSize  = 1024 * 1024 * 150;
constexpr size_t AllocAtIteration = 300;
}


namespace Rtl
{
namespace
{ //#include <ntifs.h>
typedef NTSTATUS(*PRTL_HEAP_COMMIT_ROUTINE)(IN PVOID Base, IN OUT PVOID *CommitAddress, IN OUT PSIZE_T CommitSize);

typedef struct _RTL_HEAP_PARAMETERS {
    ULONG Length;
    SIZE_T SegmentReserve;
    SIZE_T SegmentCommit;
    SIZE_T DeCommitFreeBlockThreshold;
    SIZE_T DeCommitTotalFreeThreshold;
    SIZE_T MaximumAllocationSize;
    SIZE_T VirtualMemoryThreshold;
    SIZE_T InitialCommit;
    SIZE_T InitialReserve;
    PRTL_HEAP_COMMIT_ROUTINE CommitRoutine;
    SIZE_T Reserved[2];
} RTL_HEAP_PARAMETERS, *PRTL_HEAP_PARAMETERS;

using RtlCreateHeapPtr = PVOID(NTAPI *)(
    ULONG                Flags,
    PVOID                HeapBase,
    SIZE_T               ReserveSize,
    SIZE_T               CommitSize,
    PVOID                Lock,
    PRTL_HEAP_PARAMETERS Parameters
);

using RtlDestroyHeapPtr = PVOID(NTAPI *)(
    PVOID HeapHandle
);

#define HEAP_CLASS_0                    0x00000000      // process heap
#define HEAP_CLASS_1                    0x00001000      // private heap
#define HEAP_CLASS_2                    0x00002000      // Kernel Heap
#define HEAP_CLASS_3                    0x00003000      // GDI heap
#define HEAP_CLASS_4                    0x00004000      // User heap
#define HEAP_CLASS_5                    0x00005000      // Console heap
#define HEAP_CLASS_6                    0x00006000      // User Desktop heap
#define HEAP_CLASS_7                    0x00007000      // Csrss Shared heap
#define HEAP_CLASS_8                    0x00008000      // Csr Port heap
#define HEAP_CLASS_MASK                 0x0000F000

#define HEAP_MAXIMUM_TAG                0x0FFF              // winnt
#define HEAP_GLOBAL_TAG                 0x0800
#define HEAP_PSEUDO_TAG_FLAG            0x8000              // winnt
#define HEAP_TAG_SHIFT                  18                  // winnt
#define HEAP_TAG_MASK                  (HEAP_MAXIMUM_TAG << HEAP_TAG_SHIFT)

#define HEAP_CREATE_VALID_MASK         (HEAP_NO_SERIALIZE |             \
                                        HEAP_GROWABLE |                 \
                                        HEAP_GENERATE_EXCEPTIONS |      \
                                        HEAP_ZERO_MEMORY |              \
                                        HEAP_REALLOC_IN_PLACE_ONLY |    \
                                        HEAP_TAIL_CHECKING_ENABLED |    \
                                        HEAP_FREE_CHECKING_ENABLED |    \
                                        HEAP_DISABLE_COALESCE_ON_FREE | \
                                        HEAP_CLASS_MASK |               \
                                        HEAP_CREATE_ALIGN_16 |          \
                                        HEAP_CREATE_ENABLE_TRACING |    \
                                        HEAP_CREATE_ENABLE_EXECUTE)
}

HANDLE HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T reserveSize, bool growable)
{
    const auto ntdll = GetModuleHandleW(L"ntdll");
    if (!ntdll)
        throw GetLastError();

    auto createHeap = reinterpret_cast<RtlCreateHeapPtr>(GetProcAddress(ntdll, "RtlCreateHeap"));
    if (!createHeap)
        throw GetLastError();

    flOptions &= HEAP_CREATE_VALID_MASK;
    flOptions |= HEAP_CLASS_1;
    if (growable)
        flOptions |= HEAP_GROWABLE;

    auto heap = (HANDLE)createHeap(flOptions, nullptr, reserveSize, dwInitialSize, nullptr, nullptr);
    if (!heap)
        throw GetLastError();

    return reinterpret_cast<HANDLE>(heap);
}

BOOL NTAPI HeapDestroy(HANDLE heap)
{
    const auto ntdll = GetModuleHandleW(L"ntdll");
    if (!ntdll)
        throw GetLastError();

    auto destroyHeap = reinterpret_cast<RtlDestroyHeapPtr>(GetProcAddress(ntdll, "RtlDestroyHeap"));
    if (!destroyHeap)
        throw GetLastError();

    destroyHeap(heap);
    return GetLastError() ? FALSE : TRUE;
}
}


namespace
{
struct HeapDestroyFunctor
{
    using DtorFuncPtr = BOOL(WINAPI *)(HANDLE);

    HeapDestroyFunctor(DtorFuncPtr dtor) : dtor(dtor) {}

    inline auto operator()(HANDLE heap)
    {
        return dtor(heap);
    }

private:
    const DtorFuncPtr dtor;
};

using ScopedHeapHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HeapDestroyFunctor>;
}


int main()
try
{
    ScopedHeapHandle heaps[] = {
        ScopedHeapHandle {
            HeapCreate(0, 0, 0),
            &HeapDestroy
        },

        ScopedHeapHandle {
            Rtl::HeapCreate(0, 0, HeapReserveSize, true),
            &Rtl::HeapDestroy
        },
    };

    std::cout << "Heaps:\n";
    for (const auto &heap : heaps)
        std::cout << "\t> HEAP: " << static_cast<const void*>(heap.get()) << "\n";

    std::cout << "All heaps created\nPress any key...\n";
    getchar();

    for (size_t sumSize = 0; sumSize < TestMaxAlloc; sumSize += AllocAtIteration)
        for (auto &heap : heaps)
            if (!HeapAlloc(heap.get(), 0, AllocAtIteration))
                throw std::runtime_error("allocation failed!");

    heaps[0].reset();
    std::cout << "1-st heap destroyed\nPress any key to exit...\n";
    getchar();

    return 0;
}
catch (const int &gle)
{
    std::cout << "Exception occurred: GLE: " << gle << "\n";
    return -1;
}
catch (const std::exception &ex)
{
    std::cout << "Exception occurred:\n" << ex.what() << "\n";
    return -1;
}
