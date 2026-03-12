/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Feishu (Lark) IM integration via long connection (WebSocket).
 *
 * Protocol:
 *   1. POST /callback/ws/endpoint with AppID+AppSecret to get WSS URL
 *   2. Connect WebSocket, receive Protobuf-framed events
 *   3. Forward user messages to ai_chat(), reply via HTTP API
 *
 * Feishu WebSocket frames use Protobuf encoding (Frame + Header).
 * We implement minimal hand-coded Protobuf encode/decode to avoid
 * pulling in a full protobuf library on an embedded target.
 */

#include "claw_os.h"
#include "feishu.h"
#include "ai_engine.h"

#include <string.h>
#include <stdio.h>

#define TAG "feishu"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"

#ifdef CONFIG_CLAW_FEISHU_ENABLE

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

#define FEISHU_APP_ID       CONFIG_CLAW_FEISHU_APP_ID
#define FEISHU_APP_SECRET   CONFIG_CLAW_FEISHU_APP_SECRET

#define WS_EP_URL    "https://open.feishu.cn/callback/ws/endpoint"
#define TOKEN_URL \
    "https://open.feishu.cn/open-apis/auth/v3/" \
    "tenant_access_token/internal"
#define MSG_SEND_URL "https://open.feishu.cn/open-apis/im/v1/messages"

#define TOKEN_BUF_SIZE      256
#define RESP_BUF_SIZE       4096
#define REPLY_BUF_SIZE      4096
#define PING_INTERVAL_MS    (120 * 1000)
#define WS_RECONNECT_MS     5000
#define TOKEN_REFRESH_MS    (90 * 60 * 1000)
#define HTTP_TIMEOUT_MS     15000

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

static char s_token[TOKEN_BUF_SIZE];
static claw_mutex_t s_lock;
static esp_websocket_client_handle_t s_ws_client;
/* Accessed from both WS callback and worker thread */
static volatile int s_ws_connected;

struct http_ctx {
    char   *buf;
    size_t  len;
    size_t  cap;
};

/* ------------------------------------------------------------------ */
/*  Minimal Protobuf helpers for Feishu Frame                          */
/*                                                                     */
/*  Frame {                                                            */
/*    1: uint64 SeqID                                                  */
/*    2: uint64 LogID                                                  */
/*    3: int32  service                                                */
/*    4: int32  method    (0=CONTROL, 1=DATA)                          */
/*    5: Header headers[] (key+value strings)                          */
/*    6: string payload_encoding                                       */
/*    7: string payload_type                                           */
/*    8: bytes  payload                                                */
/*    9: string LogIDNew                                               */
/*  }                                                                  */
/* ------------------------------------------------------------------ */

/* Protobuf wire types */
#define PB_VARINT   0
#define PB_LEN      2

