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

// Opcode에 드라이버 검사 추가
typedef enum _SCANNER_OPCODE {
    ScannerOp_ScanBuffer = 1,
    // ScannerOp_DriverLoad = 2, // 사후 알림이므로 제거
    ScannerOp_CheckDriver = 3   // Pre-Create 시점에 드라이버 경로를 검사하기 위한 새 Opcode
} SCANNER_OPCODE;

// ★★★ 여기가 포인트: 모든 페이로드의 정렬과 크기를 맞추기 위해 구조를 명확히 함
typedef struct _SCANNER_NOTIFICATION {
    ULONG Op;
    ULONG Reserved;  // ★ 패딩 강제 (항상 0으로 채우세요)
    union {
        struct {
            ULONG BytesToScan;
            UCHAR Contents[1024];
        } Scan;
        struct {
            WCHAR Path[512]; // 드라이버 경로를 전달하기 위한 구조체
        } Check;
    } U;
} SCANNER_NOTIFICATION, * PSCANNER_NOTIFICATION;

typedef struct _SCANNER_REPLY {
    ULONG   Op;
    BOOLEAN SafeToOpen; // 이 플래그를 통해 커널이 차단 여부를 결정
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