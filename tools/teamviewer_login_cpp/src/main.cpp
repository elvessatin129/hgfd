#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <optional>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

// Minimal JSON helper (very basic) just for small payloads
static std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), count);
    return w;
}

static std::string wideToUtf8(const std::wstring &w) {
    if (w.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), count, nullptr, nullptr);
    return s;
}

struct HttpResponse {
    int statusCode {0};
    std::string body;
};

static std::optional<HttpResponse> httpRequestJson(
    const std::wstring &host,
    INTERNET_PORT port,
    const std::wstring &verb,
    const std::wstring &path,
    const std::string &jsonBody
) {
    HINTERNET hSession = WinHttpOpen(L"TVLogin/1.0",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return std::nullopt;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return std::nullopt; }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        verb.c_str(),
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (port == INTERNET_DEFAULT_HTTPS_PORT) ? WINHTTP_FLAG_SECURE : 0
    );
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return std::nullopt; }

    std::wstring headers = L"Content-Type: application/json\r\n";
    std::wstring wBody = utf8ToWide(jsonBody);

    BOOL sent = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)-1L,
        (LPVOID)wBody.c_str(),
        (DWORD)(wBody.size() * sizeof(wchar_t)),
        (DWORD)(wBody.size() * sizeof(wchar_t)),
        0
    );

    if (!sent) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    DWORD statusCode = 0; DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

    std::string response;
    DWORD dwSize = 0;
    do {
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        std::vector<char> buffer(dwSize);
        DWORD dwDownloaded = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
        response.append(buffer.data(), dwDownloaded);
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return HttpResponse{ (int)statusCode, response };
}

static std::optional<std::string> startSession(const std::wstring &host, INTERNET_PORT port) {
    auto res = httpRequestJson(host, port, L"POST", L"/session", "{\"capabilities\":{}}" );
    if (!res || res->statusCode >= 400) return std::nullopt;
    return res->body;
}

static std::optional<std::string> post(const std::wstring &host, INTERNET_PORT port, const std::string &pathUtf8, const std::string &payload) {
    return httpRequestJson(host, port, L"POST", utf8ToWide(pathUtf8), payload);
}