/* Decode a varint, return bytes consumed (0 on error) */
static int pb_decode_varint(const uint8_t *buf, int len, uint64_t *val)
{
    *val = 0;
    int shift = 0;

    for (int i = 0; i < len && i < 10; i++) {
        *val |= (uint64_t)(buf[i] & 0x7F) << shift;
        shift += 7;
        if ((buf[i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/* Encode a varint, return bytes written */
static int pb_encode_varint(uint8_t *buf, uint64_t val)
{
    int n = 0;

    do {
        buf[n] = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val) {
            buf[n] |= 0x80;
        }
        n++;
    } while (val);
    return n;
}

/* Encode field tag */
static int pb_encode_tag(uint8_t *buf, int field, int wire_type)
{
    return pb_encode_varint(buf, (uint64_t)((field << 3) | wire_type));
}

/*
 * Parse a Feishu Frame from raw protobuf bytes.
 * We only extract: method (field 4), headers (field 5), payload (field 8).
 */
struct feishu_frame {
    int          method;
    const char  *type;          /* from header "type" */
    const char  *msg_id;        /* from header "message_id" */
    const uint8_t *payload;
    int          payload_len;
};

static int parse_frame(const uint8_t *buf, int len, struct feishu_frame *f)
{
    memset(f, 0, sizeof(*f));
    f->method = -1;
    int pos = 0;

    while (pos < len) {
        uint64_t tag;
        int n = pb_decode_varint(buf + pos, len - pos, &tag);
        if (n == 0) {
            break;
        }
        pos += n;

        int field = (int)(tag >> 3);
        int wire = (int)(tag & 0x07);

        if (wire == PB_VARINT) {
            uint64_t val;
            n = pb_decode_varint(buf + pos, len - pos, &val);
            if (n == 0) {
                break;
            }
            pos += n;
            if (field == 4) {
                f->method = (int)val;
            }
        } else if (wire == PB_LEN) {
            uint64_t slen;
            n = pb_decode_varint(buf + pos, len - pos, &slen);
            if (n == 0) {
                break;
            }
            pos += n;
            if (pos + (int)slen > len) {
                break;
            }

            if (field == 8) {
                /* payload */
                f->payload = buf + pos;
                f->payload_len = (int)slen;
            } else if (field == 5) {
                /* Embedded Header message: parse key (1) + value (2) */
                const uint8_t *hbuf = buf + pos;
                int hlen = (int)slen;
                const char *key = NULL;
                int key_len = 0;
                const char *val_str = NULL;
                int val_len = 0;
                int hp = 0;

                while (hp < hlen) {
                    uint64_t htag;
                    int hn = pb_decode_varint(hbuf + hp, hlen - hp, &htag);
                    if (hn == 0) {
                        break;
                    }
                    hp += hn;
                    int hfield = (int)(htag >> 3);
                    int hwire = (int)(htag & 0x07);

                    if (hwire == PB_LEN) {
                        uint64_t sl;
                        hn = pb_decode_varint(hbuf + hp, hlen - hp, &sl);
                        if (hn == 0) {
                            break;
                        }
                        hp += hn;
                        if (hfield == 1) {
                            key = (const char *)(hbuf + hp);
                            key_len = (int)sl;
                        } else if (hfield == 2) {
                            val_str = (const char *)(hbuf + hp);
                            val_len = (int)sl;
                        }
                        hp += (int)sl;
                    } else {
                        break;
                    }
                }

                if (key && val_str) {
                    if (key_len == 4 && memcmp(key, "type", 4) == 0) {
                        f->type = val_str;
                        /* null-terminate check not needed, we use val_len */
                        (void)val_len;
                    } else if (key_len == 10 &&
                               memcmp(key, "message_id", 10) == 0) {
                        f->msg_id = val_str;
                    }
                }
            }
            pos += (int)slen;
        } else {
            /* Unknown wire type — skip is unsafe, abort */
            break;
        }
    }
    return (f->method >= 0) ? 0 : -1;
}

/*
 * Build a minimal Protobuf ping frame (method=0, header type="pong").
 * Returns encoded length.
 */
static int build_pong_frame(uint8_t *buf, int cap)
{
    int pos = 0;

    /* field 4: method = 0 (CONTROL) */
    pos += pb_encode_tag(buf + pos, 4, PB_VARINT);
    pos += pb_encode_varint(buf + pos, 0);

    /* field 5: Header { key="type", value="pong" } */
    /* Build inner header first */
    uint8_t hdr[32];
    int hp = 0;

    hp += pb_encode_tag(hdr + hp, 1, PB_LEN);
    hp += pb_encode_varint(hdr + hp, 4);
    memcpy(hdr + hp, "type", 4);
    hp += 4;
    hp += pb_encode_tag(hdr + hp, 2, PB_LEN);
    hp += pb_encode_varint(hdr + hp, 4);
    memcpy(hdr + hp, "pong", 4);
    hp += 4;

    if (pos + 2 + hp > cap) {
        return 0;
    }
    pos += pb_encode_tag(buf + pos, 5, PB_LEN);
    pos += pb_encode_varint(buf + pos, hp);
    memcpy(buf + pos, hdr, hp);
    pos += hp;

    return pos;
}

/* ------------------------------------------------------------------ */
/*  HTTP helpers                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    struct http_ctx *ctx = evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        if (ctx->len + evt->data_len < ctx->cap) {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len += evt->data_len;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static int http_post_json(const char *url, const char *auth_header,
                          const char *body, char *resp, size_t resp_size)
{
    struct http_ctx ctx = { .buf = resp, .len = 0, .cap = resp_size };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return CLAW_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (auth_header) {
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "HTTP POST %s failed: err=%d status=%d",
                  url, err, status);
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Token management (for sending replies via REST API)                */
/* ------------------------------------------------------------------ */

static int refresh_token(void)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}",
             FEISHU_APP_ID, FEISHU_APP_SECRET);

    int ret = http_post_json(TOKEN_URL, NULL, body, resp, RESP_BUF_SIZE);
    if (ret != CLAW_OK) {
        claw_free(resp);
        return ret;
    }

    cJSON *root = cJSON_Parse(resp);
    claw_free(resp);
    if (!root) {
        return CLAW_ERROR;
    }

    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    if (!token || !cJSON_IsString(token)) {
        cJSON_Delete(root);
        CLAW_LOGE(TAG, "token response missing tenant_access_token");
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    snprintf(s_token, sizeof(s_token), "%s", token->valuestring);
    claw_mutex_unlock(s_lock);

    CLAW_LOGI(TAG, "tenant token refreshed");
    cJSON_Delete(root);
    return CLAW_OK;
}

static void get_auth_header(char *out, size_t size)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    snprintf(out, size, "Bearer %s", s_token);
    claw_mutex_unlock(s_lock);
}

/* ------------------------------------------------------------------ */
/*  Send reply to Feishu via REST API                                  */
/* ------------------------------------------------------------------ */

static int send_reply(const char *chat_id, const char *text)
{
    char auth[TOKEN_BUF_SIZE + 16];
    get_auth_header(auth, sizeof(auth));

    cJSON *content_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(content_obj, "text", text);
    char *content_str = cJSON_PrintUnformatted(content_obj);
    cJSON_Delete(content_obj);
    if (!content_str) {
        return CLAW_NOMEM;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "receive_id", chat_id);
    cJSON_AddStringToObject(body, "msg_type", "text");
    cJSON_AddStringToObject(body, "content", content_str);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    claw_free(content_str);
    if (!body_str) {
        return CLAW_NOMEM;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s?receive_id_type=chat_id", MSG_SEND_URL);

    char resp[512];
    int ret = http_post_json(url, auth, body_str, resp, sizeof(resp));
    claw_free(body_str);

    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "send reply failed");
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  AI worker — run ai_chat() off the websocket callback stack         */
/* ------------------------------------------------------------------ */

#define MSG_TEXT_MAX   512
#define CHAT_ID_MAX   64
#define WORKER_STACK   16384

typedef struct {
    char text[MSG_TEXT_MAX];
    char chat_id[CHAT_ID_MAX];
    int  pending;
} feishu_msg_t;

static feishu_msg_t s_msg;
static claw_sem_t   s_msg_sem;
static claw_mutex_t s_msg_lock;

static void ai_worker_thread(void *arg)
{
    (void)arg;
    char *reply = claw_malloc(REPLY_BUF_SIZE);
    if (!reply) {
        CLAW_LOGE(TAG, "worker: no memory");
        return;
    }

    while (1) {
        claw_sem_take(s_msg_sem, CLAW_WAIT_FOREVER);

        claw_mutex_lock(s_msg_lock, CLAW_WAIT_FOREVER);
        char text[MSG_TEXT_MAX];
        char chat_id[CHAT_ID_MAX];
        memcpy(text, s_msg.text, sizeof(text));
        memcpy(chat_id, s_msg.chat_id, sizeof(chat_id));
        s_msg.pending = 0;
        claw_mutex_unlock(s_msg_lock);

        CLAW_LOGI(TAG, "processing: %s", text);
        int ret = ai_chat(text, reply, REPLY_BUF_SIZE);
        if (ret == CLAW_OK && reply[0] != '\0') {
            send_reply(chat_id, reply);
        } else {
            send_reply(chat_id, "[rt-claw] AI engine error");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Process incoming event payload (JSON inside protobuf frame)        */
/* ------------------------------------------------------------------ */

static void handle_message_event(cJSON *event)
{
    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message) {
        return;
    }

    cJSON *msg_type = cJSON_GetObjectItem(message, "message_type");
    if (!msg_type || strcmp(msg_type->valuestring, "text") != 0) {
        CLAW_LOGD(TAG, "ignore non-text message");
        return;
    }

    cJSON *content_raw = cJSON_GetObjectItem(message, "content");
    if (!content_raw || !cJSON_IsString(content_raw)) {
        return;
    }

    cJSON *content = cJSON_Parse(content_raw->valuestring);
    if (!content) {
        return;
    }

    cJSON *text = cJSON_GetObjectItem(content, "text");
    if (!text || !cJSON_IsString(text)) {
        cJSON_Delete(content);
        return;
    }

    const char *user_msg = text->valuestring;
    CLAW_LOGI(TAG, "recv: %s", user_msg);

    cJSON *chat_id = cJSON_GetObjectItem(message, "chat_id");
    if (!chat_id || !cJSON_IsString(chat_id)) {
        cJSON_Delete(content);
        return;
    }

    /* Post to worker thread instead of calling ai_chat() here */
    claw_mutex_lock(s_msg_lock, CLAW_WAIT_FOREVER);
    if (s_msg.pending) {
        claw_mutex_unlock(s_msg_lock);
        CLAW_LOGW(TAG, "worker busy, dropping message");
        cJSON_Delete(content);
        return;
    }
    snprintf(s_msg.text, MSG_TEXT_MAX, "%s", user_msg);
    snprintf(s_msg.chat_id, CHAT_ID_MAX, "%s", chat_id->valuestring);
    s_msg.pending = 1;
    claw_mutex_unlock(s_msg_lock);

    claw_sem_give(s_msg_sem);
    cJSON_Delete(content);
}

static void process_event_payload(const uint8_t *data, int len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        CLAW_LOGW(TAG, "event payload JSON parse failed");
        return;
    }

    CLAW_LOGI(TAG, "event: %.200s", (const char *)data);

    cJSON *header = cJSON_GetObjectItem(root, "header");
    if (header) {
        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type)) {
            if (strcmp(event_type->valuestring,
                       "im.message.receive_v1") == 0) {
                cJSON *event = cJSON_GetObjectItem(root, "event");
                if (event) {
                    handle_message_event(event);
                }
            } else {
                CLAW_LOGI(TAG, "unhandled event: %s",
                          event_type->valuestring);
            }
        }
    }

    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */
/*  WebSocket event handler                                            */
/* ------------------------------------------------------------------ */

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ws = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        CLAW_LOGI(TAG, "ws connected");
        s_ws_connected = 1;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        CLAW_LOGW(TAG, "ws disconnected");
        s_ws_connected = 0;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (ws->op_code == 0x02 && ws->data_len > 0) {
            /* Binary frame — Protobuf encoded */
            struct feishu_frame f;
            if (parse_frame((const uint8_t *)ws->data_ptr,
                            ws->data_len, &f) == 0) {
                if (f.type && memcmp(f.type, "ping", 4) == 0) {
                    /* Respond with pong */
                    uint8_t pong[64];
                    int plen = build_pong_frame(pong, sizeof(pong));
                    if (plen > 0 && s_ws_client) {
                        esp_websocket_client_send_bin(
                            s_ws_client, (const char *)pong, plen,
                            portMAX_DELAY);
                    }
                    CLAW_LOGD(TAG, "ping/pong");
                } else if (f.method == 1 && f.payload && f.payload_len > 0) {
                    /* DATA frame — payload is JSON event */
                    process_event_payload(f.payload, f.payload_len);
                }
            }
        } else if (ws->op_code == 0x01 && ws->data_len > 0) {
            /* Text frame fallback — try as JSON directly */
            process_event_payload((const uint8_t *)ws->data_ptr,
                                  ws->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        CLAW_LOGE(TAG, "ws error");
        s_ws_connected = 0;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Fetch WebSocket endpoint and connect                               */
/* ------------------------------------------------------------------ */

static int connect_ws(void)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    /* Authenticate directly with AppID + AppSecret in body */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"AppID\":\"%s\",\"AppSecret\":\"%s\"}",
             FEISHU_APP_ID, FEISHU_APP_SECRET);

    int ret = http_post_json(WS_EP_URL, NULL, body, resp, RESP_BUF_SIZE);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "ws endpoint request failed");
        claw_free(resp);
        return ret;
    }

    CLAW_LOGI(TAG, "ws endpoint resp: %.200s", resp);

    cJSON *root = cJSON_Parse(resp);
    claw_free(resp);
    if (!root) {
        CLAW_LOGE(TAG, "ws endpoint JSON parse failed");
        return CLAW_ERROR;
    }

    /* Check error code */
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code && cJSON_IsNumber(code) && code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        CLAW_LOGE(TAG, "ws endpoint error: code=%d msg=%s",
                  code->valueint,
                  (msg && cJSON_IsString(msg)) ? msg->valuestring : "?");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    /* Extract WSS URL: { "data": { "URL": "wss://..." } } */
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    cJSON *url_obj = data_obj ? cJSON_GetObjectItem(data_obj, "URL") : NULL;
    if (!url_obj || !cJSON_IsString(url_obj)) {
        CLAW_LOGE(TAG, "ws endpoint missing URL field");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "ws url: %s", url_obj->valuestring);

    esp_websocket_client_config_t ws_cfg = {
        .uri = url_obj->valuestring,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    cJSON_Delete(root);
    if (!s_ws_client) {
        return CLAW_ERROR;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);
    esp_err_t err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "ws start failed: %d", err);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        return CLAW_ERROR;
    }

    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Main service thread                                                */
