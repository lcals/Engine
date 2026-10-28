#include <libplatform/libplatform.h>
#include <v8.h>

#include <iostream>
#include <memory>

int main(int argc, char** argv)
{
    if (!v8::V8::InitializeICUDefaultLocation(argc > 0 ? argv[0] : nullptr))
    {
        std::cerr << "V8 ICU data initialization failed\n";
        return 1;
    }
    auto platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    if (!v8::V8::Initialize())
    {
        std::cerr << "V8 initialization failed\n";
        return 1;
    }

    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams parameters;
    parameters.array_buffer_allocator = allocator.get();
    auto* isolate = v8::Isolate::New(parameters);
    if (!isolate)
    {
        std::cerr << "V8 isolate creation failed\n";
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
        return 1;
    }

    bool passed = false;
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        v8::TryCatch exception(isolate);
        auto source = v8::String::NewFromUtf8Literal(
            isolate, "new Intl.NumberFormat('en-US').format(1234.5) === '1,234.5'"
                     " ? [1, 2, 3].reduce((a, b) => a + b, 0) : -1");
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        if (v8::Script::Compile(context, source).ToLocal(&script) &&
            script->Run(context).ToLocal(&result))
        {
            passed = result->IsInt32() && result.As<v8::Int32>()->Value() == 6;
        }
        if (!passed)
        {
            std::cerr << "V8 script execution failed\n";
            if (exception.HasCaught())
            {
                const v8::String::Utf8Value message(isolate, exception.Exception());
                if (*message)
                {
                    std::cerr << *message << '\n';
                }
            }
        }
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    if (passed)
    {
        std::cout << "V8 " << v8::V8::GetVersion() << ": JavaScript result = 6\n";
    }
    return passed ? 0 : 1;
}
