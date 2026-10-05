#include "randomized_ski_rental_policy.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool approximatelyEqual(double left, double right) {
  return std::abs(left - right) < 1e-12;
}

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
  }
  return condition;
}

} // namespace

int main() {
  bool passed = true;
  passed &= expect(
      RandomizedSkiRentalPolicy::adaptationProbability(0, 5) == 0.0,
      "count zero must not adapt");
  passed &= expect(
      RandomizedSkiRentalPolicy::adaptationProbability(1, 1) == 1.0,
      "B=1 must adapt immediately");

  passed &= expect(
      approximatelyEqual(
          RandomizedSkiRentalPolicy::adaptationProbability(1, 2),
          1.0 / 3.0),
      "incorrect B=2 first-occurrence probability");
  passed &= expect(
      RandomizedSkiRentalPolicy::adaptationProbability(2, 2) == 1.0,
      "B=2 must adapt on the second occurrence");

  passed &= expect(
      approximatelyEqual(
          RandomizedSkiRentalPolicy::adaptationProbability(1, 5),
          0.1218467396477868),
      "incorrect B=5 first-occurrence probability");
  passed &= expect(
      approximatelyEqual(
          RandomizedSkiRentalPolicy::adaptationProbability(4, 5),
          4.0 / 9.0),
      "incorrect B=5 fourth-occurrence probability");
  passed &= expect(
      RandomizedSkiRentalPolicy::adaptationProbability(5, 5) == 1.0,
      "B=5 must adapt on the fifth occurrence");
  passed &= expect(
      RandomizedSkiRentalPolicy::adaptationProbability(8, 5) == 1.0,
      "counts beyond B must adapt");

  RandomizedSkiRentalPolicy first;
  RandomizedSkiRentalPolicy second;
  first.seed(741262640);
  second.seed(741262640);

  for (uint64_t repetition = 0; repetition < 100; ++repetition) {
    for (uint64_t count = 1; count <= 24; ++count) {
      passed &= expect(
          first.shouldAdapt(count, 24) == second.shouldAdapt(count, 24),
          "equal seeds produced different decisions");
    }
  }

  return passed ? 0 : 1;
}
