#include "cli.hpp"

#include <iostream>
#include <system_error>

int main() {
    try {
        sim::Runtime runtime;
        runtime.launch();
        sim::Shell shell{runtime, std::cin, std::cout};
        pthread_t cli;
        int result = pthread_create(&cli, nullptr, sim::cli_entry, &shell);
        if (result) throw std::system_error(result, std::generic_category(), "pthread_create CLI");
        result = pthread_join(cli, nullptr);
        if (result) throw std::system_error(result, std::generic_category(), "pthread_join CLI");
        runtime.shutdown();
        runtime.join();
        const auto error = runtime.error();
        if (!error.empty()) {
            std::cerr << "[ERROR MOTOR]: " << error << '\n';
            return 1;
        }
        return shell.status;
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR]: " << ex.what() << '\n';
        return 1;
    }
}