/* ------------------------------------------------------------------ */

static void feishu_thread(void *arg)
{
    (void)arg;

    CLAW_LOGI(TAG, "service starting (app_id=%s)", FEISHU_APP_ID);

    /* Fetch tenant token first (needed for sending replies) */
    while (refresh_token() != CLAW_OK) {
        CLAW_LOGW(TAG, "token fetch failed, retry in 10s");
        claw_thread_delay_ms(10000);
    }
    CLAW_LOGI(TAG, "token acquired");

    /* Connect WebSocket long connection */
    while (connect_ws() != CLAW_OK) {
        CLAW_LOGW(TAG, "ws connect failed, retry in %dms",
                  WS_RECONNECT_MS);
        claw_thread_delay_ms(WS_RECONNECT_MS);
    }
    CLAW_LOGI(TAG, "ws connected — ready to receive messages");

    /* Keep-alive loop */
    uint32_t last_refresh = claw_tick_ms();

    for (;;) {
        claw_thread_delay_ms(5000);

        /* Periodic token refresh */
        if (claw_tick_ms() - last_refresh >= TOKEN_REFRESH_MS) {
            refresh_token();
            last_refresh = claw_tick_ms();
        }

        /* Reconnect on disconnect */
        if (!s_ws_connected && s_ws_client) {
            CLAW_LOGW(TAG, "ws lost, reconnecting...");
            esp_websocket_client_destroy(s_ws_client);
            s_ws_client = NULL;
            claw_thread_delay_ms(WS_RECONNECT_MS);
            connect_ws();
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int feishu_init(void)
{
    s_lock = claw_mutex_create("feishu");
    if (!s_lock) {
        return CLAW_ERROR;
    }
    s_msg_sem = claw_sem_create("fs_msg", 0);
    s_msg_lock = claw_mutex_create("fs_msg");
    memset(&s_msg, 0, sizeof(s_msg));

    s_token[0] = '\0';
    s_ws_client = NULL;
    s_ws_connected = 0;
    CLAW_LOGI(TAG, "init ok");
    return CLAW_OK;
}

int feishu_start(void)
{
    /* AI worker thread — handles ai_chat() off the websocket stack */
    claw_thread_create("fs_ai", ai_worker_thread, NULL,
                       WORKER_STACK, 10);

    claw_thread_t t = claw_thread_create("feishu", feishu_thread, NULL,
                                         8192, 10);
    if (!t) {
        CLAW_LOGE(TAG, "failed to create thread");
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

#else /* !CONFIG_CLAW_FEISHU_ENABLE */

int feishu_init(void)  { return 0; }
int feishu_start(void) { return 0; }

#endif /* CONFIG_CLAW_FEISHU_ENABLE */

#else /* !CLAW_PLATFORM_ESP_IDF */

int feishu_init(void)  { return 0; }
int feishu_start(void) { return 0; }

#endif /* CLAW_PLATFORM_ESP_IDF */