int wmain() {
    const wchar_t* wDriver = _wgetenv(L"WEBDRIVER_HOST");
    const wchar_t* wPort   = _wgetenv(L"WEBDRIVER_PORT");
    const wchar_t* wUser   = _wgetenv(L"TV_USERNAME");
    const wchar_t* wPass   = _wgetenv(L"TV_PASSWORD");

    if (!wDriver || !wPort || !wUser || !wPass) {
        std::wcerr << L"Set WEBDRIVER_HOST, WEBDRIVER_PORT, TV_USERNAME, TV_PASSWORD" << std::endl;
        return 2;
    }

    std::wstring host = wDriver;
    INTERNET_PORT port = (INTERNET_PORT)std::stoi(wideToUtf8(wPort));

    auto sessionRes = startSession(host, port);
    if (!sessionRes) {
        std::cerr << "Failed to start WebDriver session" << std::endl;
        return 3;
    }

    // NOTE: Real implementation should parse JSON and extract sessionId.
    // For brevity, we will assume Chromedriver echo contains "sessionId":"..." and paths use it.
    std::string body = *sessionRes;
    std::string key = "\"sessionId\":\"";
    auto p = body.find(key);
    if (p == std::string::npos) { std::cerr << "No sessionId" << std::endl; return 4; }
    p += key.size();
    auto q = body.find("\"", p);
    if (q == std::string::npos) { std::cerr << "Bad sessionId" << std::endl; return 4; }
    std::string sessionId = body.substr(p, q - p);

    auto go = post(host, port, "/session/" + sessionId + "/url", std::string("{\"url\":\"https://login.teamviewer.com/\"}"));
    if (!go || go->statusCode >= 400) { std::cerr << "Failed to navigate" << std::endl; return 5; }

    auto findUser = post(host, port, "/session/" + sessionId + "/element", std::string("{\"using\":\"css selector\",\"value\":\"input#username\"}"));
    if (!findUser || findUser->statusCode >= 400) { std::cerr << "Username field not found" << std::endl; return 6; }

    std::string userBody = *findUser;
    std::string elemKey = "\"ELEMENT\":\""; // legacy
    std::string weKey = "\"element-6066-11e4-a52e-4f735466cecf\":\""; // W3C
    std::string elemId;
    auto pu = userBody.find(weKey);
    if (pu != std::string::npos) {
        pu += weKey.size();
        auto qu = userBody.find("\"", pu);
        elemId = userBody.substr(pu, qu - pu);
    } else {
        pu = userBody.find(elemKey);
        if (pu == std::string::npos) { std::cerr << "Bad element" << std::endl; return 6; }
        pu += elemKey.size();
        auto qu = userBody.find("\"", pu);
        elemId = userBody.substr(pu, qu - pu);
    }

    std::wstring wUserVal = wUser;
    std::string userUtf8 = wideToUtf8(wUserVal);
    auto sendUser = post(host, port, "/session/" + sessionId + "/element/" + elemId + "/value",
                         std::string("{\"text\":\"") + userUtf8 + "\",\"value\":[\"" + userUtf8 + "\"]}");
    if (!sendUser || sendUser->statusCode >= 400) { std::cerr << "Failed to type username" << std::endl; return 7; }

    auto findPass = post(host, port, "/session/" + sessionId + "/element", std::string("{\"using\":\"css selector\",\"value\":\"input[type=password]\"}"));
    if (!findPass || findPass->statusCode >= 400) { std::cerr << "Password field not found" << std::endl; return 8; }

    std::string passBody = *findPass;
    std::string passElemId;
    auto pp = passBody.find(weKey);
    if (pp != std::string::npos) {
        pp += weKey.size();
        auto qp = passBody.find("\"", pp);
        passElemId = passBody.substr(pp, qp - pp);
    } else {
        pp = passBody.find(elemKey);
        if (pp == std::string::npos) { std::cerr << "Bad password element" << std::endl; return 8; }
        pp += elemKey.size();
        auto qp = passBody.find("\"", pp);
        passElemId = passBody.substr(pp, qp - pp);
    }

    std::wstring wPassVal = wPass;
    std::string passUtf8 = wideToUtf8(wPassVal);
    auto sendPass = post(host, port, "/session/" + sessionId + "/element/" + passElemId + "/value",
                         std::string("{\"text\":\"") + passUtf8 + "\",\"value\":[\"" + passUtf8 + "\"]}");
    if (!sendPass || sendPass->statusCode >= 400) { std::cerr << "Failed to type password" << std::endl; return 9; }

    auto findBtn = post(host, port, "/session/" + sessionId + "/element", std::string("{\"using\":\"css selector\",\"value\":\"button[type=submit]\"}"));
    if (!findBtn || findBtn->statusCode >= 400) { std::cerr << "Login button not found" << std::endl; return 10; }

    std::string btnBody = *findBtn;
    std::string btnElemId;
    auto pb = btnBody.find(weKey);
    if (pb != std::string::npos) {
        pb += weKey.size();
        auto qb = btnBody.find("\"", pb);
        btnElemId = btnBody.substr(pb, qb - pb);
    } else {
        pb = btnBody.find(elemKey);
        if (pb == std::string::npos) { std::cerr << "Bad button element" << std::endl; return 10; }
        pb += elemKey.size();
        auto qb = btnBody.find("\"", pb);
        btnElemId = btnBody.substr(pb, qb - pb);
    }

    auto click = post(host, port, "/session/" + sessionId + "/element/" + btnElemId + "/click", "{}");
    if (!click || click->statusCode >= 400) { std::cerr << "Failed to click login" << std::endl; return 11; }

    std::cout << "Login flow triggered. Check browser window." << std::endl;
    return 0;
}
