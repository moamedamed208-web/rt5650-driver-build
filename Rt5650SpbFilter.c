#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <reshub.h>

#define RT5650_POOL_TAG             '05TR'
#define RT5650_READ_SEQUENCE_BYTES  3u
#define RT5650_WRITE_FRAME_BYTES    3u

typedef struct _DEVICE_CONTEXT {
    LARGE_INTEGER I2cConnectionId;
    BOOLEAN I2cConnectionFound;
    WDFIOTARGET SpbIoTarget;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext);

typedef struct _RT5650_UPDATE_OP {
    UCHAR Register;
    USHORT Mask;
    USHORT Value;
} RT5650_UPDATE_OP, *PRT5650_UPDATE_OP;

static const RT5650_UPDATE_OP g_Rt5650InitSequence[] = {
    { 0x2A, 0x4040, 0x0000 },
    { 0x46, 0x0002, 0x0000 },
    { 0x47, 0x0002, 0x0000 },
    { 0x48, 0x2001, 0x0000 },
    { 0x01, 0xC0C0, 0x0000 },
    { 0x01, 0xFFFF, 0x1818 },
};

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Rt5650EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE Rt5650EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Rt5650EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY Rt5650EvtDeviceD0Entry;

static VOID
Rt5650CloseSpbTarget(
    _Inout_ PDEVICE_CONTEXT DevContext
    );

static NTSTATUS
Rt5650OpenSpbTarget(
    _In_ WDFDEVICE Device
    );

static NTSTATUS
Rt5650WriteRegister(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _In_ USHORT Value
    );

static NTSTATUS
Rt5650ReadRegister(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value
    );

static NTSTATUS
Rt5650UpdateBits(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _In_ USHORT Mask,
    _In_ USHORT Value
    );

static NTSTATUS
Rt5650ApplyInitSequence(
    _In_ PDEVICE_CONTEXT DevContext
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Rt5650EvtDeviceAdd)
#pragma alloc_text(PAGE, Rt5650EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, Rt5650EvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, Rt5650EvtDeviceD0Entry)
#endif

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;

    KdPrintEx((DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "Rt5650SpbFilter: DriverEntry\n"));

    WDF_DRIVER_CONFIG_INIT(&config, Rt5650EvtDeviceAdd);

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES,
                             &config,
                             WDF_NO_HANDLE);

    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: WdfDriverCreate failed, status=0x%08X\n",
                   status));
    }

    return status;
}

_Use_decl_annotations_
NTSTATUS
Rt5650EvtDeviceAdd(
    WDFDRIVER Driver,
    PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDFDEVICE device;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PDEVICE_CONTEXT devContext;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = Rt5650EvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = Rt5650EvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = Rt5650EvtDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: WdfDeviceCreate failed, status=0x%08X\n",
                   status));
        return status;
    }

    devContext = DeviceGetContext(device);
    devContext->I2cConnectionFound = FALSE;
    devContext->I2cConnectionId.QuadPart = 0;
    devContext->SpbIoTarget = NULL;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
Rt5650EvtDevicePrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    ULONG resourceCount;
    ULONG index;
    PDEVICE_CONTEXT devContext;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesRaw);

    devContext = DeviceGetContext(Device);
    devContext->I2cConnectionFound = FALSE;
    devContext->I2cConnectionId.QuadPart = 0;

    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

    for (index = 0; index < resourceCount; ++index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;

        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, index);
        if (descriptor == NULL) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        if (descriptor->Type != CmResourceTypeConnection) {
            continue;
        }

        if (descriptor->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            descriptor->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C) {

            devContext->I2cConnectionId.LowPart = descriptor->u.Connection.IdLowPart;
            devContext->I2cConnectionId.HighPart = descriptor->u.Connection.IdHighPart;
            devContext->I2cConnectionFound = TRUE;
            break;
        }
    }

    if (!devContext->I2cConnectionFound) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: I2C connection resource not found\n"));
        return STATUS_NOT_FOUND;
    }

    status = Rt5650OpenSpbTarget(Device);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: Rt5650OpenSpbTarget failed, status=0x%08X\n",
                   status));
    }

    return status;
}

_Use_decl_annotations_
NTSTATUS
Rt5650EvtDeviceD0Entry(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT devContext;
    NTSTATUS status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(PreviousState);

    devContext = DeviceGetContext(Device);

    if (devContext->SpbIoTarget == NULL) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: D0Entry without open SPB target\n"));
        return STATUS_INVALID_DEVICE_STATE;
    }

    status = Rt5650ApplyInitSequence(devContext);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Rt5650SpbFilter: init sequence failed, status=0x%08X\n",
                   status));
    }

    return status;
}

_Use_decl_annotations_
NTSTATUS
Rt5650EvtDeviceReleaseHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT devContext;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    devContext = DeviceGetContext(Device);
    Rt5650CloseSpbTarget(devContext);
    devContext->I2cConnectionFound = FALSE;
    devContext->I2cConnectionId.QuadPart = 0;

    return STATUS_SUCCESS;
}

static VOID
Rt5650CloseSpbTarget(
    _Inout_ PDEVICE_CONTEXT DevContext
    )
{
    PAGED_CODE();

    if (DevContext->SpbIoTarget != NULL) {
        WdfIoTargetClose(DevContext->SpbIoTarget);
        WdfObjectDelete(DevContext->SpbIoTarget);
        DevContext->SpbIoTarget = NULL;
    }
}

