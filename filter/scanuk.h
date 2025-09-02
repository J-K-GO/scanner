// scanuk.h  (공용 헤더)
#pragma once
#ifdef _KERNEL_MODE
#include <fltKernel.h>
#include <ntstrsafe.h>
#else
#include <windows.h>
#include <fltuser.h>
#endif

#define SCANNER_READ_BUFFER_SIZE 1024
#define ScannerPortName L"\\ScannerPort"

typedef enum _SCANNER_OPCODE {
    ScannerOp_ScanBuffer = 1,
    ScannerOp_DriverLoad = 2
} SCANNER_OPCODE;

typedef struct _SCANNER_DRIVERLOAD_PAYLOAD {
    ULONGLONG ImageBase, ImageSize;
    WCHAR     Path[260];
} SCANNER_DRIVERLOAD_PAYLOAD;

// 여기가 포인트: Reserved로 정렬을 '명시적'으로 고정
typedef struct _SCANNER_NOTIFICATION {
    ULONG Op;
    ULONG Reserved;  // 패딩 강제 (항상 0으로 채우세요)
    union {
        struct {
            ULONG BytesToScan;
            UCHAR Contents[1024];
        } Scan;
        struct {
            ULONGLONG ImageBase;
            ULONGLONG ImageSize;
            WCHAR Path[260];
        } Driver;
    } U;
} SCANNER_NOTIFICATION, * PSCANNER_NOTIFICATION;

typedef struct _SCANNER_REPLY {
    ULONG   Op;
    BOOLEAN SafeToOpen;
} SCANNER_REPLY, * PSCANNER_REPLY;

#ifdef _KERNEL_MODE
typedef struct _SCANNER_MESSAGE {
    FILTER_MESSAGE_HEADER MessageHeader;
    SCANNER_NOTIFICATION  Notification;
} SCANNER_MESSAGE, * PSCANNER_MESSAGE;
#else
typedef struct _SCANNER_MESSAGE {
    FILTER_MESSAGE_HEADER MessageHeader;
    SCANNER_NOTIFICATION  Notification;
    OVERLAPPED            Ovlp;  // user-mode only
} SCANNER_MESSAGE, * PSCANNER_MESSAGE;
#endif

typedef struct _SCANNER_REPLY_MESSAGE {
    FILTER_REPLY_HEADER ReplyHeader;
    SCANNER_REPLY       Reply;
} SCANNER_REPLY_MESSAGE, * PSCANNER_REPLY_MESSAGE;
