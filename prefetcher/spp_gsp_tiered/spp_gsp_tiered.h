#ifndef SPP_GSP_TIERED_H
#define SPP_GSP_TIERED_H

#ifdef SPP_BWC_H
#define SPP_GSP_TIERED_RESTORE_SPP_BWC_H 1
#undef SPP_BWC_H
#endif

#undef SPP_BWC_VARIANT_TAG
#undef SPP_BWC_ENABLE_GLOBAL_GSP
#undef SPP_BWC_ENABLE_TIERED_GLOBAL

#define SPP_BWC_VARIANT_TAG "BWC_GSP_TIERED"
#define SPP_BWC_ENABLE_GLOBAL_GSP 1
#define SPP_BWC_ENABLE_TIERED_GLOBAL 1
#define spp_bwc spp_gsp_tiered
#include "../spp_bwc/spp_bwc.h"
#undef spp_bwc
#undef SPP_BWC_ENABLE_TIERED_GLOBAL
#undef SPP_BWC_ENABLE_GLOBAL_GSP
#undef SPP_BWC_VARIANT_TAG

#ifdef SPP_GSP_TIERED_RESTORE_SPP_BWC_H
#define SPP_BWC_H
#undef SPP_GSP_TIERED_RESTORE_SPP_BWC_H
#else
#undef SPP_BWC_H
#endif

#endif
