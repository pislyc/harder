/* HARDER: musl's libm compiled under private names, so the plugin never depends on the speed or
   accuracy of the host C runtime (msvcrt's exp/pow/sin are several times slower than these). */
#ifndef HM_NAMES_H
#define HM_NAMES_H
#define sin hm_sin
#define cos hm_cos
#define tan hm_tan
#define exp hm_exp
#define pow hm_pow
#define log10 hm_log10
#define hypot hm_hypot
#define __sin hm__sin
#define __cos hm__cos
#define __tan hm__tan
#define __rem_pio2 hm__rem_pio2
#define __rem_pio2_large hm__rem_pio2_large
#define __math_oflow hm__math_oflow
#define __math_uflow hm__math_uflow
#define __math_xflow hm__math_xflow
#define __math_invalid hm__math_invalid
#define __math_divzero hm__math_divzero
#define __exp_data hm__exp_data
#define __pow_log_data hm__pow_log_data
#endif
