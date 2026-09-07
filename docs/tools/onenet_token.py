# -*- coding: utf-8 -*-
"""OneNET 鉴权 token 生成/校验工具

用法:
  1) MQTT（mqtts.heclouds.com，用【产品级】AccessKey）:
     python onenet_token.py mqtt <product_id> <product_access_key>
  2) 融合版 OTA HTTP API（iot-api.heclouds.com，本工程 OTA 用这个）
     【实测有效】products 资源 + 产品 access_key 签名:
     python onenet_token.py ota <product_id> <product_access_key>
     （fuse 模式 userid 资源实测全部 10403，仅作备用）:
     python onenet_token.py fuse <user_id> <user_access_key>
  3) 校验手上某个 token 是不是用某个 key 签的（排障用）:
     python onenet_token.py verify "<完整token串>" <某个access_key>
     python onenet_token.py verifycfg <某个access_key>   （直接验 ota_config.h 里的 ONENET_AUTH）
  4) 不烧板、在 PC 上直接拿 token 试 check 接口（鉴权过不过秒出）:
     python onenet_token.py httpcheck "<完整token串>"
     python onenet_token.py httpcheckcfg                 （token/产品/设备/版本全读自 ota_config.h）
  5) 一条命令生成+联网验证（推荐！杜绝 res 双重编码，key 引号包住）:
     python onenet_token.py trykey <access_key>

user_id: OneNET 控制台右上角 账号信息 里的 用户ID
账号级 AccessKey: 控制台 权限管理/访问权限 -> AccessKey（账号级，不在产品详情页里）
产品级 AccessKey: 产品详情页里的 key（只够签 MQTT/产品资源）
生成后整串粘贴到 app/ota/ota_config.h 的 ONENET_AUTH 宏。

verify 模式：按 token 自带的 version/res/et/method 重算一遍签名并比对，
能直接回答 "这个 token 是用产品级还是账号级 key 签的"。
"""
import base64, hmac, hashlib, urllib.parse, sys

def _hash_by_name(name):
    return {'md5': hashlib.md5, 'sha1': hashlib.sha1, 'sha256': hashlib.sha256}[name]

def make_token(version, res, key_b64, method='sha1', et=2534022720):
    string_for_sign = '%s\n%s\n%s\n%s' % (et, method, res, version)
    key = base64.b64decode(key_b64)
    sign = base64.b64encode(
        hmac.new(key, string_for_sign.encode('utf-8'), _hash_by_name(method)).digest()
    ).decode()
    return 'version=%s&res=%s&et=%s&method=%s&sign=%s' % (
        version, urllib.parse.quote(res, safe=''), et, method,
        urllib.parse.quote(sign, safe=''))

def verify_token(token, key_b64):
    kv = dict(p.split('=', 1) for p in token.strip().split('&') if '=' in p)
    version = kv.get('version', '')
    res     = urllib.parse.unquote(kv.get('res', ''))
    et      = kv.get('et', '')
    method  = kv.get('method', 'sha1').lower()
    sign    = urllib.parse.unquote(kv.get('sign', ''))
    if method not in ('md5', 'sha1', 'sha256'):
        print('[verify] unknown method: %s' % method)
        return False
    string_for_sign = '%s\n%s\n%s\n%s' % (et, method, res, version)
    key = base64.b64decode(key_b64)
    calc = base64.b64encode(
        hmac.new(key, string_for_sign.encode('utf-8'), _hash_by_name(method)).digest()
    ).decode()
    ok = (calc == sign)
    print('[verify] method :', method)
    print('[verify] res    :', res)
    print('[verify] et     :', et)
    print('[verify] sign(token)    :', sign)
    print('[verify] sign(recompute):', calc)
    print('[verify] =>', 'MATCH，token 就是这个 key 签的' if ok else 'MISMATCH，不是这个 key 签的')
    return ok

