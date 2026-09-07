#include "ota_http.h"
#include "ota_config.h"
#include "ota_flags.h"
#include "w25q64.h"
#include "esp32.h"
#include "mytasks.h"
#include "md5.h"
#include "bsp_uart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

volatile ota_ui_t g_ota_ui = { OTA_UI_IDLE, 0 };

/* 大缓冲一律 static：OTA 流程只在 vTaskESP32/OTA 任务运行
 * （栈上放大结构体嵌套两层会打爆任务栈，实测卡死，教训见开发文档） */
static ota_flags_t s_flag;
static char        req[768];

/* ==================== HTTP 响应解析器 ====================
 * +IPD 解复用后的纯数据按序喂入；stream 选择去处：
 *   0 = cap_buf（check 的 JSON / 进度上报应答，先攒 HTTP 头取 Content-Length）
 *   1 = chunk_buf（下载分片）
 * 注意：请求一律不带 Connection: close（keep-alive）——esp-at 在服务器关链时
 * 会丢弃未打印的尾部 +IPD 数据，读满 Content-Length 后由我们自己 AT+CIPCLOSE
 * 注意：esp-at 对 SSL 流存在"吞最后一个字节"的固件行为，Range 两端各垫 2B 对冲 */
typedef enum { PS_HEAD = 0, PS_BODY } parse_state_t;

static parse_state_t pstate;
static char     head_buf[640];
static uint16_t head_len;
static int32_t  body_expect, body_got;
static uint8_t  stream_mode;
static uint8_t  cap_buf[1024];
static uint16_t cap_len;
static uint8_t  chunk_buf[OTA_CHUNK_SIZE + 832];   /* 头+杂质+垫底余量 */
static uint32_t chunk_off;
static uint8_t  vbuf[256];
static uint8_t  resp_done, resp_err;
static int32_t  s_chunk_expect;                     /* 下载模式收满即停阈值 */

/* 天气残留剔除状态（帧级两遍处理，见 parser_feed） */
static uint8_t  jf_state;        /* 0=正常 2=括号深度跳过中 */
static int16_t  jf_depth;
static char     jf_carry[16];    /* 帧尾不完整令牌暂存 */
static uint8_t  jf_carry_n;

#define OTA_SLOT_CAP    2048     /* 与 esp32.c 的 ESP32_SLOT_CAP 一致 */

static int header_value(const char *key, char *out, int max);

static void body_sink(const uint8_t *d, uint16_t n)
{
	if (stream_mode == 1)
	{
		while (n--)
		{
			if (chunk_off < sizeof(chunk_buf))
				chunk_buf[chunk_off++] = *d++;
		}
		return;
	}
	while (n-- && cap_len < sizeof(cap_buf)) cap_buf[cap_len++] = *d++;
}

static void parser_reset(int stream, uint32_t dest_base)
{
	while (xSemaphoreTake(xSemIPD, 0) == pdTRUE) ;   /* 清残留帧令牌 */

	stream_mode = (uint8_t)stream;
	chunk_off   = 0;
	cap_len     = 0;
	head_len    = 0;
	body_expect = -1;
	body_got    = 0;
	resp_done   = 0;
	resp_err    = 0;
	pstate      = PS_HEAD;
	jf_state    = 0;
	jf_depth    = 0;
	jf_carry_n  = 0;
	(void)dest_base;
}

/* ==================== 天气残留剔除（帧级两遍处理） ====================
 * esp-at 的 SSL 缓冲在关链后不清零：上一条 SSL 连接（天气）的尾流会以
 * {"results":[...]} 的形态混进下一条连接的响应流。逐字节过滤存在
 * "部分匹配吃掉真实字节"的缺陷，故在完整帧上做两遍处理：
 *   第一遍：识别 {"results":[ 签名并按括号深度剔除杂质段；
 *   第二遍：把干净字节喂给解析器。杂质可跨帧（carry 暂存帧尾半截令牌） */
