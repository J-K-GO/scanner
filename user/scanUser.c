/* user-mode scanner client */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <strsafe.h>
#include <fltuser.h>
#include "scanuk.h"

#define SCANNER_DEFAULT_REQUEST_COUNT 5
#define SCANNER_DEFAULT_THREAD_COUNT  2
#define SCANNER_MAX_THREAD_COUNT      64

typedef struct _SCANNER_THREAD_CONTEXT
{
    HANDLE Port;
    HANDLE Completion;
} SCANNER_THREAD_CONTEXT, * PSCANNER_THREAD_CONTEXT;

static const WCHAR* g_BlockedDriver = L"sioctl.sys";

static BOOL ShouldBlockDriver(const WCHAR* driverPath)
{
    if (driverPath == NULL) {
        return FALSE;
    }
    size_t pathLen, blockedNameLen;
    if (FAILED(StringCchLengthW(driverPath, 512, &pathLen)) ||
        FAILED(StringCchLengthW(g_BlockedDriver, MAX_PATH, &blockedNameLen))) {
        return FALSE;
    }
    if (pathLen < blockedNameLen) {
        return FALSE;
    }
    if (_wcsicmp(driverPath + pathLen - blockedNameLen, g_BlockedDriver) == 0) {
        if (pathLen == blockedNameLen || driverPath[pathLen - blockedNameLen - 1] == L'\\') {
            return TRUE;
        }
    }
    return FALSE;
}

DWORD WINAPI ScannerWorker(_In_ PSCANNER_THREAD_CONTEXT Ctx)
{
    HRESULT hr = S_OK;
    while (1) {
        DWORD outSize = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pOvlp = NULL;

        BOOL ok = GetQueuedCompletionStatus(Ctx->Completion, &outSize, &key, &pOvlp, INFINITE);
        if (!ok || pOvlp == NULL) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
        PSCANNER_MESSAGE msg = CONTAINING_RECORD(pOvlp, SCANNER_MESSAGE, Ovlp);

        PSCANNER_NOTIFICATION n = &msg->Notification;

        SCANNER_REPLY_MESSAGE rep = { 0 };
        rep.ReplyHeader.Status = 0;
        rep.ReplyHeader.MessageId = msg->MessageHeader.MessageId;
        rep.Reply.Op = n->Op;

        switch (n->Op)
        {
        case ScannerOp_CheckDriver:
        {
            BOOL shouldBlock = ShouldBlockDriver(n->U.Check.Path);
            rep.Reply.SafeToOpen = !shouldBlock;
            if (shouldBlock) {
                wprintf(L"[BLOCKED] Driver: %s\n", n->U.Check.Path);
            }
            else {
                wprintf(L"[Allowed] Driver: %s\n", n->U.Check.Path);
            }
            break;
        }
        case ScannerOp_ScanBuffer:
        {
            rep.Reply.SafeToOpen = TRUE;
            printf("Replying (ScanBuffer) SafeToOpen=TRUE\n");
            break;
        }
        default:
            printf("Unknown Op: %u\n", n->Op);
            rep.Reply.SafeToOpen = TRUE;
            break;
        }

        hr = FilterReplyMessage(Ctx->Port, (PFILTER_REPLY_HEADER)&rep, sizeof(rep));
        if (FAILED(hr)) { printf("FilterReplyMessage error=0x%08X\n", hr); break; }

        ZeroMemory(&msg->Ovlp, sizeof(msg->Ovlp));
        hr = FilterGetMessage(Ctx->Port, &msg->MessageHeader, FIELD_OFFSET(SCANNER_MESSAGE, Ovlp), &msg->Ovlp);
        if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) break;
    }

    if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE))
        printf("Port disconnected (driver unloaded?)\n");
    else if (FAILED(hr))
        printf("Worker exit hr=0x%08X\n", hr);

    return (DWORD)hr;
}

int main(int argc, char** argv)
{
    DWORD req = SCANNER_DEFAULT_REQUEST_COUNT;
    DWORD th = SCANNER_DEFAULT_THREAD_COUNT;

    if (argc > 1) {
        req = max(1, (DWORD)atoi(argv[1]));
        if (argc > 2) th = max(1, min(64, (DWORD)atoi(argv[2])));
    }

    printf("Scanner: Connecting to the filter...\n");
    printf("Policy: Block any driver named '%S'\n", g_BlockedDriver);

    HANDLE port;
    HRESULT hr = FilterConnectCommunicationPort(ScannerPortName, 0, NULL, 0, NULL, &port);

    if (IS_ERROR(hr)) { printf("Connect error=0x%08X\n", hr); return 2; }

    HANDLE iocp = CreateIoCompletionPort(port, NULL, 0, th);
    if (!iocp) { printf("CreateIoCompletionPort err=%lu\n", GetLastError()); CloseHandle(port); return 3; }

    printf("Scanner: Port=%p Completion=%p\n", port, iocp);

    size_t totalMsgs = (size_t)th * req;
    PSCANNER_MESSAGE msgs = (PSCANNER_MESSAGE)calloc(totalMsgs, sizeof(SCANNER_MESSAGE));
    if (!msgs) { printf("OOM\n"); CloseHandle(iocp); CloseHandle(port); return 4; }

    SCANNER_THREAD_CONTEXT ctx = { port, iocp };
    HANDLE* threads = (HANDLE*)calloc(th, sizeof(HANDLE));
    if (!threads) { printf("OOM for threads\n"); free(msgs); CloseHandle(iocp); CloseHandle(port); return 5; }

    for (DWORD i = 0; i < th; ++i)
    {
        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ScannerWorker, &ctx, 0, NULL);
        if (!threads[i]) { printf("CreateThread err=%lu\n", GetLastError()); /* proper cleanup needed */ return 6; }
    }

    for (size_t i = 0; i < totalMsgs; ++i) {
        PSCANNER_MESSAGE m = &msgs[i];
        hr = FilterGetMessage(port, &m->MessageHeader, FIELD_OFFSET(SCANNER_MESSAGE, Ovlp), &m->Ovlp);
        if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
            printf("FilterGetMessage hr=0x%08X\n", hr);
            goto cleanup;
        }
    }

    WaitForMultipleObjects(th, threads, TRUE, INFINITE);

cleanup:
    for (DWORD i = 0; i < th; ++i) {
        if (threads[i]) CloseHandle(threads[i]);
    }

    free(threads);
    free(msgs);
    CloseHandle(iocp);
    CloseHandle(port);
    return 0;
}