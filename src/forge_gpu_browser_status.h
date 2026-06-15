#ifndef SDLGPU_FORGE_GPU_BROWSER_STATUS_H
#define SDLGPU_FORGE_GPU_BROWSER_STATUS_H

void ForgeGpuBrowserSetStatus(const char *message, int frame);
void ForgeGpuBrowserSetNumberMetric(const char *key, double value);
int ForgeGpuBrowserGetValidationMode(void);
int ForgeGpuBrowserHasPointerLock(void);

#endif /* SDLGPU_FORGE_GPU_BROWSER_STATUS_H */
