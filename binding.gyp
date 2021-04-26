{
    "targets": [
        {
            "cflags!": [ "-fno-exceptions" ],
            "cflags_cc!": [ "-fno-exceptions" ],
            "include_dirs": [
                "<!@(node -p \"require('node-addon-api').include\")"
            ],
            "target_name": "index",
            "sources": [ "src/cpp/lib.cc" ],
            "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
            "cflags_cc": [
                "-std=c++17"
            ]
        }
    ]
}
