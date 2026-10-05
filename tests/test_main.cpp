#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include "TestFramework.h"

// Usage: run_tests [name-filter]   (runs tests whose name contains the filter)
int main(int argc, char* argv[]) {
    const std::string filter = (argc > 1) ? argv[1] : "";
    int ran = 0;
    int failedTests = 0;

    for (const TestCase& test : testRegistry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;

        ++ran;
        const int failuresBefore = failedChecks().load();
        std::cout << "[ RUN  ] " << test.name << std::endl;
        const auto start = std::chrono::steady_clock::now();

        try {
            test.body();
        } catch (const std::exception& e) {
            testFail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            testFail(__FILE__, __LINE__, "unexpected unknown exception");
        }

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
        if (failedChecks().load() == failuresBefore) {
            std::cout << "[  OK  ] " << test.name << " (" << ms << " ms)" << std::endl;
        } else {
            ++failedTests;
            std::cout << "[ FAIL ] " << test.name << " (" << ms << " ms)" << std::endl;
        }
    }

    std::cout << "\n==== " << ran << " test(s) run, " << (ran - failedTests) << " passed, "
              << failedTests << " failed (" << totalChecks().load() << " checks) ====" << std::endl;
    if (ran == 0) {
        std::cerr << "no test matched the filter '" << filter << "'" << std::endl;
        return 1;
    }
    return failedTests == 0 ? 0 : 1;
}