#define JUNK_TOKEN      "{\"results\":["
#define JUNK_TOKEN_LEN  12

static uint8_t  jf_state;        /* 0=正常 2=括号深度跳过中 */
static int16_t  jf_depth;
static char     jf_carry[16];    /* 帧尾不完整令牌暂存 */
static uint8_t  jf_carry_n;

static void parser_feed_core(const char *d, uint16_t n)
{
	uint16_t i;

	for (i = 0; i < n; i++)
	{
		uint8_t c = (uint8_t)d[i];

		if (pstate == PS_HEAD)
		{
			if (head_len < 7)
			{
				/* 前缀重同步：丢弃 "HTTP/1." 之前的任何杂散前缀 */
				if (c == "HTTP/1."[head_len])
				{
					head_buf[head_len++] = (char)c;
					head_buf[head_len] = '\0';
				}
				else
				{
					head_len = (c == 'H') ? 1 : 0;
					head_buf[0] = head_len ? 'H' : '\0';
					head_buf[head_len] = '\0';
				}
				continue;
			}
			if (head_len < sizeof(head_buf) - 1)
			{
				head_buf[head_len++] = (char)c;
				head_buf[head_len] = '\0';
				if (head_len >= 4 && strcmp(&head_buf[head_len - 4], "\r\n\r\n") == 0)
				{
					char v[20];
					body_expect = 0;
					if (header_value("Content-Length:", v, sizeof(v)))
						body_expect = (int32_t)atoi(v);
					if (header_value("Ota-Errno:", v, sizeof(v)) && atoi(v) != 0)
						resp_err = 1;                       /* OneNET 下载错误码 */
					head_buf[head_len - 4] = '\0';
					if (strstr(head_buf, " 20") == NULL)    /* 非 2xx 状态行 */
						resp_err = 1;
					pstate = PS_BODY;
					body_got = 0;
					if (body_expect == 0) resp_done = 1;
				}
				else if (head_len == sizeof(head_buf) - 1)
				{
					resp_err = 1;                           /* 头异常膨胀 */
				}
			}
			else
			{
				resp_err = 1;
			}
		}
		else
		{
			if (body_got < body_expect || body_expect < 0)
			{
				body_sink(&c, 1);
				body_got++;
				if (body_expect >= 0 && body_got >= body_expect) resp_done = 1;
			}
			/* body_expect<0（无 CL）：继续吞，靠空闲超时结束 */
		}
	}
}

static void parser_feed(const char *d, uint16_t n)
{
	if (stream_mode == 1)
	{
		parser_feed_core(d, n);              /* 下载：HTTP/1. 前缀重同步 + Content-Length
		                                      * 精确解析；不走剔杂（固件体里就有
		                                      * JUNK_TOKEN 字串本体，剔杂会自噬） */
		return;
	}
	char     buf[OTA_SLOT_CAP + 32];
	uint16_t total = 0, w = 0, i;
	uint8_t  state = jf_state;
	int16_t  depth = jf_depth;

	/* 跨帧半截令牌前缀 + 新数据拼接 */
	for (i = 0; i < jf_carry_n; i++) buf[total++] = jf_carry[i];
	for (i = 0; i < n; i++) buf[total++] = d[i];
	jf_carry_n = 0;

	i = 0;
	while (i < total)
	{
		char c = buf[i];

		if (state == 2)                          /* 杂质深度内：丢弃 */
		{
			if (c == '{' || c == '[') depth++;
			else if (c == '}' || c == ']')
			{
				if (--depth == 0) { state = 0; }
			}
			i++;
			continue;
		}
		if (c == '{' &&
		    total - i >= JUNK_TOKEN_LEN &&
		    memcmp(buf + i, JUNK_TOKEN, JUNK_TOKEN_LEN) == 0)
		{
			state = 2; depth = 2;                /* 杂质签名命中：整段剔除 */
			i += JUNK_TOKEN_LEN;
			continue;
		}
		if (c == '{' && total - i < JUNK_TOKEN_LEN)
		{
			/* 帧尾不完整令牌：留到下一帧拼接后再判 */
			uint16_t k;
			jf_carry_n = 0;
			for (k = 0; k < total - i; k++) jf_carry[jf_carry_n++] = buf[i + k];
			total = w;                           /* 已确认部分到此为止 */
			break;
		}
		buf[w++] = c;
		i++;
	}

	jf_state = state;
	jf_depth = depth;
	parser_feed_core(buf, (uint16_t)w);
}

