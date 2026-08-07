#include <iostream>
#include <vector>
#include <complex>

namespace XDR {
namespace Utils {
double VectorStandardDeviation(const std::vector<double>& vec);
std::vector<double> GetAxis(const std::vector<std::vector<double>>& seq, int axis);
std::vector<std::complex<double>> RFFT(const std::vector<double>& input);
std::vector<double> FFTFreq(int n, double d);
// std::vector<double> GaussianFilter1D(const std::vector<double>& input, double sigma, int axis = -1, int order = 0, 
//     std::vector<double> output = {}, const std::string& mode = "reflect", 
//     double cval = 0.0, double truncate = 4.0, int radius = -1);

} // namespace Utils
} // namespace XDR