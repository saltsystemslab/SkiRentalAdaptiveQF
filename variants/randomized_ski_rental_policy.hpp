#ifndef RANDOMIZED_SKI_RENTAL_POLICY
#define RANDOMIZED_SKI_RENTAL_POLICY

#include <cmath>
#include <cstdint>
#include <random>

class RandomizedSkiRentalPolicy {
public:
  void seed(uint64_t randomSeed) {
    generator.seed(randomSeed);
  }

  // Karlin et al.'s optimal discrete ski-rental distribution chooses a
  // purchase occurrence K in [1, B] with
  //   P[K=k] = q^(B-k) / (B * (1-q^B)), q = 1 - 1/B.
  // A fingerprint count c tells us that K >= c, so the equivalent online
  // decision adapts with the conditional probability P[K=c | K>=c].
  static double adaptationProbability(uint64_t count, uint64_t breakEven) {
    if (breakEven == 0 || count == 0) {
      return 0.0;
    }
    if (breakEven == 1 || count >= breakEven) {
      return 1.0;
    }

    const double q = 1.0 - 1.0 / static_cast<double>(breakEven);
    const uint64_t remainingRentals = breakEven - count;
    const double numerator =
        (1.0 - q) * std::pow(q, static_cast<double>(remainingRentals));
    const double denominator =
        1.0 - std::pow(q, static_cast<double>(remainingRentals + 1));
    return numerator / denominator;
  }

  bool shouldAdapt(uint64_t count, uint64_t breakEven) {
    return uniform(generator) < adaptationProbability(count, breakEven);
  }

private:
  std::mt19937_64 generator;
  std::uniform_real_distribution<double> uniform{0.0, 1.0};
};

#endif
