#include <cassert>
#include <iostream>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

// --- Mocks to simulate ESP-IDF environment for host testing ---
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

struct httpd_req_t {
    const char* mock_content_type;
};

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* req, const char* field, char* val, size_t val_size) {
    if (strcmp(field, "Content-Type") == 0 && req->mock_content_type != nullptr) {
        strncpy(val, req->mock_content_type, val_size - 1);
        val[val_size - 1] = '\0';
        return ESP_OK;
    }
    return ESP_FAIL;
}

// --- The function to test (extracted from webui.cpp) ---
bool content_type_ok(httpd_req_t* req) {
  char ct[64] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof ct) != ESP_OK)
    return false;
  return strncasecmp(ct, "application/json", 16) == 0;
}

// --- Test Cases ---
int main() {
    httpd_req_t req;

    // Test 1: Exact valid match
    req.mock_content_type = "application/json";
    assert(content_type_ok(&req) == true);
    
    // Test 2: Valid match with mixed case
    req.mock_content_type = "aPpLiCaTiOn/JsOn";
    assert(content_type_ok(&req) == true);

    // Test 3: Valid match with trailing charset parameters
    req.mock_content_type = "application/json; charset=utf-8";
    assert(content_type_ok(&req) == true);

    // Test 4: Invalid content type
    req.mock_content_type = "text/plain";
    assert(content_type_ok(&req) == false);

    // Test 5: Missing header entirely
    req.mock_content_type = nullptr;
    assert(content_type_ok(&req) == false);
    
    // Test 6: Empty header
    req.mock_content_type = "";
    assert(content_type_ok(&req) == false);

    std::cout << "All content_type_ok (Test 4.2) tests passed!" << std::endl;
    return 0;
}
