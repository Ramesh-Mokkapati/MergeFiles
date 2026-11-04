// TestFramework.h - a minimal unit test runner (no third-party dependency).
//
//   TEST(Diff, EmptyFiles) { CHECK_EQ(DiffLines(a, b, options).size(), 0u); }
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace mftest
{
    struct TestCase
    {
        const char* suite;
        const char* name;
        void (*function)();
    };

    inline std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    inline int& Failures()
    {
        static int failures = 0;
        return failures;
    }

    struct Registrar
    {
        Registrar(const char* suite, const char* name, void (*function)())
        {
            Registry().push_back({ suite, name, function });
        }
    };

    inline void Fail(const char* file, int line, const std::string& message)
    {
        ++Failures();
        std::printf("  FAILED %s(%d): %s\n", file, line, message.c_str());
    }

    inline std::string Narrow(const std::wstring& text)
    {
        std::string out;
        for (wchar_t c : text)
            out.push_back(c < 128 ? static_cast<char>(c) : '?');
        return out;
    }

    template <typename T>
    std::string ToString(const T& value)
    {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
    inline std::string ToString(const std::wstring& value)
    {
        return "L\"" + Narrow(value) + "\"";
    }
    inline std::string ToString(const wchar_t* value)
    {
        return ToString(std::wstring(value));
    }
    inline std::string ToString(bool value)
    {
        return value ? "true" : "false";
    }
}

#define TEST(suite, name)                                                                   \
    static void Test_##suite##_##name();                                                    \
    static mftest::Registrar Registrar_##suite##_##name(#suite, #name, Test_##suite##_##name); \
    static void Test_##suite##_##name()

#define CHECK(condition)                                         \
    do                                                           \
    {                                                            \
        if (!(condition))                                        \
            mftest::Fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                                       \
    do                                                                                                                   \
    {                                                                                                                    \
        const auto& actualValue_ = (actual);                                                                             \
        const auto& expectedValue_ = (expected);                                                                         \
        if (!(actualValue_ == expectedValue_))                                                                           \
            mftest::Fail(__FILE__, __LINE__, std::string(#actual " == " #expected ": got ") + mftest::ToString(actualValue_) + \
                ", expected " + mftest::ToString(expectedValue_));                                                       \
    } while (0)
