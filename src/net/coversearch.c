#include "net/coversearch.h"
#include "net/url.h"
#include "core/strutil.h"
#include <cJSON.h>
#include <stdio.h>
#include <math.h>

int coversearch_url(const char *title, char *out, size_t cap)
{
	char q[1024];
	if (!title || !*title || url_encode_component(q, sizeof q, title) < 0) return -1;
	int n = snprintf(out, cap, "https://openlibrary.org/search.json?title=%s&fields=key,title,author_name,cover_i&limit=12", q);
	return n >= 0 && (size_t)n < cap ? 0 : -1;
}

int coversearch_image_url(int64_t id, char *out, size_t cap)
{
	if (id <= 0) return -1;
	int n = snprintf(out, cap, "https://covers.openlibrary.org/b/id/%lld-M.jpg?default=false", (long long)id);
	return n >= 0 && (size_t)n < cap ? 0 : -1;
}

int coversearch_parse(const char *json, size_t len, cover_candidate_t *out, int max)
{
	if (!json || !out || max <= 0) return -1;
	cJSON *root = cJSON_ParseWithLength(json, len);
	if (!root) return -1;
	cJSON *docs = cJSON_GetObjectItemCaseSensitive(root, "docs"), *d;
	int n = 0;
	if (!cJSON_IsArray(docs)) { cJSON_Delete(root); return -1; }
	cJSON_ArrayForEach(d, docs) {
		cJSON *id = cJSON_GetObjectItemCaseSensitive(d, "cover_i");
		cJSON *title = cJSON_GetObjectItemCaseSensitive(d, "title");
		if (!cJSON_IsNumber(id) || id->valuedouble <= 0 || id->valuedouble > 2147483647 ||
		    !isfinite(id->valuedouble) || !cJSON_IsString(title) || !title->valuestring[0]) continue;
		cJSON *authors = cJSON_GetObjectItemCaseSensitive(d, "author_name");
		cJSON *author = cJSON_GetArrayItem(authors, 0);
		cJSON *key = cJSON_GetObjectItemCaseSensitive(d, "key");
		str_copy(out[n].title, sizeof out[n].title, title->valuestring);
		str_copy(out[n].author, sizeof out[n].author, cJSON_IsString(author) ? author->valuestring : "");
		str_copy(out[n].key, sizeof out[n].key, cJSON_IsString(key) ? key->valuestring : "");
		out[n++].cover_id = (int64_t)id->valuedouble;
		if (n >= max) break;
	}
	cJSON_Delete(root);
	return n;
}
