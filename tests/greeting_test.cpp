#include "cpp_template/greeting.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

bool expect_equal(std::string_view label, std::string_view actual, std::string_view expected)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << label << ": expected [" << expected << "], got [" << actual << "]\n";
    return false;
}

bool rejects_empty_name()
{
    try {
        static_cast<void>(cpp_template::greeting({}));
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        std::cerr << "empty name: wrong exception type\n";
        return false;
    }

    std::cerr << "empty name: expected std::invalid_argument\n";
    return false;
}

} // namespace

int main()
{
    int failures = 0;
    if (!expect_equal("normal name", cpp_template::greeting("World"), "Hello, World!")) {
        ++failures;
    }
    if (!expect_equal("bounded string view", cpp_template::greeting(std::string_view{"Alice_extra", 5}), "Hello, Alice!")) {
        ++failures;
    }
    if (!expect_equal("embedded null", cpp_template::greeting(std::string_view{"A\0B", 3}), std::string{"Hello, A\0B!", 11})) {
        ++failures;
    }
    if (!rejects_empty_name()) {
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
