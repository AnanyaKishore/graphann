#include "vamana_two_pass_index.h"

#include "distance.h"
#include "io_utils.h"
#include "timer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <cstdlib>
#include <omp.h>

VamanaTwoPassIndex::~VamanaTwoPassIndex() {
    if (owns_data_ && data_) {
        std::free(data_);
        data_ = nullptr;
    }
}

std::pair<std::vector<VamanaTwoPassIndex::Candidate>, uint32_t>
VamanaTwoPassIndex::greedy_search(const float* query, uint32_t L) const {
    std::set<Candidate> candidate_set;
    std::vector<bool> visited(npts_, false);
    std::set<uint32_t> expanded;

    uint32_t dist_cmps = 0;

    float start_dist = compute_l2sq(query, get_vector(start_node_), dim_);
    dist_cmps++;
    candidate_set.insert({start_dist, start_node_});
    visited[start_node_] = true;

    while (true) {
        uint32_t best_node = UINT32_MAX;
        for (const auto& cand : candidate_set) {
            if (expanded.find(cand.second) == expanded.end()) {
                best_node = cand.second;
                break;
            }
        }
        if (best_node == UINT32_MAX) {
            break;
        }
        expanded.insert(best_node);

        std::vector<uint32_t> neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[best_node]);
            neighbors = graph_[best_node];
        }

        for (uint32_t nbr : neighbors) {
            if (visited[nbr]) {
                continue;
            }
            visited[nbr] = true;

            float d = compute_l2sq(query, get_vector(nbr), dim_);
            dist_cmps++;

            if (candidate_set.size() < L) {
                candidate_set.insert({d, nbr});
            } else {
                auto worst = std::prev(candidate_set.end());
                if (d < worst->first) {
                    candidate_set.erase(worst);
                    candidate_set.insert({d, nbr});
                }
            }
        }
    }

    std::vector<Candidate> results(candidate_set.begin(), candidate_set.end());
    return {results, dist_cmps};
}

void VamanaTwoPassIndex::robust_prune(uint32_t node,
                                      std::vector<Candidate>& candidates,
                                      float alpha, uint32_t R) {
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [node](const Candidate& c) { return c.second == node; }),
        candidates.end());

    std::sort(candidates.begin(), candidates.end());

    std::vector<uint32_t> new_neighbors;
    new_neighbors.reserve(R);

    for (const auto& cand : candidates) {
        if (new_neighbors.size() >= R) {
            break;
        }

        float dist_to_node = cand.first;
        uint32_t cand_id = cand.second;

        bool keep = true;
        for (uint32_t selected : new_neighbors) {
            float dist_cand_to_selected =
                compute_l2sq(get_vector(cand_id), get_vector(selected), dim_);
            if (dist_to_node > alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }
        }

        if (keep) {
            new_neighbors.push_back(cand_id);
        }
    }

    graph_[node] = std::move(new_neighbors);
}

uint32_t VamanaTwoPassIndex::compute_medoid_start() const {
    std::vector<double> centroid(dim_, 0.0);
    for (uint32_t i = 0; i < npts_; i++) {
        const float* x = get_vector(i);
        for (uint32_t d = 0; d < dim_; d++) {
            centroid[d] += x[d];
        }
    }

    for (uint32_t d = 0; d < dim_; d++) {
        centroid[d] /= static_cast<double>(npts_);
    }

    uint32_t best_id = 0;
    double best_dist = std::numeric_limits<double>::infinity();
    for (uint32_t i = 0; i < npts_; i++) {
        const float* x = get_vector(i);
        double acc = 0.0;
        for (uint32_t d = 0; d < dim_; d++) {
            const double diff = static_cast<double>(x[d]) - centroid[d];
            acc += diff * diff;
        }
        if (acc < best_dist) {
            best_dist = acc;
            best_id = i;
        }
    }

    return best_id;
}

void VamanaTwoPassIndex::init_random_seed_graph(uint32_t seed_degree,
                                                 std::mt19937& rng) {
    if (seed_degree == 0 || npts_ <= 1) {
        return;
    }

    seed_degree = std::min(seed_degree, npts_ - 1);

    const uint32_t base_seed = rng();

    #pragma omp parallel
    {
        std::mt19937 local_rng(base_seed + static_cast<uint32_t>(omp_get_thread_num()));

        #pragma omp for schedule(static)
        for (uint32_t i = 0; i < npts_; i++) {
            std::unordered_set<uint32_t> chosen;
            chosen.reserve(seed_degree * 2);

            while (chosen.size() < seed_degree) {
                uint32_t j = local_rng() % npts_;
                if (j != i) {
                    chosen.insert(j);
                }
            }

            auto& nbrs = graph_[i];
            nbrs.assign(chosen.begin(), chosen.end());
        }
    }
}

