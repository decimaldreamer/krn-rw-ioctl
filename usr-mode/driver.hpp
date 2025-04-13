#pragma once
#include <Windows.h>
#include <string>
#include <stdexcept>
#include <memory>
class DriverException : public std::runtime_error {
public:
    explicit DriverException(const std::string& message) : std::runtime_error(message) {}
};
constexpr DWORD init_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); 
constexpr DWORD read_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); 
constexpr DWORD write_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); 

class driver_manager {
private:
    HANDLE m_driver_handle = nullptr;
    DWORD m_target_process_id = 0;

    struct info_t { 
        UINT64 target_pid = 0; 
        UINT64 target_address = 0x0; 
        UINT64 buffer_address = 0x0; 
        UINT64 size = 0; 
        UINT64 return_size = 0; 
    };

    void check_handle() const {
        if (!m_driver_handle || m_driver_handle == INVALID_HANDLE_VALUE) {
            throw DriverException("Driver handle is invalid");
        }
    }

    bool perform_io_control(DWORD control_code, info_t& io_info) {
        DWORD bytes_returned;
        return DeviceIoControl(
            m_driver_handle,
            control_code,
            &io_info,
            sizeof(io_info),
            &io_info,
            sizeof(io_info),
            &bytes_returned,
            nullptr
        );
    }

public:
    driver_manager(const char* driver_name, DWORD target_process_id) {
        m_target_process_id = target_process_id;
        m_driver_handle = CreateFileA(
            driver_name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (!m_driver_handle || m_driver_handle == INVALID_HANDLE_VALUE) {
            throw DriverException("Failed to open driver handle");
        }

        attach_to_process(target_process_id);
    }

    ~driver_manager() {
        if (m_driver_handle && m_driver_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_driver_handle);
        }
    }

    driver_manager(const driver_manager&) = delete;
    driver_manager& operator=(const driver_manager&) = delete;
    driver_manager(driver_manager&&) = delete;
    driver_manager& operator=(driver_manager&&) = delete;

    void attach_to_process(DWORD process_id) {
        check_handle();
        
        info_t io_info;
        io_info.target_pid = process_id;

        if (!perform_io_control(init_code, io_info)) {
            throw DriverException("Failed to attach to process");
        }
    }

    template<typename T>
    T RPM(const UINT64 address) {
        check_handle();
        
        info_t io_info;
        T read_data;

        io_info.target_address = address;
        io_info.buffer_address = reinterpret_cast<UINT64>(&read_data);
        io_info.size = sizeof(T);

        if (!perform_io_control(read_code, io_info)) {
            throw DriverException("Failed to read memory");
        }

        if (io_info.return_size != sizeof(T)) {
            throw DriverException("Incomplete memory read");
        }

        return read_data;
    }

    template<typename T>
    void WPM(const UINT64 address, const T& value) {
        check_handle();
        
        info_t io_info;
        io_info.target_address = address;
        io_info.buffer_address = reinterpret_cast<UINT64>(&value);
        io_info.size = sizeof(T);

        if (!perform_io_control(write_code, io_info)) {
            throw DriverException("Failed to write memory");
        }

        if (io_info.return_size != sizeof(T)) {
            throw DriverException("Incomplete memory write");
        }
    }
    template<typename T>
    void RPM_batch(const UINT64 address, T* buffer, size_t count) {
        check_handle();
        
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = RPM<T>(address + (i * sizeof(T)));
        }
    }
    template<typename T>
    void WPM_batch(const UINT64 address, const T* buffer, size_t count) {
        check_handle();
        
        for (size_t i = 0; i < count; ++i) {
            WPM<T>(address + (i * sizeof(T)), buffer[i]);
        }
    }
    std::vector<UINT64> pattern_scan(const UINT64 start_address, const UINT64 end_address, 
                                    const std::vector<BYTE>& pattern, const std::string& mask) {
        check_handle();
        
        std::vector<UINT64> results;
        const size_t pattern_size = pattern.size();

        for (UINT64 addr = start_address; addr < end_address - pattern_size; ++addr) {
            bool found = true;
            for (size_t i = 0; i < pattern_size; ++i) {
                if (mask[i] == 'x' && RPM<BYTE>(addr + i) != pattern[i]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                results.push_back(addr);
            }
        }

        return results;
    }
};
