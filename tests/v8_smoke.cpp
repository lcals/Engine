#include "v8_smoke.hpp"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <atomic>
#include <memory>

namespace engine::tests
{
namespace
{

std::atomic_flag smoke_started = ATOMIC_FLAG_INIT;

bool check_script(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* source,
                  V8SmokeLog log)
{
    v8::TryCatch exception(isolate);
    v8::Local<v8::String> text;
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    if (v8::String::NewFromUtf8(isolate, source).ToLocal(&text) &&
        v8::Script::Compile(context, text).ToLocal(&script) &&
        script->Run(context).ToLocal(&result) && result->IsTrue())
    {
        return true;
    }
    if (exception.HasCaught())
    {
        const v8::String::Utf8Value message(isolate, exception.Exception());
        if (*message != nullptr)
        {
            log(*message);
        }
    }
    return false;
}

bool check_context(v8::Isolate* isolate, V8SmokeLog log)
{
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    auto context = v8::Context::New(isolate);
    if (context.IsEmpty())
    {
        log("FAIL: V8 context creation");
        return false;
    }
    v8::Context::Scope context_scope(context);
    if (!check_script(isolate, context, "[1, 2, 3].reduce((a, b) => a + b, 0) === 6", log))
    {
        log("FAIL: JavaScript");
        return false;
    }
    log("PASS: JavaScript");
    // 偏移一个字节的 64 字节视图覆盖共享字节访问和非对齐 Base64 分块。
    if (!check_script(isolate, context,
                      R"js((() => {
    const memory = new SharedArrayBuffer(66);
    const guards = new Uint8Array(memory);
    guards[0] = 0xa5;
    guards[65] = 0x5a;
    const bytes = new Uint8Array(memory, 1, 64);
    const cases = [
        [bytes, 7, 11, 3],
        [new Int32Array(new SharedArrayBuffer(16)), 7, 11, 3],
        [new BigInt64Array(new SharedArrayBuffer(16)), 7n, 11n, 3n]
    ];
    for (const [view, first, next, increment] of cases) {
        if (Atomics.store(view, 0, first) !== first ||
            Atomics.compareExchange(view, 0, first, next) !== first ||
            Atomics.compareExchange(view, 0, first, first) !== next ||
            Atomics.add(view, 0, increment) !== next ||
            Atomics.load(view, 0) !== next + increment) return false;
    }
    const hex = '000102030405060708090a0b0c0d0e0f' +
                '101112131415161718191a1b1c1d1e1f' +
                '202122232425262728292a2b2c2d2e2f' +
                '303132333435363738393a3b3c3d3e3f';
    const base64 = 'AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g' +
                   'ISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+Pw==';
    const fromHex = bytes.setFromHex(hex);
    if (fromHex.read !== hex.length || fromHex.written !== bytes.length ||
        bytes.toHex() !== hex || bytes.toBase64() !== base64) return false;
    bytes.fill(0xcc);
    const fromBase64 = bytes.setFromBase64(base64);
    if (fromBase64.read !== base64.length || fromBase64.written !== bytes.length ||
        bytes.toHex() !== hex || bytes.toBase64() !== base64) return false;
    for (let index = 0; index < bytes.length; ++index) {
        if (Atomics.load(bytes, index) !== index) return false;
    }
    return Atomics.load(guards, 0) === 0xa5 && Atomics.load(guards, 65) === 0x5a;
})())js",
                      log))
    {
        log("FAIL: SharedArrayBuffer Atomics, hex, Base64");
        return false;
    }
    log("PASS: SharedArrayBuffer Atomics, hex, Base64");
    if (!check_script(isolate, context,
                      "new Intl.NumberFormat('en-US').format(1234.5) === '1,234.5' && "
                      "new Intl.DateTimeFormat('en-GB', { timeZone: 'UTC', year: 'numeric', "
                      "month: '2-digit', day: '2-digit' })"
                      ".format(new Date('2024-02-29T12:00:00Z')) === '29/02/2024'",
                      log))
    {
        log("FAIL: Intl");
        return false;
    }
    log("PASS: Intl");
    if (!check_script(isolate, context,
                      "typeof Temporal === 'object' && "
                      "Temporal.PlainDate.from('2024-02-28').add({ days: 1 }).toString() "
                      "=== '2024-02-29' && "
                      "Temporal.ZonedDateTime.from('2024-01-01T00:00:00+08:00[Asia/Shanghai]')"
                      ".epochNanoseconds === 1704038400000000000n",
                      log))
    {
        log("FAIL: Temporal");
        return false;
    }
    log("PASS: Temporal");
    return true;
}

} // namespace

bool run_v8_smoke(const char* executable_path, const char* icu_data_file, V8SmokeLog log)
{
    if (log == nullptr)
    {
        return false;
    }
    if (smoke_started.test_and_set(std::memory_order_relaxed))
    {
        log("FAIL: V8 smoke already ran; start a new application process");
        return false;
    }
    log(v8::V8::GetVersion());
    if (!v8::V8::InitializeICUDefaultLocation(executable_path, icu_data_file))
    {
        log("FAIL: V8 ICU data initialization");
        return false;
    }
    // 固定 V8 版本的 Temporal 仍需显式启用运行时标志。
    v8::V8::SetFlagsFromString("--harmony-temporal");
    auto platform = v8::platform::NewDefaultPlatform();
    if (!platform)
    {
        log("FAIL: V8 platform creation");
        return false;
    }
    v8::V8::InitializePlatform(platform.get());
    if (!v8::V8::Initialize())
    {
        log("FAIL: V8 initialization");
        v8::V8::DisposePlatform();
        return false;
    }

    bool passed = false;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    if (allocator)
    {
        v8::Isolate::CreateParams parameters;
        parameters.array_buffer_allocator = allocator.get();
        auto* isolate = v8::Isolate::New(parameters);
        if (isolate != nullptr)
        {
            passed = check_context(isolate, log);
            isolate->Dispose();
        }
        else
        {
            log("FAIL: V8 isolate creation");
        }
    }
    else
    {
        log("FAIL: V8 ArrayBuffer allocator creation");
    }
    allocator.reset();
    if (!v8::V8::Dispose())
    {
        log("FAIL: V8 disposal");
        passed = false;
    }
    v8::V8::DisposePlatform();
    if (passed)
    {
        log("V8 smoke PASS: JavaScript, SharedArrayBuffer, Intl, Temporal");
    }
    return passed;
}

} // namespace engine::tests
