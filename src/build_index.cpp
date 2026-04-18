#include "vamana_index.h"
#include "vamana_two_pass_index.h"
#include "timer.h"

#include <iostream>
#include <string>
#include <cstdlib>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --data <fbin_path>"
              << " --output <index_path>"
              << " [--builder <two-pass|baseline>]"
              << " [--R <max_degree=32>]"
              << " [--L <build_search_list=75>]"
              << " [--alpha <rng_alpha=1.2>]"
              << " [--alpha1 <pass1_alpha=1.0>]"
              << " [--gamma <degree_multiplier=1.5>]"
              << " [--seed-degree <initial_random_degree=8>]"
              << std::endl;
}

int main(int argc, char** argv) {
    // Defaults
    std::string data_path, output_path;
    uint32_t R = 32;
    uint32_t L = 75;
    float alpha = 1.2f;
    float alpha1 = 1.0f;
    float gamma = 1.5f;
    uint32_t seed_degree = 8;
    std::string builder = "two-pass";

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc)       data_path = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_path = argv[++i];
        else if (arg == "--builder" && i + 1 < argc) builder = argv[++i];
        else if (arg == "--R" && i + 1 < argc)      R = std::atoi(argv[++i]);
        else if (arg == "--L" && i + 1 < argc)      L = std::atoi(argv[++i]);
        else if (arg == "--alpha" && i + 1 < argc)  alpha = std::atof(argv[++i]);
        else if (arg == "--alpha1" && i + 1 < argc) alpha1 = std::atof(argv[++i]);
        else if (arg == "--gamma" && i + 1 < argc)  gamma = std::atof(argv[++i]);
        else if (arg == "--seed-degree" && i + 1 < argc) seed_degree = std::atoi(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (data_path.empty() || output_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "=== Vamana Index Builder ===" << std::endl;
    std::cout << "Parameters:" << std::endl;
    std::cout << "  builder = " << builder << std::endl;
    std::cout << "  R     = " << R << std::endl;
    std::cout << "  L     = " << L << std::endl;
    std::cout << "  alpha = " << alpha << std::endl;
    std::cout << "  alpha1 = " << alpha1 << std::endl;
    std::cout << "  gamma = " << gamma << std::endl;
    std::cout << "  seed_degree = " << seed_degree << std::endl;

    Timer total_timer;

    if (builder == "baseline") {
        VamanaIndex index;
        index.build(data_path, R, L, alpha, gamma);
        index.save(output_path);
    } else if (builder == "two-pass") {
        VamanaTwoPassIndex index;
        index.build(data_path, R, L, alpha1, alpha, gamma, seed_degree);
        index.save(output_path);
    } else {
        std::cerr << "Unknown builder: " << builder
                  << " (expected: two-pass or baseline)" << std::endl;
        return 1;
    }

    double total_time = total_timer.elapsed_seconds();

    std::cout << "\nTotal build time: " << total_time << " seconds" << std::endl;
    std::cout << "Done." << std::endl;
    return 0;
}
