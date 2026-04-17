#include "spp_gsp_tiered.h"

#define SPP_BWC_VARIANT_TAG "BWC_GSP_TIERED"
#define SPP_BWC_ENABLE_GLOBAL_GSP 1
#define SPP_BWC_ENABLE_TIERED_GLOBAL 1
#define spp_bwc spp_gsp_tiered
#include "../spp_bwc/spp_bwc_impl.inc"
#undef spp_bwc
#undef SPP_BWC_ENABLE_TIERED_GLOBAL
#undef SPP_BWC_ENABLE_GLOBAL_GSP
#undef SPP_BWC_VARIANT_TAG
