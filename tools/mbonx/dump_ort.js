/*
 * mbonx dump agent — 在样本把模型交给 ONNX Runtime 的瞬间截获明文
 *
 * 原理：BeeEdit 的 C# 绑定不走导出符号，而是 OrtGetApiBase()->GetApi(v) 取函数表后间接调用。
 *       因此这里先拿到 OrtApi 表，再按表偏移挂钩 CreateSessionFromArray（offset 64）。
 *
 * 用法（由 dump.py 加载，不建议手动 frida -l）：
 *   python tools/mbonx/dump.py --exe <BeeEdit.exe> --out samples/mbonx/dumped
 */
'use strict';

const ORT_MODULE = 'onnxruntime.dll';
const OFFSET_CREATE_SESSION = 7 * Process.pointerSize;        // CreateSession
const OFFSET_CREATE_SESSION_FROM_ARRAY = 8 * Process.pointerSize; // CreateSessionFromArray
const OFFSET_GET_INPUT_COUNT = 30 * Process.pointerSize;      // SessionGetInputCount

let seq = 0;
const log = (m) => send({ type: 'log', msg: m });

// 反反调试：样本被注入后会走 Environment.FailFast 自杀。
// 用 attach + retval 改写（Frida 17 下 Interceptor.replace 行为有变），
// 并在终止原语上加最后一道保险：即使 FailFast 触发，也不让进程死掉。
function hookAntiDebug() {
  const installed = [];

  // Frida 17 移除了静态 Module.findExportByName，必须走模块实例
  function findExport(mod, name) {
    try {
      const m = Process.findModuleByName(mod);
      return m ? m.findExportByName(name) : null;
    } catch (e) {
      return null;
    }
  }

  function hookFake(mods, name, onLeave, tag) {
    for (const mod of mods) {
      const addr = findExport(mod, name);
      if (!addr) continue;
      try {
        Interceptor.attach(addr, {
          onEnter(args) {
            this.a = args;
          },
          onLeave(retval) {
            try {
              onLeave(retval, this.a);
            } catch (e) {}
          },
        });
        installed.push(tag || name);
        return true;
      } catch (e) {
        log('[adb] ' + name + ' 挂接失败: ' + e.message);
      }
    }
    return false;
  }

  hookFake(['kernel32.dll'], 'IsDebuggerPresent', (r) => r.replace(0));
  hookFake(['kernel32.dll'], 'CheckRemoteDebuggerPresent', (r, a) => {
    try {
      a[1].writeU8(0);
    } catch (e) {}
    r.replace(1);
  });
  hookFake(['ntdll.dll'], 'NtQueryInformationProcess', (r, a) => {
    const cls = a[1].toInt32();
    if (cls === 7 || cls === 30 || cls === 31) {
      try {
        a[2].writeU32(0);
      } catch (e) {}
      r.replace(0);
    }
  });
  hookFake(['kernelbase.dll', 'kernel32.dll'], 'RaiseFailFastException', (r) => {
    log('[adb] 拦截 FailFast');
    r.replace(0);
  });
  hookFake(['kernel32.dll'], 'TerminateProcess', (r, a) => {
    log('[adb] 拦截 TerminateProcess(pid=' + a[0].toInt32() + ')');
    r.replace(1);
  });
  hookFake(['ntdll.dll'], 'NtTerminateProcess', (r) => {
    log('[adb] 拦截 NtTerminateProcess');
    r.replace(0);
  });

  log('[adb] 已安装 ' + installed.length + ' 个: ' + installed.join(', '));
  return installed;
}
const keepAlive = hookAntiDebug();

