#ifndef LOCAL_ACR_VERSION_H
#define LOCAL_ACR_VERSION_H

#include <stdint.h>

#if defined(__GNUC__)
#define LACR_PUBLIC __attribute__((visibility("default")))
#else
#define LACR_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

LACR_PUBLIC uint32_t lacr_version_abi(void);

#ifdef __cplusplus
}
#endif

#endif
