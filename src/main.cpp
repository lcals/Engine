#include "cpp_template/greeting.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char* argv[])
{
    if (argc > 2) {
        std::cerr << "Usage: cpp_template_app [name]\n";
        return 2;
    }

    const std::string_view name = argc == 2 ? std::string_view{argv[1]} : "World";
    try {
        std::cout << cpp_template::greeting(name) << '\n';
    } catch (const std::invalid_argument& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
