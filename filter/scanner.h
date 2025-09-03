/*
        scanner.h  (kernel-only header)
*/
#ifndef __SCANNER_H__
#define __SCANNER_H__

#include <fltKernel.h>
#include "scanuk.h"   // 공용 프로토콜(Op/Message) – 커널/유저 동일 헤더 사용

///////////////////////////////////////////////////////////////////////////
//
//  Globals
//
///////////////////////////////////////////////////////////////////////////

typedef struct _SCANNER_DATA {
    PDRIVER_OBJECT DriverObject;
    PFLT_FILTER    Filter;
    PFLT_PORT      ServerPort;
    PEPROCESS      UserProcess;
    PFLT_PORT      ClientPort;
    EX_PUSH_LOCK   ClientPortLock;
} SCANNER_DATA, * PSCANNER_DATA;

extern SCANNER_DATA ScannerData;

///////////////////////////////////////////////////////////////////////////
//
//  Driver lifecycle
//
///////////////////////////////////////////////////////////////////////////

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
ScannerUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
ScannerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

NTSTATUS
ScannerQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

///////////////////////////////////////////////////////////////////////////
//
//  I/O callbacks
//
///////////////////////////////////////////////////////////////////////////

FLT_PREOP_CALLBACK_STATUS
ScannerPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
ScannerPostCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
ScannerPreCleanup(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_PREOP_CALLBACK_STATUS
ScannerPreWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

#if (WINVER >= 0x0602)
FLT_PREOP_CALLBACK_STATUS
ScannerPreFileSystemControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);
#endif

#endif /* __SCANNER_H__ */