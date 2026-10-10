/* Local, opt-in HTTP uploads. A single streaming writer keeps Vita memory bounded. */
#include "net/wifi_import.h"
#include "core/affinity.h"
#include "core/paths.h"
#include "lib/formats.h"
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/rng.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

#define BACKUPS PATH_DATA_ROOT "/backups"
#define PART PATH_DATA_ROOT "/wifi-upload.part"
#define CHUNK 65536
#define HEADER 8192
#define MAX_UPLOAD (2ULL * 1024 * 1024 * 1024)
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static wifi_import_state_t g_state;
static int g_started, g_stop, g_completed;

static const char PAGE[] =
"<!doctype html><html><meta name=viewport content='width=device-width,initial-scale=1'>"
"<meta charset=utf-8><title>NoctoPage uploads</title><style>"
"body{font:16px system-ui;background:#111820;color:#f0ebe0;max-width:620px;margin:40px auto;padding:20px}"
"input,button{font:inherit;padding:12px;margin:8px 0;box-sizing:border-box;width:100%;border-radius:8px}"
"button{background:#4ecab5;border:0}progress{width:100%}</style>"
"<h1>NoctoPage</h1><p>Send books to your Vita. Keep this screen open on the Vita.</p>"
"<label>Pairing code shown on Vita<input id=code inputmode=numeric maxlength=6 autocomplete=off></label>"
"<label>Series folder (optional)<input id=series placeholder='Tokyo Ghoul'></label>"
"<label>Books or NoctoPage .npb backups<input id=files type=file multiple></label>"
"<button id=send>Upload files</button><progress id=p value=0 max=1></progress><pre id=status></pre>"
"<p>Books go into ux0:books. Backups go into the app backup folder. Existing files are kept.</p>"
"<script>const send=document.getElementById('send'),files=document.getElementById('files'),code=document.getElementById('code'),series=document.getElementById('series'),p=document.getElementById('p'),status=document.getElementById('status');send.onclick=async()=>{send.disabled=true;let fs=[...files.files];status.textContent='';"
"try{if(!/^\\d{6}$/.test(code.value))throw Error('Enter the six-digit Vita code');"
"for(let i=0;i<fs.length;i++){let f=fs[i];status.textContent+='Uploading '+f.name+'\\n';"
"await new Promise((ok,bad)=>{let x=new XMLHttpRequest();x.open('PUT','/upload?name='+encodeURIComponent(f.name)+'&series='+encodeURIComponent(series.value));"
"x.setRequestHeader('X-Noctopage-Code',code.value);x.upload.onprogress=e=>{if(e.lengthComputable)p.value=(i+e.loaded/e.total)/fs.length};"
"x.onload=()=>x.status===201?ok():bad(Error(x.responseText||'Upload failed'));"
"x.onerror=()=>bad(Error('Connection lost; check Vita Wi-Fi'));x.timeout=3600000;"
"x.ontimeout=()=>bad(Error('Upload timed out'));x.send(f)});status.textContent+='Saved '+f.name+'\\n'}"
"}catch(e){status.textContent+=e.message+'\\n'}finally{send.disabled=false}}</script></html>";

