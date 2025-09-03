#include <fltKernel.h>
#include <ntstrsafe.h>
#include "scanuk.h"
#include "scanner.h"

SCANNER_DATA ScannerData;

// ====== 주요 로직: PreCreate 콜백 ======
FLT_PREOP_CALLBACK_STATUS
ScannerPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb->Parameters.Create.Options & FILE_DIRECTORY_FILE) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    FLT_PREOP_CALLBACK_STATUS returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;
    PFLT_PORT clientPort = NULL;
    PSCANNER_NOTIFICATION notification = NULL;

    status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);
    if (!NT_SUCCESS(status)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    FltParseFileNameInformation(nameInfo);

    // ✅ 변수를 한 번만 선언합니다.
    UNICODE_STRING sysExtension = RTL_CONSTANT_STRING(L"sys");

    // .sys 파일이 아니면 감지 로직을 건너뜁니다.
    if (!RtlEqualUnicodeString(&nameInfo->Extension, &sysExtension, TRUE)) {
        goto Cleanup;
    }

    ExAcquirePushLockShared(&ScannerData.ClientPortLock);
    clientPort = ScannerData.ClientPort;
    if (clientPort) {
        ObReferenceObject(clientPort);
    }
    ExReleasePushLockShared(&ScannerData.ClientPortLock);

    if (clientPort == NULL) {
        goto Cleanup;
    }

    // ★★ 오류 수정: 빌드 환경에 맞춰 4-argument 버전의 함수 호출로 변경 ★★
    notification = FltAllocatePoolAlignedWithTag(
        FltObjects->Instance,
        NonPagedPool,
        sizeof(SCANNER_NOTIFICATION),
        'nacS'
    );

    // 반환된 포인터가 NULL인지 직접 확인
    if (notification == NULL) {
        goto Cleanup;
    }
    RtlZeroMemory(notification, sizeof(SCANNER_NOTIFICATION));

    notification->Op = ScannerOp_CheckDriver;

    size_t pathLen = min(nameInfo->Name.Length / sizeof(WCHAR), RTL_NUMBER_OF(notification->U.Check.Path) - 1);
    RtlCopyMemory(notification->U.Check.Path, nameInfo->Name.Buffer, pathLen * sizeof(WCHAR));
    notification->U.Check.Path[pathLen] = L'\0';

    SCANNER_REPLY reply = { 0 };
    ULONG replyLength = sizeof(reply);
    LARGE_INTEGER timeout;
    timeout.QuadPart = -30 * 10000000LL;

    status = FltSendMessage(ScannerData.Filter, &clientPort, notification, sizeof(SCANNER_NOTIFICATION), &reply, &replyLength, &timeout);

    if (NT_SUCCESS(status) && replyLength > 0 && !reply.SafeToOpen) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        returnStatus = FLT_PREOP_COMPLETE;
    }

Cleanup:
    if (notification) {
        FltFreePoolAlignedWithTag(FltObjects->Instance, notification, 'nacS');
    }
    if (nameInfo) {
        FltReleaseFileNameInformation(nameInfo);
    }
    if (clientPort) {
        ObDereferenceObject(clientPort);
    }

    return returnStatus;
}


// ====== 나머지 콜백 함수 정의 ======

FLT_POSTOP_CALLBACK_STATUS
ScannerPostCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_    PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_    FLT_POST_OPERATION_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS
ScannerPreCleanup(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
ScannerPreWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

#if (WINVER >= 0x0602)
FLT_PREOP_CALLBACK_STATUS
ScannerPreFileSystemControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
#endif

NTSTATUS
ScannerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);
    return STATUS_SUCCESS;
}

NTSTATUS
ScannerQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}


// ====== 통신 콜백 ======
static NTSTATUS
ScannerConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionPortCookie
)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    if (ConnectionPortCookie) *ConnectionPortCookie = NULL;

    ExAcquirePushLockExclusive(&ScannerData.ClientPortLock);
    ScannerData.ClientPort = ClientPort;
    ScannerData.UserProcess = PsGetCurrentProcess();
    ExReleasePushLockExclusive(&ScannerData.ClientPortLock);
    return STATUS_SUCCESS;
}


static VOID
ScannerDisconnect(
    _In_opt_ PVOID ConnectionCookie
)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

    ExAcquirePushLockExclusive(&ScannerData.ClientPortLock);
    if (ScannerData.ClientPort)
    {
        FltCloseClientPort(ScannerData.Filter, &ScannerData.ClientPort);
        ScannerData.ClientPort = NULL;
        ScannerData.UserProcess = NULL;
    }
    ExReleasePushLockExclusive(&ScannerData.ClientPortLock);
}

// ====== 등록 테이블 ======
CONST FLT_CONTEXT_REGISTRATION gContexts[] = {
    { FLT_CONTEXT_END }
};

CONST FLT_OPERATION_REGISTRATION gCallbacks[] = {
    { IRP_MJ_CREATE,               0, ScannerPreCreate,             ScannerPostCreate },
    { IRP_MJ_CLEANUP,              0, ScannerPreCleanup,            NULL              },
    { IRP_MJ_WRITE,                0, ScannerPreWrite,              NULL              },
#if (WINVER >= 0x0602)
    { IRP_MJ_FILE_SYSTEM_CONTROL,  0, ScannerPreFileSystemControl,  NULL              },
#endif
    { IRP_MJ_OPERATION_END }
};

NTSTATUS ScannerUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags);

CONST FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    gContexts,
    gCallbacks,
    ScannerUnload,
    ScannerInstanceSetup,
    ScannerQueryTeardown,
    NULL, NULL, NULL, NULL, NULL, NULL
};

// ====== DriverEntry / Unload ======
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    UNICODE_STRING portName;
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    RtlZeroMemory(&ScannerData, sizeof(ScannerData));
    ScannerData.DriverObject = DriverObject;
    ExInitializePushLock(&ScannerData.ClientPortLock);

    status = FltRegisterFilter(DriverObject, &FilterRegistration, &ScannerData.Filter);
    if (!NT_SUCCESS(status)) goto Exit;

    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status)) goto ExitUnreg;

    RtlInitUnicodeString(&portName, ScannerPortName);
    InitializeObjectAttributes(&oa, &portName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, sd);

    status = FltCreateCommunicationPort(ScannerData.Filter, &ScannerData.ServerPort, &oa, NULL, ScannerConnect, ScannerDisconnect, NULL, 1);

    FltFreeSecurityDescriptor(sd);
    sd = NULL;

    if (!NT_SUCCESS(status)) goto ExitUnreg;

    status = FltStartFiltering(ScannerData.Filter);
    if (!NT_SUCCESS(status)) goto ExitClosePort;

    return STATUS_SUCCESS;

ExitClosePort:
    if (ScannerData.ServerPort)
    {
        FltCloseCommunicationPort(ScannerData.ServerPort);
        ScannerData.ServerPort = NULL;
    }
ExitUnreg:
    if (ScannerData.Filter)
    {
        FltUnregisterFilter(ScannerData.Filter);
        ScannerData.Filter = NULL;
    }
Exit:
    if (sd) { FltFreeSecurityDescriptor(sd); }
    return status;
}


NTSTATUS
ScannerUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Flags);

    if (ScannerData.ServerPort)
    {
        FltCloseCommunicationPort(ScannerData.ServerPort);
        ScannerData.ServerPort = NULL;
    }

    if (ScannerData.Filter)
    {
        FltUnregisterFilter(ScannerData.Filter);
        ScannerData.Filter = NULL;
    }
    return STATUS_SUCCESS;
}