static NTSTATUS
Rt5650OpenSpbTarget(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    DECLARE_UNICODE_STRING_SIZE(resourceHubPath, RESOURCE_HUB_PATH_SIZE);
    PDEVICE_CONTEXT devContext;

    PAGED_CODE();

    devContext = DeviceGetContext(Device);

    Rt5650CloseSpbTarget(devContext);

    status = WdfIoTargetCreate(Device,
                               WDF_NO_OBJECT_ATTRIBUTES,
                               &devContext->SpbIoTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RESOURCE_HUB_CREATE_PATH_FROM_ID(&resourceHubPath,
                                     devContext->I2cConnectionId.LowPart,
                                     devContext->I2cConnectionId.HighPart);

    if (resourceHubPath.Length == 0 ||
        resourceHubPath.Buffer == NULL ||
        resourceHubPath.Buffer[0] == UNICODE_NULL) {
        WdfObjectDelete(devContext->SpbIoTarget);
        devContext->SpbIoTarget = NULL;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams,
                                                &resourceHubPath,
                                                FILE_GENERIC_READ | FILE_GENERIC_WRITE);

    status = WdfIoTargetOpen(devContext->SpbIoTarget, &openParams);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(devContext->SpbIoTarget);
        devContext->SpbIoTarget = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Rt5650WriteRegister(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _In_ USHORT Value
    )
{
    NTSTATUS status;
    UCHAR writeFrame[RT5650_WRITE_FRAME_BYTES];
    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    ULONG_PTR bytesTransferred = 0;

    PAGED_CODE();

    if (DevContext->SpbIoTarget == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    writeFrame[0] = Register;
    writeFrame[1] = (UCHAR)((Value >> 8) & 0xFF);
    writeFrame[2] = (UCHAR)(Value & 0xFF);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor,
                                      writeFrame,
                                      (ULONG)sizeof(writeFrame));

    status = WdfIoTargetSendWriteSynchronously(DevContext->SpbIoTarget,
                                               NULL,
                                               &memoryDescriptor,
                                               NULL,
                                               NULL,
                                               &bytesTransferred);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (bytesTransferred != sizeof(writeFrame)) {
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Rt5650ReadRegister(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value
    )
{
    NTSTATUS status;
    SPB_TRANSFER_LIST_AND_ENTRIES(2) sequence;
    WDF_MEMORY_DESCRIPTOR sequenceDescriptor;
    ULONG_PTR bytesTransferred = 0;
    UCHAR readBuffer[2] = { 0 };

    PAGED_CODE();

    if (Value == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (DevContext->SpbIoTarget == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    SPB_TRANSFER_LIST_INIT(&sequence.List, 2);

    sequence.List.Transfers[0] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionToDevice,
        0,
        &Register,
        sizeof(Register));

    sequence.List.Transfers[1] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionFromDevice,
        0,
        readBuffer,
        sizeof(readBuffer));

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&sequenceDescriptor,
                                      &sequence,
                                      (ULONG)sizeof(sequence));

    status = WdfIoTargetSendIoctlSynchronously(DevContext->SpbIoTarget,
                                               NULL,
                                               IOCTL_SPB_EXECUTE_SEQUENCE,
                                               &sequenceDescriptor,
                                               NULL,
                                               NULL,
                                               &bytesTransferred);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (bytesTransferred != RT5650_READ_SEQUENCE_BYTES) {
        return STATUS_IO_DEVICE_ERROR;
    }

    *Value = (USHORT)(((USHORT)readBuffer[0] << 8) | readBuffer[1]);
    return STATUS_SUCCESS;
}

static NTSTATUS
Rt5650UpdateBits(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ UCHAR Register,
    _In_ USHORT Mask,
    _In_ USHORT Value
    )
{
    NTSTATUS status;
    USHORT currentValue;
    USHORT newValue;
    USHORT invertedMask;

    PAGED_CODE();

    status = Rt5650ReadRegister(DevContext, Register, &currentValue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    invertedMask = (USHORT)(~Mask);
    newValue = (USHORT)((currentValue & invertedMask) | (Value & Mask));

    if (newValue == currentValue) {
        return STATUS_SUCCESS;
    }

    return Rt5650WriteRegister(DevContext, Register, newValue);
}

static NTSTATUS
Rt5650ApplyInitSequence(
    _In_ PDEVICE_CONTEXT DevContext
    )
{
    NTSTATUS status;
    ULONG index;

    PAGED_CODE();

    for (index = 0; index < ARRAYSIZE(g_Rt5650InitSequence); ++index) {
        const RT5650_UPDATE_OP* op;

        op = &g_Rt5650InitSequence[index];

        status = Rt5650UpdateBits(DevContext,
                                  op->Register,
                                  op->Mask,
                                  op->Value);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_IHVDRIVER_ID,
                       DPFLTR_ERROR_LEVEL,
                       "Rt5650SpbFilter: Rt5650UpdateBits failed at step %lu, reg=0x%02X, status=0x%08X\n",
                       index,
                       op->Register,
                       status));
            return status;
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "Rt5650SpbFilter: init sequence applied successfully\n"));

    return STATUS_SUCCESS;
}
