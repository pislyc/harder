// Private libm (musl, MIT): see hmath/hm_names.h
#pragma once
extern "C" {
double hm_sin(double); double hm_cos(double); double hm_tan(double); double hm_exp(double);
double hm_pow(double, double); double hm_log10(double); double hm_hypot(double, double);
}