/* 在响应头中取某字段值（大小写不敏感，值到 \r 结束） */
static int header_value(const char *key, char *out, int max)
{
	char low[640];
	int  i;
	for (i = 0; i < (int)head_len && i < (int)sizeof(low) - 1; i++)
		low[i] = (head_buf[i] >= 'A' && head_buf[i] <= 'Z') ? (char)(head_buf[i] + 32) : head_buf[i];
	low[i] = '\0';

	char k[32];
	for (i = 0; key[i] && i < (int)sizeof(k) - 1; i++)
		k[i] = (key[i] >= 'A' && key[i] <= 'Z') ? (char)(key[i] + 32) : key[i];
	k[i] = '\0';

	const char *p = strstr(low, k);
	if (!p) return 0;
	p += strlen(k);
	while (*p == ' ') p++;
	i = 0;
	while (*p && *p != '\r' && i < max - 1) out[i++] = *p++;
	out[i] = '\0';
	return 1;
}

/* 消费 FIFO 响应：读到 Content-Length 满 / 出错 / 6s 静默为止。返回 body 字节数（负=错） */
static int http_read(void)
{
	uint32_t quiet = 0;

	for (;;)
	{
		if (xSemaphoreTake(xSemIPD, pdMS_TO_TICKS(300)) == pdTRUE)
		{
			quiet = 0;
			parser_feed(ESP32_GetFrame(), ESP32_GetFrameLen());
			ESP32_PopFrame();
		}
		else if (++quiet >= 20)
			break;

		if (stream_mode == 1 && body_got >= s_chunk_expect &&
		    ESP32_FramesPending() == 0)
			break;                               /* 收满且队列排空才停：esp-at 吞尾使 CL 永不满 */
		if (resp_done)
		{
			ESP32_FrameFlush();                  /* CL 收尾=流已净：清吞字节残留的 dm_remain */
			break;
		}
		if (resp_err) break;
	}
	return resp_err ? -2 : (int)body_got;
}

/* CIP POST 应答（mode 0）：进度上报专用 */
static int http_transaction(char *req, uint16_t req_len)
{
	parser_reset(0, 0);
	if (!ESP32_TcpSend(req, req_len)) return -1;
	return http_read();
}

/* ==================== JSON 小解析 ==================== */
static int json_get_str(const char *json, const char *key, char *out, int max)
{
	const char *p = strstr(json, key);
	int i;
	if (!p) return 0;
	p += strlen(key);
	while (*p == ' ' || *p == ':' || *p == '"') p++;
	for (i = 0; *p && *p != '"' && *p != ',' && *p != '}' && i < max - 1; i++)
		out[i] = *p++;
	out[i] = '\0';
	return i > 0;
}

/* 十六进制字符串不区分大小写比较器 */
static int hex_ieq(const char *a, const char *b)
{
	while (*a && *b)
	{
		char ca = *a++, cb = *b++;
		if (ca >= 'A' && ca <= 'F') ca += 32;
		if (cb >= 'A' && cb <= 'F') cb += 32;
		if (ca != cb) return 0;
	}
	return *a == *b;
}

