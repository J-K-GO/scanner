/*++

        scanner.h  (kernel-only header)

    Kernel-mode only declarations for the Scanner mini-filter.
    Adds image-load (.sys) notification plumbing to notify user-mode via
    the existing communication port.

--*/
#ifndef __SCANNER_H__
#define __SCANNER_H__

#include <fltKernel.h>
#include "scanuk.h"   // ★ 공용 프로토콜(Op/Message) – 커널/유저 동일 헤더 사용

///////////////////////////////////////////////////////////////////////////
//
//  Globals
//
///////////////////////////////////////////////////////////////////////////

typedef struct _SCANNER_DATA {

    //
    //  The object that identifies this driver.
    //
    PDRIVER_OBJECT DriverObject;

    //
    //  The filter handle from FltRegisterFilter.
    //
    PFLT_FILTER    Filter;

    //
    //  Listens for incoming connections (FltCreateCommunicationPort).
    //
    PFLT_PORT      ServerPort;

    //
    //  User process that connected to the port.
    //
    PEPROCESS      UserProcess;

    //
    //  Client port for a connection to user-mode.
    //
    PFLT_PORT      ClientPort;

    //
    //  Protect ClientPort/UserProcess updates.
    //
    EX_PUSH_LOCK   ClientPortLock;

    //
    //  Whether image load callback is registered.
    //
    BOOLEAN        ImageLoadCbRegistered;

} SCANNER_DATA, * PSCANNER_DATA;

extern SCANNER_DATA ScannerData;

typedef struct _SCANNER_STREAM_HANDLE_CONTEXT {
    BOOLEAN RescanRequired;
} SCANNER_STREAM_HANDLE_CONTEXT, * PSCANNER_STREAM_HANDLE_CONTEXT;

#pragma warning(push)
#pragma warning(disable:4200) // zero-length array
typedef struct _SCANNER_CREATE_PARAMS {
    WCHAR String[0];
} SCANNER_CREATE_PARAMS, * PSCANNER_CREATE_PARAMS;
#pragma warning(pop)

///////////////////////////////////////////////////////////////////////////
//
//  Driver lifecycle (implemented in scanner.c / scanr.c)
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

///////////////////////////////////////////////////////////////////////////
//
//  Image-load (.sys) notify – prototypes (implemented in scanr.c)
//
//  - ScannerImageLoadInit / Fini : 콜백 등록/해제
//  - ScannerImageLoadNotify     : PsSetLoadImageNotifyRoutine에서 호출될 콜백
//  - ScannerSendDriverLoadEvent : 유저모드로 메시지 전송 헬퍼
//
///////////////////////////////////////////////////////////////////////////

NTSTATUS
ScannerImageLoadInit(
    VOID
);

VOID
ScannerImageLoadFini(
    VOID
);

VOID
ScannerImageLoadNotify(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
);

NTSTATUS
ScannerSendDriverLoadEvent(
    _In_ PUNICODE_STRING FullImageName,
    _In_ PIMAGE_INFO ImageInfo
);

#endif /* __SCANNER_H__ */
