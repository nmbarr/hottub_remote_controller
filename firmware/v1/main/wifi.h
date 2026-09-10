#pragma once

// Blocks until associated with an IP, or until the first connect attempt
// gives up. Retries continue in the background either way.
void wifi_init_sta(void);