/* W25Q 写入 + 回读校验（错则擦扇区重写一次；仍错返回 -1 => SPI 路径不可靠） */
static int w25q_write_verified(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	uint32_t w, i, n2;
	int      pass, bad;

	for (pass = 0; pass < 2; pass++)
	{
		bad = 0;
		if (pass == 1)
			W25Q_SectorErase(addr & ~0xFFFu);
		for (w = 0; w < len; w += 256)
		{
			n2 = (len - w > 256) ? 256 : (len - w);
			W25Q_PageProgram(addr + w, buf + w, (uint16_t)n2);
		}
		for (w = 0; w < len && !bad; w += 256)
		{
			n2 = (len - w > 256) ? 256 : (len - w);
			W25Q_Read(addr + w, vbuf, (uint16_t)n2);
			for (i = 0; i < n2; i++)
				if (vbuf[i] != buf[w + i]) { bad = 1; break; }
		}
		if (!bad) return 0;
	}
	return -1;
}

/* ==================== 平台交互 ==================== */

/* 清管道：上一事务（如天气）的在途残留帧到此为止 */
static void OTA_PipeFlush(void)
{
	uint32_t q = 0;
	ESP32_DmuxMode("+IPD,", ':');
	ESP32_FrameFlush();
	for (;;)
	{
		if (xSemaphoreTake(xSemIPD, pdMS_TO_TICKS(100)) == pdTRUE)
		{
			q = 0;
			ESP32_PopFrame();                /* 丢弃残留帧 */
		}
		else if (++q >= 3) break;            /* 300ms 静默 = 干净 */
	}
}

/* 查询升级任务（CIP 裸 TCP，keep-alive）：0=有任务 -1=失败 1=无任务 */
static int OTA_CheckTask(char *target, char *tid, uint32_t *size, char *md5hex)
{
	char ver[24], tmp[24];
	long eno;
	int  got, n, i;

	OTA_Flags_Load(&s_flag);
	/* 设备当前版本以编译期 FW_VERSION 为准（回滚/重烧后 flags 里的记录是陈旧值） */
	strncpy(ver, FW_VERSION, sizeof(ver) - 1);
	ver[sizeof(ver) - 1] = '\0';

	g_ota_ui.state = OTA_UI_CHECKING;
	/* 固件身份证：从日志即可确认板载版本，避免"改了没烧"的空转排查 */
	safe_printf("[OTA] FW " FW_VERSION " (built " __DATE__ " " __TIME__ ")\r\n");

	ESP32_DmuxMode("+IPD,", ':');
	OTA_PipeFlush();                    /* 清天气等上一事务的在途残留 */

	if (!ESP32_TcpConnect(ONENET_HOST, ONENET_PORT, "SSL"))
	{
		safe_printf("[OTA] tcp connect fail\r\n");
		return -1;
	}

	n = snprintf(req, sizeof(req),
		"GET /fuse-ota/%s/%s/check?type=2&version=%s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Authorization: %s\r\n"
		"\r\n",
		ONENET_PRO_ID, ONENET_DEV_NAME, ver, ONENET_HOST, ONENET_AUTH);

	got = http_transaction(req, (uint16_t)n);
	ESP32_TcpClose();

	if (got <= 0)
	{
		safe_printf("[OTA] check http fail (%d)\r\n", got);
		return -1;
	}
	cap_buf[cap_len < sizeof(cap_buf) - 1 ? cap_len : sizeof(cap_buf) - 1] = '\0';
	safe_printf("[OTA] check resp(%d): %.*s\r\n", (int)cap_len,
	            (int)(cap_len < 384 ? cap_len : 384), (char *)cap_buf);

	eno = 0;
	if (json_get_str((char *)cap_buf, "\"errno\"", tmp, sizeof(tmp)) ||
	    json_get_str((char *)cap_buf, "\"code\"", tmp, sizeof(tmp)))
		eno = atol(tmp);
	if (eno == 11 || eno == 1178) { safe_printf("[OTA] no upgrade task\r\n"); return 1; }
	if (eno != 0)  { safe_printf("[OTA] check errno=%ld\r\n", eno); return -1; }
	if (!json_get_str((char *)cap_buf, "\"target\"", target, 24) ||
	    !json_get_str((char *)cap_buf, "\"tid\"", tid, 32) ||
	    !json_get_str((char *)cap_buf, "\"md5\"", md5hex, 40))
	{
		safe_printf("[OTA] check resp missing fields\r\n");
		return -1;
	}
	/* 只保留十六进制字符并要求恰好 32 位（脏字段=接收被截断的信号） */
	for (i = 0, n = 0; md5hex[i] && n < 32; i++)
	{
		char c = md5hex[i];
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
			md5hex[n++] = c;
	}
	md5hex[n] = '\0';
	if (n != 32)
	{
		safe_printf("[OTA] md5 field dirty (%s), retry check\r\n", md5hex);
		return -1;
	}
	if (!json_get_str((char *)cap_buf, "\"size\"", tmp, sizeof(tmp)))
	{
		safe_printf("[OTA] check resp missing size\r\n");
		return -1;
	}
	*size = (uint32_t)atol(tmp);
	safe_printf("[OTA] task: %s size=%u md5=%.8s...\r\n", target, (unsigned)*size, md5hex);
	return 0;
}

