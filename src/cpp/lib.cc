#include <napi.h>
#include "num.hpp"
#include "encoders.hpp"

using namespace std;


Napi::Value foo(const Napi::CallbackInfo& info) {
    auto typeData = info[0].As<Napi::Object>();
    return typeData.Get("name");
}

Napi::Object init_module(Napi::Env env, Napi::Object exports) {
    exports.Set(
        Napi::String::New(env, "foo"),
        Napi::Function::New(env, foo)
    );
    return exports;
}

NODE_API_MODULE(index, init_module)