static uint64_t now(void) { return sceKernelGetProcessTimeWide() / 1000; }
static int stopping(void) { return __atomic_load_n(&g_stop, __ATOMIC_ACQUIRE); }
static int again(void)
{
	int e = *sceNetErrnoLoc();
	return e == SCE_NET_EAGAIN || e == SCE_NET_EINTR;
}
static void message(const char *s)
{
	pthread_mutex_lock(&g_mu);
	snprintf(g_state.message, sizeof g_state.message, "%s", s);
	pthread_mutex_unlock(&g_mu);
}
static int nonblock(int s)
{
	int one = 1;
	return sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof one);
}
static int send_all(int s, const void *data, size_t len)
{
	const char *p = data; uint64_t deadline = now() + 10000;
	while (len && !stopping()) {
		int n = sceNetSend(s, p, (unsigned)len, 0);
		if (n > 0) { p += n; len -= n; deadline = now() + 10000; }
		else if (n < 0 && again() && now() < deadline) sceKernelDelayThread(10000);
		else return -1;
	}
	return len ? -1 : 0;
}
static void response(int s, int code, const char *type, const char *body)
{
	char h[320];
	snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
	         "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
	         code, code == 200 ? "OK" : code == 201 ? "Created" : "Request failed", type, (unsigned)strlen(body));
	if (!send_all(s, h, strlen(h))) send_all(s, body, strlen(body));
}
static int hex(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	c = (char)tolower((unsigned char)c);
	return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
/* Decoding is bounded and forbids mount names, hidden/traversal components and
 * control characters. Only the optional series field can contain '/' separators. */
static int decode(const char *s, size_t len, char *out, size_t cap, int folders)
{
	size_t n = 0, component = 0; int depth = 1;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '%') {
			if (i + 2 >= len || hex(s[i+1]) < 0 || hex(s[i+2]) < 0) return -1;
			c = (unsigned char)(hex(s[i+1]) * 16 + hex(s[i+2])); i += 2;
		} else if (c == '+') c = ' ';
		if (c < 32 || c == 127 || c == ':' || c == '\\' || c == '?' || c == '*' || c == '"' || c == '<' || c == '>' || c == '|') return -1;
		if (c == '/') {
			if (!folders || !component || out[n-1] == ' ' || out[n-1] == '.' || ++depth > 8) return -1;
			component = 0;
		} else {
			if (!component && (c == '.' || c == ' ')) return -1;
			if (++component > 200) return -1;
		}
		if (n + 1 >= cap) return -1;
		out[n++] = (char)c;
	}
	if (n && (!component || out[n-1] == '.' || out[n-1] == ' ')) return -1;
	out[n] = 0; return 0;
}
static int query(const char *url, const char *key, char *out, size_t cap, int folder)
{
	const char *s = strchr(url, '?'); out[0] = 0;
	if (!s) return 0;
	s++;
	while (*s) {
		const char *end = strchr(s, '&'); if (!end) end = s + strlen(s);
		const char *eq = memchr(s, '=', (size_t)(end-s));
		if (eq && (size_t)(eq-s) == strlen(key) && !strncmp(s,key,(size_t)(eq-s)))
			return decode(eq+1, (size_t)(end-eq-1), out, cap, folder);
		s = *end ? end+1 : end;
	}
	return 0;
}
static void serve(int s)
{
	char header[HEADER + 1], method[12], url[1400], protocol[16];
	size_t used = 0, body_at = 0; uint64_t deadline = now() + 10000;
	while (!stopping() && used < HEADER) {
		int n = sceNetRecv(s, header+used, HEADER-(unsigned)used, 0);
		if (n > 0) {
			used += n; header[used] = 0;
			char *end = strstr(header,"\r\n\r\n");
			if (end) { body_at = (size_t)(end-header)+4; *end = 0; break; }
		} else if (n < 0 && again() && now() < deadline) sceKernelDelayThread(10000);
		else return;
	}
	if (!body_at || sscanf(header,"%11s %1399s %15s",method,url,protocol) != 3) { response(s,400,"text/plain","Invalid request"); return; }
	if (!strcmp(method,"GET") && !strcmp(url,"/")) { response(s,200,"text/html; charset=utf-8",PAGE); return; }
	if (strcmp(method,"PUT") || strncmp(url,"/upload?",8)) { response(s,404,"text/plain","Not found"); return; }
	char pin[32] = ""; uint64_t total = 0; int lengths = 0, pins = 0, bad = 0;
	char *line = strstr(header,"\r\n");
	while (line && *(line += 2)) {
		char *end = strstr(line,"\r\n"); if (end) *end = 0;
		if (!strncasecmp(line,"Content-Length:",15)) {
			char *v = line+15, *tail; while (*v == ' ') v++;
			if (!isdigit((unsigned char)*v)) bad = 1;
			total = strtoull(v,&tail,10); while (*tail == ' ' || *tail == '\t') tail++;
			if (*tail) bad = 1;
			lengths++;
		} else if (!strncasecmp(line,"Transfer-Encoding:",18)) bad = 1;
		else if (!strncasecmp(line,"X-Noctopage-Code:",17)) {
			char *v = line+17; while (*v == ' ' || *v == '\t') v++;
			snprintf(pin,sizeof pin,"%s",v); pins++;
		}
		line = end;
	}
	if (pins != 1 || strcmp(pin,g_state.code)) { response(s,403,"text/plain","Pairing code does not match the Vita"); return; }
	if (bad || lengths != 1 || !total || total > MAX_UPLOAD) { response(s,413,"text/plain","Use a non-empty file smaller than 2 GiB"); return; }
	char name[256], series[256], dir[512], dest[768];
	if (query(url,"name",name,sizeof name,0) < 0 || !name[0] || query(url,"series",series,sizeof series,1) < 0) {
		response(s,400,"text/plain","Invalid file or series folder name"); return;
	}
	const char *ext = strrchr(name,'.'); int backup = ext && !strcasecmp(ext,".npb");
	if (!backup && !formats_kind_for_path(name)) { response(s,415,"text/plain","This book format is not supported"); return; }
	snprintf(dir,sizeof dir,"%s%s%s",backup ? BACKUPS : PATH_BOOKS_DIR,!backup && series[0] ? "/" : "",backup ? "" : series);
	snprintf(dest,sizeof dest,"%s/%s",dir,name);
	if (strlen(dest) >= 512) { response(s,400,"text/plain","File path too long"); return; }
	struct stat st;
	if (!stat(dest,&st)) { response(s,409,"text/plain","A file with that name already exists"); return; }
	if (paths_mkdir_p(dir) < 0) { response(s,500,"text/plain","Could not create destination folder"); return; }
	FILE *f = fopen(PART,"wb");
	if (!f) { response(s,500,"text/plain","Could not create upload file"); return; }
	char *buf = malloc(CHUNK);
	if (!buf) { fclose(f); unlink(PART); response(s,500,"text/plain","Not enough memory"); return; }
	setvbuf(f,NULL,_IOFBF,CHUNK);
	pthread_mutex_lock(&g_mu);
	g_state.bytes = 0; g_state.total = total; snprintf(g_state.file,sizeof g_state.file,"%s",name);
	snprintf(g_state.message,sizeof g_state.message,"Receiving file...");
	pthread_mutex_unlock(&g_mu);
	uint64_t bytes = used-body_at; int ok = bytes <= total && fwrite(header+body_at,1,(size_t)bytes,f) == bytes;
	pthread_mutex_lock(&g_mu); g_state.bytes = bytes; pthread_mutex_unlock(&g_mu);
	deadline = now()+60000;
	while (ok && bytes < total && !stopping()) {
		unsigned want = total-bytes < CHUNK ? (unsigned)(total-bytes) : CHUNK;
		int n = sceNetRecv(s,buf,want,0);
		if (n > 0) {
			ok = fwrite(buf,1,(size_t)n,f) == (size_t)n; bytes += n; deadline = now()+60000;
			pthread_mutex_lock(&g_mu); g_state.bytes = bytes; pthread_mutex_unlock(&g_mu);
		} else if (n < 0 && again() && now() < deadline) sceKernelDelayThread(10000);
		else ok = 0;
	}
	free(buf); if (fclose(f)) ok = 0;
	/* A final existence check also catches files copied over FTP during the upload. */
	if (stopping() || bytes != total || !stat(dest,&st)) ok = 0;
	if (ok && rename(PART,dest)) ok = 0;
	if (!ok) { unlink(PART); message("Upload failed or cancelled; partial file removed"); response(s,500,"text/plain","Upload failed; check free storage and connection"); return; }
	pthread_mutex_lock(&g_mu);
	g_state.imported++; g_state.bytes = bytes;
	if (!backup) g_completed++;
	snprintf(g_state.message,sizeof g_state.message,backup ? "Backup received. Open Restore in Settings." : "Book received; your shelf will refresh.");
	pthread_mutex_unlock(&g_mu);
	response(s,201,"text/plain","Saved");
}
static void *worker(void *ud)
{
	int listener = *(int *)ud; free(ud);
	affinity_use_all_cores();
	while (!stopping()) {
		int s = sceNetAccept(listener,NULL,NULL);
		if (s >= 0) { if (nonblock(s) >= 0) serve(s); sceNetSocketClose(s); }
		else if (again()) sceKernelDelayThread(20000);
		else { message("Wi-Fi listener stopped. Reopen uploads to retry."); break; }
	}
	sceNetSocketClose(listener);
	pthread_mutex_lock(&g_mu); g_state.running = 0; pthread_mutex_unlock(&g_mu);
	return NULL;
}
int wifi_import_start(void)
{
	wifi_import_stop();
	pthread_mutex_lock(&g_mu); memset(&g_state,0,sizeof g_state); pthread_mutex_unlock(&g_mu);
	SceNetCtlInfo info; uint32_t random;
	memset(&info,0,sizeof info);
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS,&info) < 0 || !info.ip_address[0] ||
	    !strcmp(info.ip_address,"0.0.0.0")) { message("Connect the Vita to Wi-Fi, then try again."); return -1; }
	if (sceKernelGetRandomNumber(&random,sizeof random) < 0) { message("Could not create a pairing code."); return -1; }
	int s = sceNetSocket("NoctoPage upload",SCE_NET_AF_INET,SCE_NET_SOCK_STREAM,0);
	if (s < 0) { message("Could not open upload listener."); return -1; }
	int one = 1; sceNetSetsockopt(s,SCE_NET_SOL_SOCKET,SCE_NET_SO_REUSEADDR,&one,sizeof one);
	SceNetSockaddrIn addr; memset(&addr,0,sizeof addr);
	addr.sin_len = sizeof addr; addr.sin_family = SCE_NET_AF_INET; addr.sin_port = sceNetHtons(8080);
	if (nonblock(s) < 0 || sceNetBind(s,(SceNetSockaddr *)&addr,sizeof addr) < 0 || sceNetListen(s,2) < 0) {
		sceNetSocketClose(s); message("Could not listen on port 8080."); return -1;
	}
	int *arg = malloc(sizeof *arg);
	if (!arg) { sceNetSocketClose(s); message("Not enough memory to start uploads."); return -1; }
	*arg = s;
	pthread_mutex_lock(&g_mu);
	memset(&g_state,0,sizeof g_state); g_state.running = 1;
	snprintf(g_state.url,sizeof g_state.url,"http://%s:8080",info.ip_address);
	snprintf(g_state.code,sizeof g_state.code,"%06u",(unsigned)(random%1000000));
	snprintf(g_state.message,sizeof g_state.message,"Ready for uploads on your local Wi-Fi.");
	pthread_mutex_unlock(&g_mu);
	__atomic_store_n(&g_stop,0,__ATOMIC_RELEASE);
	pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,256*1024);
	int rc = pthread_create(&g_thread,&attr,worker,arg); pthread_attr_destroy(&attr);
	if (rc) { free(arg); sceNetSocketClose(s); message("Could not start upload worker."); pthread_mutex_lock(&g_mu); g_state.running=0; pthread_mutex_unlock(&g_mu); return -1; }
	g_started = 1; return 0;
}
void wifi_import_stop(void)
{
	if (!g_started) return;
	__atomic_store_n(&g_stop,1,__ATOMIC_RELEASE);
	pthread_join(g_thread,NULL); g_started = 0;
}
void wifi_import_state(wifi_import_state_t *out)
{
	pthread_mutex_lock(&g_mu); *out = g_state; pthread_mutex_unlock(&g_mu);
}
int wifi_import_pump(void)
{
	pthread_mutex_lock(&g_mu); int n = g_completed; g_completed = 0; pthread_mutex_unlock(&g_mu); return n;
}