/* 上报下载进度（CIP POST 短连接，尽力而为） */
static void OTA_ReportStep(const char *tid, int step)
{
	char body[24];
	int  bn = snprintf(body, sizeof(body), "{\"step\":%d}", step);
	int  n = snprintf(req, sizeof(req),
		"POST /fuse-ota/%s/%s/%s/status HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Authorization: %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n%s",
		ONENET_PRO_ID, ONENET_DEV_NAME, tid, ONENET_HOST, ONENET_AUTH, bn, body);

	if (!ESP32_TcpConnect(ONENET_HOST, ONENET_PORT, "SSL")) return;
	http_transaction(req, (uint16_t)n);
	ESP32_TcpClose();
	safe_printf("[OTA] progress %d%% reported\r\n", step);
}

/* SSL 连接（含 3 次重试）。0=成功 */
static int ota_connect(void)
{
	int ci;
	for (ci = 0; ci < 3; ci++)
	{
		if (ESP32_TcpConnect(ONENET_HOST, ONENET_PORT, "SSL")) return 0;
		safe_printf("[OTA] connect retry %d\r\n", ci + 1);
		vTaskDelay(pdMS_TO_TICKS(1500));
	}
	return -1;
}

/* 主流程：查询 -> 单连接 keep-alive 分片下载（逐片回读校验）
 *        -> MD5（失败自动重下一次） -> READY -> 复位 */
