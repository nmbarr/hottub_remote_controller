#pragma once

static inline int xTaskCreate(void (*fn)(void *), const char *name, int stack,
                              void *arg, int prio, void *handle)
{
  (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)handle;
  return 1;
}
