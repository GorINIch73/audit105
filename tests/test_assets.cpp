#include "PlatformUtils.h"

#include <cstdio>
#include <string>

int main() {
    const char* assets[] = {
        "Roboto-Regular.ttf",
        "fa-solid-900.otf",
        "audit105.svg",
        "manual_oms_terms.csv",
    };

    int failed = 0;
    for (const char* asset : assets) {
        std::string path = AssetPath(asset);
        if (path.empty()) {
            printf("fail: asset %s not found from this location\n", asset);
            failed = 1;
        } else {
            printf("ok: %s -> %s\n", asset, path.c_str());
        }
    }

    printf(failed ? "test_assets: FAILED\n" : "test_assets: OK\n");
    return failed;
}
