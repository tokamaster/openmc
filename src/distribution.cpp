#include "distribution.h"

#include <algorithm> // for copy
#include <cmath>     // for sqrt, floor, max
#include <iterator>  // for back_inserter
#include <numeric>   // for accumulate
#include <string>    // for string, stod

#include "error.h"
#include "math_functions.h"
#include "random_lcg.h"
#include "xml_interface.h"

namespace openmc {

//==============================================================================
// Discrete implementation
//==============================================================================

Discrete::Discrete(pugi::xml_node node)
{
  auto params = get_node_array<double>(node, "parameters");

  std::size_t n = params.size();
  std::copy(params.begin(), params.begin() + n/2, std::back_inserter(x_));
  std::copy(params.begin() + n/2, params.end(), std::back_inserter(p_));

  normalize();
}

Discrete::Discrete(const double* x, const double* p, int n)
  : x_{x, x+n}, p_{p, p+n}
{
  normalize();
}

double Discrete::sample() const
{
  int n = x_.size();
  if (n > 1) {
    double xi = prn();
    double c = 0.0;
    for (int i = 0; i < n; ++i) {
      c += p_[i];
      if (xi < c) return x_[i];
    }
    // throw exception?
  } else {
    return x_[0];
  }
}

void Discrete::normalize()
{
  // Renormalize density function so that it sums to unity
  double norm = std::accumulate(p_.begin(), p_.end(), 0.0);
  for (auto& p_i : p_)
    p_i /= norm;
}

//==============================================================================
// Uniform implementation
//==============================================================================

Uniform::Uniform(pugi::xml_node node)
{
  auto params = get_node_array<double>(node, "parameters");
  if (params.size() != 2)
    openmc::fatal_error("Uniform distribution must have two "
                        "parameters specified.");

  a_ = params.at(0);
  b_ = params.at(1);
}

double Uniform::sample() const
{
  return a_ + prn()*(b_ - a_);
}

//==============================================================================
// Maxwell implementation
//==============================================================================

Maxwell::Maxwell(pugi::xml_node node)
{
  theta_ = std::stod(get_node_value(node, "parameters"));
}

double Maxwell::sample() const
{
  return maxwell_spectrum_c(theta_);
}

//==============================================================================
// Watt implementation
//==============================================================================

Watt::Watt(pugi::xml_node node)
{
  auto params = get_node_array<double>(node, "parameters");
  if (params.size() != 2)
    openmc::fatal_error("Watt energy distribution must have two "
                        "parameters specified.");

  a_ = params.at(0);
  b_ = params.at(1);
}

double Watt::sample() const
{
  return watt_spectrum_c(a_, b_);
}

//==============================================================================
// Tabular implementation
//==============================================================================

Tabular::Tabular(pugi::xml_node node)
{
  if (check_for_node(node, "interpolation")) {
    std::string temp = get_node_value(node, "interpolation");
    if (temp == "histogram") {
      interp_ = Interpolation::histogram;
    } else if (temp == "linear-linear") {
      interp_ = Interpolation::lin_lin;
    } else {
      openmc::fatal_error("Unknown interpolation type for distribution: " + temp);
    }
  } else {
    interp_ = Interpolation::histogram;
  }

  // Read and initialize tabular distribution
  auto params = get_node_array<double>(node, "parameters");
  std::size_t n = params.size() / 2;
  const double* x = params.data();
  const double* p = x + n;
  init(x, p, n);
}

Tabular::Tabular(const double* x, const double* p, int n, Interpolation interp, const double* c)
  : interp_{interp}
{
  init(x, p, n, c);
}

void Tabular::init(const double* x, const double* p, std::size_t n, const double* c)
{
  // Copy x/p arrays into vectors
  std::copy(x, x + n, std::back_inserter(x_));
  std::copy(p, p + n, std::back_inserter(p_));

  // Check interpolation parameter
  if (interp_ != Interpolation::histogram &&
      interp_ != Interpolation::lin_lin) {
    openmc::fatal_error("Only histogram and linear-linear interpolation "
                        "for tabular distribution is supported.");
  }

  // Calculate cumulative distribution function
  if (c) {
    std::copy(c, c + n, std::back_inserter(c_));
  } else {
    c_.resize(n);
    c_[0] = 0.0;
    for (int i = 1; i < n; ++i) {
      if (interp_ == Interpolation::histogram) {
        c_[i] = c_[i-1] + p_[i-1]*(x_[i] - x_[i-1]);
      } else if (interp_ == Interpolation::lin_lin) {
        c_[i] = c_[i-1] + 0.5*(p_[i-1] + p_[i]) * (x_[i] - x_[i-1]);
      }
    }
  }

  // Normalize density and distribution functions
  for (int i = 0; i < n; ++i) {
    p_[i] = p_[i]/c_[n-1];
    c_[i] = c_[i]/c_[n-1];
  }
}

double Tabular::sample() const
{
  // Sample value of CDF
  double c = prn();

  // Find first CDF bin which is above the sampled value
  double c_i = c_[0];
  int i;
  std::size_t n = c_.size();
  for (i = 0; i < n - 1; ++i) {
    if (c <= c_[i+1]) break;
    c_i = c_[i+1];
  }

  // Determine bounding PDF values
  double x_i = x_[i];
  double p_i = p_[i];

  if (interp_ == Interpolation::histogram) {
    // Histogram interpolation
    if (p_i > 0.0) {
      return x_i + (c - c_i)/p_i;
    } else {
      return x_i;
    }
  } else {
    // Linear-linear interpolation
    double x_i1 = x_[i + 1];
    double p_i1 = p_[i + 1];

    double m = (p_i1 - p_i)/(x_i1 - x_i);
    if (m == 0.0) {
      return x_i + (c - c_i)/p_i;
    } else {
      return x_i + (std::sqrt(std::max(0.0, p_i*p_i + 2*m*(c - c_i))) - p_i)/m;
    }
  }
}

//==============================================================================
// Equiprobable implementation
//==============================================================================

double Equiprobable::sample() const
{
  std::size_t n = x_.size();

  double r = prn();
  int i = std::floor((n - 1)*r);

  double xl = x_[i];
  double xr = x_[i+i];
  return xl + ((n - 1)*r - i) * (xr - xl);
}

//==============================================================================
// Helper function
//==============================================================================

UPtrDist distribution_from_xml(pugi::xml_node node)
{
  if (!check_for_node(node, "type"))
    openmc::fatal_error("Distribution type must be specified.");

  // Determine type of distribution
  std::string type = get_node_value(node, "type", true, true);

  // Allocate extension of Distribution
  if (type == "uniform") {
    return UPtrDist{new Uniform(node)};
  } else if (type == "maxwell") {
    return UPtrDist{new Maxwell(node)};
  } else if (type == "watt") {
    return UPtrDist{new Watt(node)};
  } else if (type == "discrete") {
    return UPtrDist{new Discrete(node)};
  } else if (type == "tabular") {
    return UPtrDist{new Tabular(node)};
  } else {
    openmc::fatal_error("Invalid distribution type: " + type);
  }
}

} // namespace openmc
