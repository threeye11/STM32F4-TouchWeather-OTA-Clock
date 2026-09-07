#include "weather.h"

#include <string.h>
/* 取字符串字段：找 "key"，跳过空格/冒号/引号，取到引号/逗号为止 
//const char *json：原始完整 json 应答字符串（只读）
//const char *key：要查找的键名字符串，例如`"temp"`
//char *out：输出缓冲区，把提取出来的 value 拷贝到这里
//size_t max_len：输出 out 缓冲区总大小，防止溢出
*/
static const char *JsonGetStrValue(const char *json, const char *key,
																		char *out, size_t max_len				)
{
	if(json == NULL || key == NULL || out == NULL || max_len == 0)
        return NULL;
	
	const char *p = strstr(json ,key);
	if (!p)return NULL;
	p +=strlen(key);
	//跳过无关字符：空格、冒号、双引号。
	while (*p == ' ' || *p == ':' || *p == '"')p++;			
	size_t i = 0;
	//循环条件：原始字符串没结束,
	//没碰到 `"`、`,`、`}` 这些 value 结束标记
	//输出缓冲区还剩位置（留 1 字节给`\0`）
	//→ 才继续拷贝字符。
	while (*p && *p != '"' && *p != ',' && *p != '}' && i < max_len - 1) {
		out[i++] = *p++;
	}
	out[i] = '\0';
    return p;
}


/* 取整数字段 */
static const char *JsonGetIntValue(const char *json, const char *key, int *out)
{
	const char *p = strstr(json , key);
	if(!p)return NULL;
	p +=strlen(key);
	while (*p == ' ' || *p == ':' || *p == '"')p++;
	//手写十进制转换，不用atoi（MicroLIB 兼容
	int sign = 1;
	if (*p == '-') { sign = -1; p++; }
	int val = 0;
	while (*p >= '0' && *p <= '9') {
			val = val * 10 + (*p - '0');
			p++;
	}
	*out = sign * val;
	return p;
}

/* 温度字段:整数部分 + 1位小数 */
static const char *JsonGetTempValue(const char *json , const char *key , int *int_part , uint8_t *frac_part)
{
	const char *p = strstr(json , key);
	if(!p)return NULL;
	p +=strlen(key);
	while (*p == ' ' || *p == ':' || *p == '"')p++;
	
	int sign = 1;
	if (*p == '-')
	{
		sign = -1;
		p++;
	}
	//整数部分
	int ip = 0;
	while (*p >= '0' && *p <= '9') {
			ip = ip * 10 + (*p - '0');
			p++;
	}
	//小数部分
	uint8_t fp = 0;
	if (*p == '.') {
			p++;
			if (*p >= '0' && *p <= '9') {
					fp = (uint8_t)(*p - '0');
			}
	}
	*int_part = sign * ip;
	*frac_part = fp;
	return p;
}

bool Parse_SeniverseResponse(const char *response , weather_info *info)
{
	if (response == NULL || info == NULL)return false;
	memset (info , 0 ,sizeof(weather_info));
	
	const char *results = strstr(response ,"\"results\":");
	if (!results)return false;
	
	const char *location = strstr(response ,"\"location\":");
	if (location )
	{
		JsonGetStrValue(location , "\"name\"" , info->city ,sizeof (info->city));
		JsonGetStrValue(location , "\"path\"" , info->location , sizeof (info->location));
	}
	
	const char *now = strstr (results , "\"now\":");
	if (now)
	{
		JsonGetStrValue(now ,"\"text\"" , info->weather , sizeof(info->weather));
		JsonGetIntValue(now , "\"code\"" , &info->weather_code );
		JsonGetTempValue(now , "\"temperature\"" ,&info->temp_int , &info->temp_frac);

		/* 保留 API 原始 UTF-8：LVGL 按 UTF-8 渲染中文（旧 LCD GBK 字库已弃用） */
		return true;
	}
	return false;
}

