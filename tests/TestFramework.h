#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

// A tiny unit-test framework: enough for this project, no external library.

#include <atomic>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct TestCase {
    std::string name;
    std::function<void()> body;
};

// Function-local statics avoid the static-initialization-order problem:
// the registry exists the first time any TEST() registers itself.
inline std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}
inline std::atomic<int>& failedChecks() { static std::atomic<int> n{0}; return n; }
inline std::atomic<int>& totalChecks()  { static std::atomic<int> n{0}; return n; }

inline void testFail(const char* file, int line, const std::string& message) {
    ++failedChecks();
    std::cerr << "    FAILED " << file << ':' << line << ": " << message << std::endl;
}

// Constructed before main() by the TEST macro; adds a test to the registry.
struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> body) {
        testRegistry().push_back(TestCase{name, std::move(body)});
    }
};

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* exprA, const char* exprB,
             const char* file, int line) {
    ++totalChecks();
    if (!(a == b)) {
        std::ostringstream out;
        out << exprA << " == " << exprB << "   (got [" << a << "] vs [" << b << "])";
        testFail(file, line, out.str());
    }
}

#define TEST(name)                                                      \
    static void test_##name();                                          \
    static TestRegistrar registrar_##name(#name, test_##name);          \
    static void test_##name()

#define CHECK(cond)                                                     \
    do {                                                                \
        ++totalChecks();                                                \
        if (!(cond)) testFail(__FILE__, __LINE__, #cond);               \
    } while (0)

#define CHECK_EQ(a, b) checkEq((a), (b), #a, #b, __FILE__, __LINE__)

#endif // TEST_FRAMEWORK_H
