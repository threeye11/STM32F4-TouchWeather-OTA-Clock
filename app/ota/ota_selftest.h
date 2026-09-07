#ifndef __OTA_SELFTEST_H
#define __OTA_SELFTEST_H

/* OTA 阶段 1 上电自检：W25Q64 驱动 + 标志区
 * 联调完成后把 ota_selftest.c 里的 OTA_SELFTEST_EN 置 0 即可关闭 */
void OTA_SelfTest(void);

#endif /* __OTA_SELFTEST_H */
