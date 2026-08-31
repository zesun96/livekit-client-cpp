#include <vld.h>

#include <iostream>

int main() {
#if !defined(_DEBUG)
	std::cout << "Visual Leak Detector is active only for Debug builds.\n";
	return 77;
#else
	VLDSetReportOptions(VLD_OPT_REPORT_TO_STDOUT, nullptr);
	VLDMarkAllLeaksAsReported();

	volatile int* allocation = new int(42);
	const VLD_UINT leak_count = VLDGetLeaksCount();
	delete allocation;
	VLDMarkAllLeaksAsReported();

	if (leak_count == 0) {
		std::cerr << "Visual Leak Detector did not observe the self-test allocation.\n";
		return 1;
	}
	std::cout << "Visual Leak Detector self-test observed " << leak_count << " allocation(s).\n";
	return 0;
#endif
}
