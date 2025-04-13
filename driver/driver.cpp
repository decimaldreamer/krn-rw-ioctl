#include <ntifs.h>
#include <ntstrsafe.h>

// Debug loglama için makro
#define DEBUG_PRINT(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, fmt, __VA_ARGS__)

extern "C" {
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress, PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);
}

// IOCTL kodları için güvenli doğrulama
constexpr ULONG init_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); 
constexpr ULONG write_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); 

// Hata kodları
enum class DriverError : NTSTATUS {
	SUCCESS = STATUS_SUCCESS,
	INVALID_PARAMETER = STATUS_INVALID_PARAMETER,
	ACCESS_DENIED = STATUS_ACCESS_DENIED,
	INSUFFICIENT_RESOURCES = STATUS_INSUFFICIENT_RESOURCES,
	PROCESS_NOT_FOUND = STATUS_NOT_FOUND
};

struct info_t { 
	HANDLE target_pid = 0; 
	void*  target_address = 0x0; 
	void*  buffer_address = 0x0;
	SIZE_T size = 0; 
	SIZE_T return_size = 0; 
};
static PEPROCESS s_target_process = nullptr;
static PDEVICE_OBJECT s_device_object = nullptr;
static UNICODE_STRING s_device_name;
static UNICODE_STRING s_symbolic_link;

// IOCTL işleyicisi
NTSTATUS ctl_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Information = sizeof(info_t);
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto buffer = (info_t*)irp->AssociatedIrp.SystemBuffer;

	if (!stack || !buffer || sizeof(*buffer) < sizeof(info_t)) {
		irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_PARAMETER;
	}

	const auto ctl_code = stack->Parameters.DeviceIoControl.IoControlCode;
	NTSTATUS status = STATUS_SUCCESS;

	__try {
		switch (ctl_code) {
			case init_code: {
				status = PsLookupProcessByProcessId(buffer->target_pid, &s_target_process);
				if (!NT_SUCCESS(status)) {
					DEBUG_PRINT("Failed to lookup process: 0x%X\n", status);
					break;
				}
				break;
			}
			case read_code: {
				if (!s_target_process) {
					status = STATUS_INVALID_HANDLE;
					break;
				}
				status = MmCopyVirtualMemory(s_target_process, buffer->target_address, 
										   PsGetCurrentProcess(), buffer->buffer_address, 
										   buffer->size, KernelMode, &buffer->return_size);
				break;
			}
			case write_code: {
				if (!s_target_process) {
					status = STATUS_INVALID_HANDLE;
					break;
				}
				status = MmCopyVirtualMemory(PsGetCurrentProcess(), buffer->buffer_address, 
										   s_target_process, buffer->target_address, 
										   buffer->size, KernelMode, &buffer->return_size);
				break;
			}
			default:
				status = STATUS_INVALID_DEVICE_REQUEST;
				break;
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		status = STATUS_ACCESS_VIOLATION;
	}

	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS unsupported_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS create_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS close_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

void DriverUnload(PDRIVER_OBJECT driver_obj) {
	UNREFERENCED_PARAMETER(driver_obj);
	
	if (s_target_process) {
		ObDereferenceObject(s_target_process);
		s_target_process = nullptr;
	}

	if (s_device_object) {
		IoDeleteSymbolicLink(&s_symbolic_link);
		IoDeleteDevice(s_device_object);
		s_device_object = nullptr;
	}

	DEBUG_PRINT("Driver unloaded successfully\n");
}

NTSTATUS real_main(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(registery_path);
	
	NTSTATUS status;

	RtlInitUnicodeString(&s_device_name, L"\\Device\\kmdriver");
	RtlInitUnicodeString(&s_symbolic_link, L"\\DosDevices\\kmdriver");

	status = IoCreateDevice(driver_obj, 0, &s_device_name, FILE_DEVICE_UNKNOWN, 
						  FILE_DEVICE_SECURE_OPEN, FALSE, &s_device_object);
	if (!NT_SUCCESS(status)) {
		DEBUG_PRINT("Failed to create device: 0x%X\n", status);
		return status;
	}

	status = IoCreateSymbolicLink(&s_symbolic_link, &s_device_name);
	if (!NT_SUCCESS(status)) {
		DEBUG_PRINT("Failed to create symbolic link: 0x%X\n", status);
		IoDeleteDevice(s_device_object);
		return status;
	}

	SetFlag(s_device_object->Flags, DO_BUFFERED_IO);
	for (int t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++) 
		driver_obj->MajorFunction[t] = unsupported_io;

	driver_obj->MajorFunction[IRP_MJ_CREATE] = create_io;
	driver_obj->MajorFunction[IRP_MJ_CLOSE] = close_io;
	driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl_io;
	driver_obj->DriverUnload = DriverUnload;

	ClearFlag(s_device_object->Flags, DO_DEVICE_INITIALIZING);

	DEBUG_PRINT("Driver initialized successfully\n");
	return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(driver_obj);
	UNREFERENCED_PARAMETER(registery_path);

	UNICODE_STRING drv_name;
	RtlInitUnicodeString(&drv_name, L"\\Driver\\kmdriver");
	
	return IoCreateDriver(&drv_name, &real_main);
}
