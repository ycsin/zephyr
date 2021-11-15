#include <time.h>
#include <device.h>

int esp_get_local_time(const struct device *dev, struct tm *tm, int32_t *offset);
