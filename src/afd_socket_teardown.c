#include <ntddk.h>

// Represents the relevant AFD_SOCKET structure conceptually
typedef struct _AFD_SOCKET {
    PVOID ContextBuffer;
    KSPIN_LOCK StateLock;
    LONG ReferenceCount;
} AFD_SOCKET, *PAFD_SOCKET;

#define AFD_POOL_TAG 'dfA ' // 'Afd '

extern VOID AfdFreeSocketObject(PAFD_SOCKET Socket);

/**
 * Patched socket teardown routine.
 * Safely nullifies pointers inside a spinlock to prevent UAF race conditions.
 */
NTSTATUS AfdCloseSocket(PAFD_SOCKET Socket) {
    KIRQL OldIrql;
    
    // 1. Acquire spinlock to ensure mutually exclusive access to the socket state
    KeAcquireSpinLock(&Socket->StateLock, &OldIrql);

    if (Socket->ContextBuffer) {
        // 2. Safely capture the pointer and immediately NULL the struct member
        PVOID DanglingBuffer = Socket->ContextBuffer;
        Socket->ContextBuffer = NULL; 
        
        // 3. Free the captured pointer safely without leaving a dangling reference
        ExFreePoolWithTag(DanglingBuffer, AFD_POOL_TAG);
    }

    // 4. Safe reference counting: Only free the parent object if no other threads 
    // hold a reference to this socket.
    LONG RemainingRefs = InterlockedDecrement(&Socket->ReferenceCount);
    
    // Release the lock before freeing the parent object to prevent deadlocks
    KeReleaseSpinLock(&Socket->StateLock, OldIrql);

    if (RemainingRefs == 0) {
        AfdFreeSocketObject(Socket);
    }

    return STATUS_SUCCESS;
}