void OTA_Run(const char *ssid, const char *pwd)
{
	char     target[24], tid[32], md5hex[40];
	uint32_t size, offset;
	static MD5_CTX  ctx;
	static uint8_t  digest[16], md5_buf[512];
	char     hex[33];
	int      rc, dl_attempt, retries = 0;

	rc = OTA_CheckTask(target, tid, &size, md5hex);
	if (rc != 0)
	{
		g_ota_ui.state = (rc == 1) ? OTA_UI_NO_TASK : OTA_UI_FAIL;
		return;
	}

	g_ota_ui.state = OTA_UI_DOWNLOADING;
	for (dl_attempt = 0; dl_attempt < 2; dl_attempt++)
	{
		if (dl_attempt == 1)
			safe_printf("[OTA] MD5 retry: re-download once\r\n");   /* 位错误多为偶发，整包重下一次 */

		W25Q_EraseRange(W25Q_ZONE1_BASE, size);
		memset(&s_flag, 0, sizeof(s_flag));
		strcpy(s_flag.new_ver, target);
		s_flag.fw_size = size;
		strncpy(s_flag.file_md5, md5hex, sizeof(s_flag.file_md5) - 1);
		s_flag.downloaded_size = 0;
		OTA_Flags_Save(&s_flag);

		if (ota_connect())
		{
			g_ota_ui.state = OTA_UI_FAIL;
			return;
		}

		offset = 0;
		retries = 0;
		while (offset < size)
		{
			/* esp-at SSL 流固件行为：每条响应吞尾约 2B（实测 CL=4864 收 4862）。
			 * 中间片 Range 尾部多要 768B，被吞的是垫底；收满 want 即停，
			 * 剩余垫底留在流里由下一片的前缀重同步跳过。末片顶到文件尾无垫可牺牲，
			 * 短收走 flush 补收（见下方 last chunk 分支） */
			uint32_t want = (size - offset > OTA_CHUNK_SIZE) ? OTA_CHUNK_SIZE : (size - offset);
			int32_t  r_end = (int32_t)(offset + want - 1 + 768);
			int      got;
			uint8_t  ok_chunk;

			if (r_end > (int32_t)(size - 1)) r_end = (int32_t)(size - 1);
			g_ota_ui.percent = (uint8_t)(offset * 100 / size);
			snprintf(req, sizeof(req),
				"GET /fuse-ota/%s/%s/%s/download HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Authorization: %s\r\n"
				"Range: bytes=%u-%d\r\n"
				"\r\n",
				ONENET_PRO_ID, ONENET_DEV_NAME, tid, ONENET_HOST, ONENET_AUTH,
				(unsigned)offset, (int)r_end);

			parser_reset(1, 0);
			ESP32_FrameFlush();                  /* 清上一流吞尾残留的 dm_remain（流已静默） */
			s_chunk_expect = (int32_t)want;
			if (!ESP32_TcpSend(req, (uint16_t)strlen(req)))
				got = -1;
			else
				got = http_read();

			ok_chunk = 0;
			if (got >= (int)want)
			{
				/* chunk_buf 即纯固件体（头解析定位体起点，CL 体逐字节对应） */
				if (!w25q_write_verified(W25Q_ZONE1_BASE + offset, chunk_buf, want))
					ok_chunk = 1;
				else
				{
					ESP32_TcpClose();
					safe_printf("[OTA] w25q verify fail at %u (SPI path)\r\n", (unsigned)offset);
					g_ota_ui.state = OTA_UI_FAIL;
					return;
				}
			}
			else if (offset + want == size && got >= (int)want - 4 && got > 0)
			{
				/* 末片短收 1~4B：esp-at 固定吞掉每条响应的尾 1~2 字节，且实测被吞
				 * 字节是丢弃而非暂存（flush 顶出的是 HTTP 头，无法回收，排障35）。
				 * 对策在文件侧：上传包末尾垫 4B 0x00（make_release.py 追加），
				 * 被吞的只会是垫底。此处按缺失量补写 0x00 重建完整文件，MD5 兜底 */
				safe_printf("[OTA] last chunk short (got=%d want=%u), pad %u zero\r\n",
				            got, (unsigned)want, (unsigned)(want - got));
				if (!w25q_write_verified(W25Q_ZONE1_BASE + offset, chunk_buf, (uint32_t)got))
				{
					uint8_t zbuf[4] = {0, 0, 0, 0};
					if (!w25q_write_verified(W25Q_ZONE1_BASE + offset + got, zbuf,
					                         want - (uint32_t)got))
						ok_chunk = 1;
				}
				if (!ok_chunk)
					safe_printf("[OTA] last chunk pad write fail\r\n");
			}
			if (!ok_chunk)
			{
				/* 链路疑似断开：关掉重建再重试同一分片 */
				ESP32_TcpClose();
				if (++retries > 5)
				{
					safe_printf("[OTA] chunk fail at %u\r\n", (unsigned)offset);
					break;                      /* 本轮尝试失败 */
				}
				safe_printf("[OTA] chunk retry %d (got=%d want=%u)\r\n", retries, got, (unsigned)want);
				vTaskDelay(pdMS_TO_TICKS(1000));
				{
					/* 诊断：响应头 + RX 黑匣子（联调期） */
					safe_printf("[OTA] resp head(%u): %.48s\r\n",
					            (unsigned)head_len, head_buf);
					ESP32_RxDiag();
				}
				if (retries == 4)
				{
					safe_printf("[OTA] module reset recovery\r\n");
					if (!ESP32_OTA_PreFlight(ssid, pwd))   /* 返回 true=成功，取非才是失败 */
					{
						safe_printf("[OTA] preflight fail\r\n");
						g_ota_ui.state = OTA_UI_FAIL;
						return;
					}
				}
				if (ota_connect())                    /* 复位恢复后同样要重建 SSL 连接 */
				{
					g_ota_ui.state = OTA_UI_FAIL;
					return;
				}
				continue;
			}

			retries = 0;
			offset += want;
			g_ota_ui.percent = (uint8_t)(offset * 100 / size);
		}
		ESP32_TcpClose();

		if (offset < size)
		{
			g_ota_ui.state = OTA_UI_FAIL;
			return;
		}
		safe_printf("[OTA] download complete (%u bytes)\r\n", (unsigned)size);
		/* 分块指纹（联调期） */
		{
			uint32_t bo;
			for (bo = 0; bo < size; bo += 4096)
			{
				uint32_t c2 = (size - bo > 4096) ? 4096 : (size - bo);
				uint32_t ro;
				MD5_CTX  bctx;
				uint8_t  bd[16];
				char     bh[33];
				MD5_Init(&bctx);
				for (ro = 0; ro < c2; ro += 512)
				{
					uint32_t n2 = (c2 - ro > 512) ? 512 : (c2 - ro);
					W25Q_Read(W25Q_ZONE1_BASE + bo + ro, md5_buf, (uint16_t)n2);
					MD5_Update(&bctx, md5_buf, n2);
				}
				MD5_Final(bd, &bctx);
				MD5_HexStr(bd, bh);
				bh[8] = '\0';
				W25Q_Read(W25Q_ZONE1_BASE + bo, md5_buf, 8);
				safe_printf("[OTA] B%03u %.8s %02X%02X%02X%02X%02X%02X%02X%02X\r\n",
				            (unsigned)(bo / 4096), bh,
				            md5_buf[0], md5_buf[1], md5_buf[2], md5_buf[3],
				            md5_buf[4], md5_buf[5], md5_buf[6], md5_buf[7]);
			}
		}

		/* ---- MD5 全量校验（回读 Zone1） ---- */
		g_ota_ui.state = OTA_UI_VERIFYING;
		MD5_Init(&ctx);
		for (offset = 0; offset < size; offset += sizeof(md5_buf))
		{
			uint32_t c = (size - offset > sizeof(md5_buf)) ? sizeof(md5_buf) : (size - offset);
			W25Q_Read(W25Q_ZONE1_BASE + offset, md5_buf, (uint16_t)c);
			MD5_Update(&ctx, md5_buf, c);
		}
		MD5_Final(digest, &ctx);
		MD5_HexStr(digest, hex);

		OTA_Flags_Load(&s_flag);
		if (!hex_ieq(hex, s_flag.file_md5))
		{
			safe_printf("[OTA] MD5 mismatch: %s vs %s\r\n", hex, s_flag.file_md5);
			continue;                       /* 整包重下一次 */
		}
		safe_printf("[OTA] MD5 OK\r\n");
		break;
	}
	if (dl_attempt == 2)
	{
		g_ota_ui.state = OTA_UI_FAIL;
		return;
	}

	/* ---- 置 READY，交棒 BootLoader ---- */
	s_flag.upgrade_flag    = UPGRADE_MAGIC_READY;
	s_flag.downloaded_size = 0;
	OTA_Flags_Save(&s_flag);

	OTA_ReportStep(tid, 100);               /* step=100：平台切"正在升级" */

	g_ota_ui.state = OTA_UI_DONE_REBOOT;
	safe_printf("[OTA] firmware ready, reboot to bootloader\r\n");
	vTaskDelay(pdMS_TO_TICKS(500));
	NVIC_SystemReset();
}

void OTA_NotifyFail(void)
{
	g_ota_ui.state = OTA_UI_FAIL;
}
