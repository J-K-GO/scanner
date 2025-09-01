/* user-mode scanner client */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fltuser.h>
#include "scanuk.h"   // ← 이것만 포함(중복 include 금지)



#define SCANNER_DEFAULT_REQUEST_COUNT 5
#define SCANNER_DEFAULT_THREAD_COUNT  2
#define SCANNER_MAX_THREAD_COUNT      64

static const UCHAR g_Foul[] = "foul";

typedef struct _SCANNER_THREAD_CONTEXT {
    HANDLE Port;
    HANDLE Completion;
} SCANNER_THREAD_CONTEXT, * PSCANNER_THREAD_CONTEXT;

static VOID Usage(VOID)
{
    printf("Connects to the scanner filter and scans buffers \n");
    printf("Usage: scanuser [requests per thread] [number of threads(1-64)]\n");
}

static BOOL ScanBuffer(_In_reads_bytes_(len) const UCHAR* buf, _In_ ULONG len)
{
    size_t n = sizeof(g_Foul) - 1;
    if (len < n) return FALSE;
    for (ULONG i = 0; i <= len - n; ++i)
        if (memcmp(buf + i, g_Foul, n) == 0) return TRUE;
    return FALSE;
}

static VOID HandleDriverLoad(_In_ SCANNER_DRIVERLOAD_PAYLOAD* drv)
{
    wprintf(L"[DriverLoad] path=\"%ls\" base=0x%llX size=0x%llX\n",
        drv->Path,
        (unsigned long long)drv->ImageBase,
        (unsigned long long)drv->ImageSize);
}

DWORD WINAPI ScannerWorker(_In_ PSCANNER_THREAD_CONTEXT Ctx)
{
    HRESULT hr = S_OK;
    while (1) {
        DWORD outSize = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pOvlp = NULL;

        BOOL ok = GetQueuedCompletionStatus(Ctx->Completion, &outSize, &key, &pOvlp, INFINITE);
        PSCANNER_MESSAGE msg = CONTAINING_RECORD(pOvlp, SCANNER_MESSAGE, Ovlp);
        if (!ok) { hr = HRESULT_FROM_WIN32(GetLastError()); break; }

        PSCANNER_NOTIFICATION n = &msg->Notification;

        SCANNER_REPLY_MESSAGE rep = { 0 };
        rep.ReplyHeader.Status = 0;
        rep.ReplyHeader.MessageId = msg->MessageHeader.MessageId;
        rep.Reply.Op = n->Op;

        switch (n->Op) {
        case ScannerOp_ScanBuffer: {
            BOOL foul = ScanBuffer(n->U.Scan.Contents, n->U.Scan.BytesToScan);
            rep.Reply.SafeToOpen = !foul;
            printf("Replying (ScanBuffer) SafeToOpen=%d\n", rep.Reply.SafeToOpen);
            break;
        }
        case ScannerOp_DriverLoad: {
            HandleDriverLoad((SCANNER_DRIVERLOAD_PAYLOAD*)&n->U.Driver);
            rep.Reply.SafeToOpen = TRUE; // 의미 없음
            printf("Replying (DriverLoad ACK)\n");
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
        hr = FilterGetMessage(Ctx->Port,
            &msg->MessageHeader,
            FIELD_OFFSET(SCANNER_MESSAGE, Ovlp),  // ★ 여기까지가 msgSize와 동일해야 함
            &msg->Ovlp);


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

    printf("Scanner: Connecting to the filter ...\n");

    HANDLE port;
    HRESULT hr = FilterConnectCommunicationPort(ScannerPortName,
                                    0,
                                    NULL,
                                    0,
                                    NULL,
                                    &port);
    if (IS_ERROR(hr)) { printf("Connect error=0x%08X\n", hr); return 2; }

    HANDLE iocp = CreateIoCompletionPort(port, NULL, 0, th);
    if (!iocp) { printf("CreateIoCompletionPort err=%lu\n", GetLastError()); CloseHandle(port); return 3; }

    printf("Scanner: Port=%p Completion=%p\n", port, iocp);

    size_t total = (size_t)th * req;
    PSCANNER_MESSAGE msgs = (PSCANNER_MESSAGE)calloc(total, sizeof(SCANNER_MESSAGE));
    if (!msgs) { printf("OOM\n"); CloseHandle(iocp); CloseHandle(port); return 4; }

    SCANNER_THREAD_CONTEXT ctx = { port, iocp };
    HANDLE threads[SCANNER_MAX_THREAD_COUNT] = { 0 };

    for (DWORD i = 0; i < th; ++i) {
        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ScannerWorker, &ctx, 0, NULL);
        if (!threads[i]) { printf("CreateThread err=%lu\n", GetLastError()); return 5; }

        for (DWORD j = 0; j < req; ++j) {
            PSCANNER_MESSAGE m = &msgs[i * req + j];
            ZeroMemory(&m->Ovlp, sizeof(m->Ovlp));
            hr = FilterGetMessage(port, &m->MessageHeader,
                FIELD_OFFSET(SCANNER_MESSAGE, Ovlp), &m->Ovlp);
            if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
                printf("FilterGetMessage hr=0x%08X\n", hr); return 6;
            }
        }
    }

    WaitForMultipleObjects(th, threads, TRUE, INFINITE);

    free(msgs);
    CloseHandle(iocp);
    CloseHandle(port);
    return 0;
}
