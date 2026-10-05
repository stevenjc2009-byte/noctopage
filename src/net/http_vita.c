/* http_vita - the Vita network bring-up for net/http.c. curl + mbedTLS only need the socket
 * layer (SceNet) and the connection manager (SceNetCtl): TLS and HTTP are done in-process, so
 * the SceSsl / SceHttp modules are not loaded. Vita-only; the host build skips the body. */
#ifdef __vita__

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define NET_POOL_SIZE (1 * 1024 * 1024)
/* sceNetCtlInit when already initialised; not named in the SDK headers */
#define NETCTL_ERROR_NOT_TERMINATED 0x80412102

int http_vita_net_up(void);
void http_vita_net_down(void);

static char g_pool[NET_POOL_SIZE] __attribute__((aligned(64)));
static int g_module, g_net, g_ctl;

int http_vita_net_up(void)
{
	if (!g_module) {
		if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0) return -1;
		g_module = 1;
	}
	if (!g_net) {
		SceNetInitParam p;
		p.memory = g_pool;
		p.size = sizeof g_pool;
		p.flags = 0;
		int r = sceNetInit(&p);
		if (r < 0 && r != (int)SCE_NET_ERROR_EBUSY) return -1; /* EBUSY: already up */
		g_net = r >= 0 ? 1 : -1; /* only terminate what this module started */
	}
	if (!g_ctl) {
		int r = sceNetCtlInit();
		if (r < 0 && r != (int)NETCTL_ERROR_NOT_TERMINATED) return -1;
		g_ctl = r >= 0 ? 1 : -1;
	}
	return 0;
}

void http_vita_net_down(void)
{
	if (g_ctl == 1) sceNetCtlTerm();
	if (g_net == 1) sceNetTerm();
	if (g_module) sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
	g_ctl = g_net = g_module = 0;
}

#endif
