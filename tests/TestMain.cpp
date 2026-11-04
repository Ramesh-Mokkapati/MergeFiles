// TestMain.cpp - runs every registered test; an optional argument filters by "Suite.Name" substring.
#include "TestFramework.h"

#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const mftest::TestCase& test : mftest::Registry())
    {
        const std::string fullName = std::string(test.suite) + "." + test.name;
        if (filter && fullName.find(filter) == std::string::npos)
            continue;
        const int before = mftest::Failures();
        test.function();
        ++run;
        std::printf("%s %s\n", mftest::Failures() == before ? "[ ok ]" : "[FAIL]", fullName.c_str());
    }
    std::printf("\n%d tests, %d failed checks\n", run, mftest::Failures());
    return mftest::Failures() == 0 ? 0 : 1;
}
