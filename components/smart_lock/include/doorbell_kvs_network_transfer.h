#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

void doorbell_kvs_get_format_utc_ts(char *buf, size_t len);
bk_err_t doorbell_kvs_ntwk_init(char *service_name, void *param);
bk_err_t doorbell_kvs_ntwk_deinit(char *service_name);

#ifdef __cplusplus
}
#endif
