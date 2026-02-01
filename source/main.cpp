#include <pu/Plutonium>
#include <sstream>
#include <switch.h>

#include "PKSMApplication.hpp"
#include "utils/Logger.hpp"

int main(int argc, char* argv[]) {
    try {

        auto app = pksm::PKSMApplication::Initialize();
        if (!app) {
            // Initialization failed, cleanup and exit
            pksm::utils::Logger::Error("Failed to initialize application");
            pksm::utils::Logger::Finalize();
            return 1;
        }
        app->ShowWithFadeIn();

        // Cleanup
        pksm::utils::Logger::Finalize();
        return 0;
    } catch (const std::exception& e) {
        // Make sure we log any fatal errors
        pksm::utils::Logger::Error("Fatal error: " + std::string(e.what()));
        pksm::utils::Logger::Finalize();
        return 1;
    }
}