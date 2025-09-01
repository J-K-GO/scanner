#include <fltKernel.h>
#include <ntstrsafe.h>
#include "scanuk.h"



#include "scanner.h"

#ifndef ScannerPortName
#error "wrong scanuk.h is included"
#endif
#ifndef ScannerOp_DriverLoad
//#error "scanuk.h is outdated"
#endif

SCANNER_DATA ScannerData;

// ====== I/O callback stubs (원하면 실제 로직으로 교체) ======
FLT_PREOP_CALLBACK_STATUS
ScannerPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_    PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

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
    _In_    PCFLT_RELATED_OBJECTS FltObjects,
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
    _In_    PCFLT_RELATED_OBJECTS FltObjects,
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
    _In_    PCFLT_RELATED_OBJECTS FltObjects,
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

// ====== Communication callbacks ======
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
    if (ScannerData.ClientPort) {
        FltCloseClientPort(ScannerData.Filter, &ScannerData.ClientPort);
        ScannerData.ClientPort = NULL;
        ScannerData.UserProcess = NULL;
    }
    ExReleasePushLockExclusive(&ScannerData.ClientPortLock);
}

// ====== Registration tables ======
CONST FLT_CONTEXT_REGISTRATION gContexts[] = {
    { FLT_STREAMHANDLE_CONTEXT, 0, NULL,
      sizeof(SCANNER_STREAM_HANDLE_CONTEXT), 'cSRS', NULL, NULL, NULL },
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
    PSECURITY_DESCRIPTOR sd = NULL;   // ★ 포트 보안 서술자

    // 전역 초기화
    RtlZeroMemory(&ScannerData, sizeof(ScannerData));
    ScannerData.DriverObject = DriverObject;
    ExInitializePushLock(&ScannerData.ClientPortLock);

    // 미니필터 등록
    status = FltRegisterFilter(DriverObject, &FilterRegistration, &ScannerData.Filter);
    if (!NT_SUCCESS(status)) goto Exit;

    // ★ 모든 사용자 접근 허용 SD (테스트용; 제품은 제한 SDDL 권장)
    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status)) goto ExitUnreg;

    // 포트 이름 + OA(SecurityDescriptor=sd)
    RtlInitUnicodeString(&portName, ScannerPortName);
    InitializeObjectAttributes(&oa,
        &portName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        sd);

    // 통신 포트 생성
    status = FltCreateCommunicationPort(ScannerData.Filter,
        &ScannerData.ServerPort,
        &oa,
        NULL,
        ScannerConnect,
        ScannerDisconnect,
        NULL,
        1);
    // SD는 더 이상 필요 없음
    FltFreeSecurityDescriptor(sd);
    sd = NULL;

    if (!NT_SUCCESS(status)) goto ExitUnreg;

    // 이미지 로드 콜백 등록
    status = ScannerImageLoadInit();
    if (!NT_SUCCESS(status)) goto ExitClosePort;

    // 필터 시작
    status = FltStartFiltering(ScannerData.Filter);
    if (!NT_SUCCESS(status)) goto ExitImgCb;

    return STATUS_SUCCESS;

ExitImgCb:
    ScannerImageLoadFini();
ExitClosePort:
    if (ScannerData.ServerPort) {
        FltCloseCommunicationPort(ScannerData.ServerPort);
        ScannerData.ServerPort = NULL;
    }
ExitUnreg:
    if (ScannerData.Filter) {
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

    if (ScannerData.ServerPort) {
        FltCloseCommunicationPort(ScannerData.ServerPort);
        ScannerData.ServerPort = NULL;
    }

    ScannerImageLoadFini();

    if (ScannerData.Filter) {
        FltUnregisterFilter(ScannerData.Filter);
        ScannerData.Filter = NULL;
    }
    return STATUS_SUCCESS;
}

// ====== Image-load (.sys) notify ======
NTSTATUS ScannerImageLoadInit(VOID)
{
    NTSTATUS st = PsSetLoadImageNotifyRoutine(ScannerImageLoadNotify);
    if (NT_SUCCESS(st)) ScannerData.ImageLoadCbRegistered = TRUE;
    return st;
}

VOID ScannerImageLoadFini(VOID)
{
    if (ScannerData.ImageLoadCbRegistered) {
        PsRemoveLoadImageNotifyRoutine(ScannerImageLoadNotify);
        ScannerData.ImageLoadCbRegistered = FALSE;
    }
}

VOID
ScannerImageLoadNotify(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
)
{
    UNREFERENCED_PARAMETER(ProcessId);
    if (!FullImageName || !ImageInfo) return;
    if (!ImageInfo->SystemModeImage)  return;

    UNICODE_STRING ext = RTL_CONSTANT_STRING(L".sys");
    if (!RtlSuffixUnicodeString(&ext, FullImageName, TRUE)) return;

    (void)ScannerSendDriverLoadEvent(FullImageName, ImageInfo);
}

NTSTATUS
ScannerSendDriverLoadEvent(_In_ PUNICODE_STRING FullImageName, _In_ PIMAGE_INFO ImageInfo)
{
    SCANNER_NOTIFICATION n = { 0 };  // ✅ payload만 보냄
    n.Op = ScannerOp_DriverLoad;
    n.Reserved = 0;                // scanuk.h에 Reserved 추가했다면 명시적으로 0
    n.U.Driver.ImageBase = (ULONGLONG)ImageInfo->ImageBase;
    n.U.Driver.ImageSize = (ULONGLONG)ImageInfo->ImageSize;

    size_t cch = min((SIZE_T)(FullImageName->Length / sizeof(WCHAR)),
        (SIZE_T)(RTL_NUMBER_OF(n.U.Driver.Path) - 1));
    RtlStringCchCopyNW(n.U.Driver.Path,
        RTL_NUMBER_OF(n.U.Driver.Path),
        FullImageName->Buffer,
        cch);

    SCANNER_REPLY_MESSAGE reply = { 0 };
    ULONG replyLen = sizeof(reply);

    PFLT_PORT client = NULL;
    ExAcquirePushLockShared(&ScannerData.ClientPortLock);
    client = ScannerData.ClientPort;
    if (client) ObReferenceObject(client);
    ExReleasePushLockShared(&ScannerData.ClientPortLock);
    if (!client) return STATUS_PORT_DISCONNECTED;

    // ✅ 헤더를 빼고 payload 크기만 보냅니다
    NTSTATUS st = FltSendMessage(ScannerData.Filter,
        &client,
        &n, sizeof(n),
        &reply, &replyLen,
        NULL);

    ObDereferenceObject(client);
    return st;
}
