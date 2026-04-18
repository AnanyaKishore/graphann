#pragma once

#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>

// Two-pass Vamana builder:
//   - medoid-like start node
//   - random seeded initial graph
//   - pass 1 with alpha1 (typically 1.0)
//   - pass 2 with alpha2 (typically > 1.0)
//
// The saved graph format matches VamanaIndex::save(), so the existing
// search binary can load and evaluate this index without changes.
class VamanaTwoPassIndex {
  public:
    VamanaTwoPassIndex() = default;
    ~VamanaTwoPassIndex();

    void build(const std::string& data_path, uint32_t R, uint32_t L,
               float alpha1, float alpha2, float gamma,
               uint32_t seed_degree = 8);

    void save(const std::string& path) const;

  private:
    using Candidate = std::pair<float, uint32_t>;

    std::pair<std::vector<Candidate>, uint32_t>
    greedy_search(const float* query, uint32_t L) const;

    void robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                      float alpha, uint32_t R);

    uint32_t compute_medoid_start() const;
    void init_random_seed_graph(uint32_t seed_degree, std::mt19937& rng);
    void run_insertion_pass(const std::vector<uint32_t>& perm, uint32_t R,
                            uint32_t L, float alpha, uint32_t gamma_R,
                            uint32_t pass_id);

    const float* get_vector(uint32_t id) const {
        return data_ + static_cast<size_t>(id) * dim_;
    }

    float* data_ = nullptr;
    uint32_t npts_ = 0;
    uint32_t dim_ = 0;
    bool owns_data_ = false;

    std::vector<std::vector<uint32_t>> graph_;
    uint32_t start_node_ = 0;

    mutable std::vector<std::mutex> locks_;
};
