#include "driver.hpp"
#include <iostream>
#include <string>

int main() {
	try {
		std::cout << "Driver test application" << std::endl;
		

		DWORD process_id = 1234;
		

		auto driver = std::make_unique<driver_manager>("\\\\.\\kmdriver", process_id);
		std::cout << "Driver initialized successfully" << std::endl;


		uintptr_t test_address = 0x12345678;
		uint32_t value = driver->RPM<uint32_t>(test_address);
		std::cout << "Read value: " << std::hex << value << std::endl;


		driver->WPM<uint32_t>(test_address, 0xDEADBEEF);
		std::cout << "Write completed" << std::endl;

		return 0;
	}
	catch (const DriverException& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	catch (const std::exception& e) {
		std::cerr << "Unexpected error: " << e.what() << std::endl;
		return 1;
	}
}