void VamanaTwoPassIndex::run_insertion_pass(const std::vector<uint32_t>& perm,
                                            uint32_t R, uint32_t L, float alpha,
                                            uint32_t gamma_R, uint32_t pass_id) {
    std::cout << "Pass " << pass_id << " build (alpha=" << alpha << ")..." << std::endl;

    Timer pass_timer;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t idx = 0; idx < npts_; idx++) {
        uint32_t point = perm[idx];

        auto search_result = greedy_search(get_vector(point), L);
        auto& candidates = search_result.first;

        robust_prune(point, candidates, alpha, R);

        std::vector<uint32_t> local_neighbors = graph_[point];
        for (uint32_t nbr : local_neighbors) {
            std::lock_guard<std::mutex> lock(locks_[nbr]);

            graph_[nbr].push_back(point);

            if (graph_[nbr].size() > gamma_R) {
                std::vector<Candidate> nbr_candidates;
                nbr_candidates.reserve(graph_[nbr].size());
                for (uint32_t nn : graph_[nbr]) {
                    float d = compute_l2sq(get_vector(nbr), get_vector(nn), dim_);
                    nbr_candidates.push_back({d, nn});
                }
                robust_prune(nbr, nbr_candidates, alpha, R);
            }
        }

        if (idx % 10000 == 0) {
            #pragma omp critical
            {
                std::cout << "\r  Pass " << pass_id << ": inserted " << idx << " / " << npts_
                          << " points" << std::flush;
            }
        }
    }

    double pass_time = pass_timer.elapsed_seconds();
    std::cout << "\n  Pass " << pass_id << " complete in " << pass_time << " seconds."
              << std::endl;
}

void VamanaTwoPassIndex::build(const std::string& data_path, uint32_t R,
                               uint32_t L, float alpha1, float alpha2,
                               float gamma, uint32_t seed_degree) {
    std::cout << "Loading data from " << data_path << "..." << std::endl;
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_ = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    std::cout << "  Points: " << npts_ << ", Dimensions: " << dim_ << std::endl;

    if (L < R) {
        std::cerr << "Warning: L (" << L << ") < R (" << R
                  << "). Setting L = R." << std::endl;
        L = R;
    }

    graph_.clear();
    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    std::mt19937 rng(42);

    start_node_ = compute_medoid_start();
    std::cout << "  Start node (medoid-like): " << start_node_ << std::endl;

    init_random_seed_graph(seed_degree, rng);
    std::cout << "  Seeded random graph with degree " << std::min(seed_degree, npts_ > 0 ? npts_ - 1 : 0)
              << std::endl;

    std::vector<uint32_t> perm(npts_);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);
    if (gamma_R < R) {
        gamma_R = R;
    }

    std::cout << "Building two-pass index (R=" << R << ", L=" << L
              << ", alpha1=" << alpha1 << ", alpha2=" << alpha2
              << ", gamma=" << gamma << ", gammaR=" << gamma_R << ")..."
              << std::endl;

    Timer build_timer;
    run_insertion_pass(perm, R, L, alpha1, gamma_R, 1);

    // Re-shuffle between passes to reduce ordering artifacts.
    std::shuffle(perm.begin(), perm.end(), rng);
    run_insertion_pass(perm, R, L, alpha2, gamma_R, 2);

    const double build_time = build_timer.elapsed_seconds();

    size_t total_edges = 0;
    for (uint32_t i = 0; i < npts_; i++) {
        total_edges += graph_[i].size();
    }
    const double avg_degree = (npts_ == 0)
                                  ? 0.0
                                  : static_cast<double>(total_edges) / npts_;

    std::cout << "  Two-pass build complete in " << build_time << " seconds." << std::endl;
    std::cout << "  Average out-degree: " << avg_degree << std::endl;
}

void VamanaTwoPassIndex::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }

    out.write(reinterpret_cast<const char*>(&npts_), 4);
    out.write(reinterpret_cast<const char*>(&dim_), 4);
    out.write(reinterpret_cast<const char*>(&start_node_), 4);

    for (uint32_t i = 0; i < npts_; i++) {
        const uint32_t deg = graph_[i].size();
        out.write(reinterpret_cast<const char*>(&deg), 4);
        if (deg > 0) {
            out.write(reinterpret_cast<const char*>(graph_[i].data()),
                      static_cast<std::streamsize>(deg * sizeof(uint32_t)));
        }
    }

    std::cout << "Index saved to " << path << std::endl;
}