function resolveApiTable() {
  const mod = Process.findModuleByName(ORT_MODULE);
  if (!mod) {
    log('[!] ' + ORT_MODULE + ' 尚未加载');
    return null;
  }
  const getApiBase = mod.findExportByName('OrtGetApiBase');
  if (!getApiBase) {
    log('[!] 找不到 OrtGetApiBase 导出');
    return null;
  }
  const base = new NativeFunction(getApiBase, 'pointer', [])();
  if (base.isNull()) {
    log('[!] OrtGetApiBase() 返回空');
    return null;
  }
  const getApi = new NativeFunction(base.readPointer(), 'pointer', ['uint32']);
  // 版本号从高往低试，取第一个可用的表（表是前缀兼容的，早期成员偏移稳定）
  for (let v = 22; v >= 10; v--) {
    const api = getApi(v);
    if (!api.isNull()) {
      log('[+] OrtApi 版本 ' + v + ' 解析成功 @ ' + api);
      return api;
    }
  }
  log('[!] 没有可用的 OrtApi 版本');
  return null;
}

function hookCreateSessionFromArray(api) {
  const fnPtr = api.add(OFFSET_CREATE_SESSION_FROM_ARRAY).readPointer();
  if (fnPtr.isNull()) {
    log('[!] CreateSessionFromArray 指针为空');
    return;
  }
  log('[+] CreateSessionFromArray @ ' + fnPtr);

  Interceptor.attach(fnPtr, {
    onEnter(args) {
      // OrtStatus* OrtCreateSessionFromArray(env, model_data, model_data_length, options, out)
      const dataPtr = args[1];
      const len = args[2].toUInt32();
      if (dataPtr.isNull() || len < 1024) return;

      let head = '';
      try {
        const b = dataPtr.readByteArray(8);
        head = Array.from(new Uint8Array(b))
          .map((x) => x.toString(16).padStart(2, '0'))
          .join(' ');
      } catch (e) {
        head = '(不可读)';
      }
      log('[>>] 截获模型: len=' + len + ' head=' + head);

      try {
        const bytes = dataPtr.readByteArray(len);
        send({ type: 'model', len: len, seq: seq++, head: head }, bytes);
      } catch (e) {
        log('[!] 读取失败: ' + e.message);
      }
    },
  });
}

function hookCreateSession(api) {
  const fnPtr = api.add(OFFSET_CREATE_SESSION).readPointer();
  if (fnPtr.isNull()) return;
  Interceptor.attach(fnPtr, {
    onEnter(args) {
      try {
        const p = args[1];
        if (!p.isNull()) log('[..] CreateSession(path) = ' + p.readUtf16String());
      } catch (e) {}
    },
  });
}

// 兜底：内存里扫 ONNX protobuf 特征（producer_name="pytorch" 的长度前缀 12 07）
rpc.exports = {
  scanonnx(maxhits) {
    const sig = [0x12, 0x07, 0x70, 0x79, 0x74, 0x6f, 0x72, 0x63, 0x68];
    const hits = [];
    Process.enumerateRanges('r--').forEach((r) => {
      if (hits.length >= (maxhits || 4)) return;
      if (r.size < 1024 * 1024) return; // 只扫大块
      try {
        let cur = r.base;
        const end = r.base.add(r.size);
        while (cur.compare(end) < 0 && hits.length < (maxhits || 4)) {
          const found = Memory.scanSync(cur, end.sub(cur), sig);
          if (!found || found.length === 0) break;
          hits.push({ addr: found[0].address.toString(), size: r.size });
          cur = found[0].address.add(1);
        }
      } catch (e) {}
    });
    log('[scan] 命中 ' + hits.length + ' 处');
    return hits;
  },

  // 从指定地址 dump 指定长度
  dumpat(addr, len) {
    const p = ptr(addr);
    const bytes = p.readByteArray(len);
    send({ type: 'model', len: len, seq: seq++, head: 'manual' }, bytes);
    return true;
  },
};

function bootstrap() {
  const api = resolveApiTable();
  if (!api) return false;
  hookCreateSessionFromArray(api);
  hookCreateSession(api);
  return true;
}

// onnxruntime.dll 可能延迟加载
if (!bootstrap()) {
  const iv = setInterval(() => {
    if (bootstrap()) clearInterval(iv);
  }, 1000);
  // 60s 后放弃
  setTimeout(() => clearInterval(iv), 60000);
}
