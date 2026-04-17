#include "spp_gsp_headgate_keep3.h"

#define SPP_BWC_VARIANT_TAG "BWC_GSP_HEADGATE_KEEP3"
#define SPP_BWC_ENABLE_GLOBAL_GSP 1
#define SPP_BWC_ENABLE_TIERED_GLOBAL 1
#define SPP_BWC_ENABLE_SIG_UTIL 1
#define SPP_BWC_ENABLE_HEADGATE 1
#define SPP_BWC_HEADGATE_KEEP_CANDIDATES 3
#define spp_bwc spp_gsp_headgate_keep3
#include "../spp_bwc/spp_bwc_impl.inc"
#undef spp_bwc
#undef SPP_BWC_HEADGATE_KEEP_CANDIDATES
#undef SPP_BWC_ENABLE_HEADGATE
#undef SPP_BWC_ENABLE_SIG_UTIL
#undef SPP_BWC_ENABLE_TIERED_GLOBAL
#undef SPP_BWC_ENABLE_GLOBAL_GSP
#undef SPP_BWC_VARIANT_TAG