def read_hdr_token_and_ids():
    import io, re
    for enc in ('utf-8-sig', 'gbk'):            # 兼容仓库内不同编码的历史文件
        try:
            hdr = io.open(r'app/ota/ota_config.h',
                          'r', encoding=enc).read()
            break
        except UnicodeDecodeError:
            continue
    else:
        return ('', '', '', '', '')
    def macro(name, default=''):
        m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, hdr)
        return m.group(1) if m else default
    m = re.search(r'#define ONENET_AUTH     "([^"]*)"', hdr)
    return (m.group(1) if m else '', macro('ONENET_PRO_ID'),
            macro('ONENET_DEV_NAME'), macro('FW_VERSION', '1.1.0'),
            macro('ONENET_USER_ID'))

def http_check(token):
    """PC 上直接 GET check 接口：返回体不是 10403 即鉴权通过，烧板前先过这关"""
    import ssl, urllib.request, urllib.error
    _, pro, dev, ver, _ = read_hdr_token_and_ids()
    url = 'https://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=2&version=%s' % (pro, dev, ver)
    print('[httpcheck] GET', url)
    req = urllib.request.Request(url, headers={'Authorization': token})
    try:
        r = urllib.request.urlopen(req, timeout=15)
    except urllib.error.HTTPError as e:
        r = e
    except Exception as e:
        print('[httpcheck] network error:', e)
        try:
            ctx = ssl._create_unverified_context()
            r = urllib.request.urlopen(urllib.request.Request(
                url, headers={'Authorization': token}), timeout=15, context=ctx)
        except Exception as e2:
            print('[httpcheck] retry failed:', e2)
            return
    body = r.read().decode('utf-8', 'replace')
    print('[httpcheck] status:', getattr(r, 'status', None) or r.code)
    print('[httpcheck] body :', body)
    if '10403' in body:
        print('[httpcheck] => 鉴权失败（10403），换一把 key 重新生成 token 再试')
    elif '"code":0' in body or '"errno":0' in body or '"target"' in body:
        print('[httpcheck] => 鉴权通过且有升级任务，可以把 token 写进 ota_config.h 烧板了')
    else:
        print('[httpcheck] => 鉴权通过（无任务/其他业务码），token 可用')

if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'verifycfg':
        import io, re
        for enc in ('utf-8-sig', 'gbk'):        # 兼容仓库内不同编码的历史文件
            try:
                hdr = io.open(r'app/ota/ota_config.h',
                              'r', encoding=enc).read()
                break
            except UnicodeDecodeError:
                continue
        else:
            hdr = ''
        m = re.search(r'#define ONENET_AUTH     "([^"]*)"', hdr)
        assert m, 'ota_config.h 里没找到 ONENET_AUTH'
        verify_token(m.group(1), sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == 'httpcheckcfg':
        t, _, _, _, _ = read_hdr_token_and_ids()
        assert t, 'ota_config.h 里没找到 ONENET_AUTH'
        http_check(t)
    elif len(sys.argv) == 3 and sys.argv[1] == 'trykey':
        # 生成 + 联网验证一体：res 由脚本内部拼（杜绝双重编码），key 从命令行传
        _, _, _, _, uid = read_hdr_token_and_ids()
        assert uid, 'ota_config.h 里没找到 ONENET_USER_ID'
        t = make_token('2022-05-01', 'userid/%s' % uid, sys.argv[2])
        print('[trykey] token:')
        print(t)
        http_check(t)
    elif len(sys.argv) == 3 and sys.argv[1] == 'httpcheck':
        http_check(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == 'verify':
        verify_token(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == 'mqtt':
        print(make_token('2018-10-31', 'products/%s' % sys.argv[2], sys.argv[3], method='md5'))
    elif len(sys.argv) == 4 and sys.argv[1] == 'ota':
        print(make_token('2022-05-01', 'products/%s' % sys.argv[2], sys.argv[3], method='sha1'))
    elif len(sys.argv) == 4 and sys.argv[1] == 'fuse':
        print(make_token('2022-05-01', 'userid/%s' % sys.argv[2], sys.argv[3], method='sha1'))
    else:
        print(__doc__)
        sys.exit(1)
