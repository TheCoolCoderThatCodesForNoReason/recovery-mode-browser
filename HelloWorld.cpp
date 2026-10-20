
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

std::string toUTF8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(
        CP_UTF8, 0, s.data(), (int)s.size(),
        nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, s.data(), (int)s.size(),
        out.data(), n, nullptr, nullptr);
    return out;
}

void error(const char* message) {
    std::cerr << "\n" << message
              << " (Windows error " << GetLastError()
              << ")\n";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::wcout << L"================================\n"
               << L"   RECOVERY TEXT BROWSER\n"
               << L"================================\n";

    for (;;) {
        std::wstring method, url, body;

        std::wcout << L"\nGET, POST, or EXIT: ";
        std::getline(std::wcin, method);

        if (method == L"EXIT" || method == L"exit")
            break;

        for (auto& c : method)
            c = towupper(c);

        if (method != L"GET" && method != L"POST") {
            std::wcout << L"Choose GET or POST.\n";
            continue;
        }

        std::wcout << L"URL (include https://): ";
        std::getline(std::wcin, url);

        if (url.empty()) continue;

        if (method == L"POST") {
            std::wcout << L"Content-Type "
                << L"(Enter for form data): ";
            std::wstring contentType;
            std::getline(std::wcin, contentType);

            if (contentType.empty())
                contentType =
                    L"application/x-www-form-urlencoded";

            std::wcout << L"POST body (one line): ";
            std::getline(std::wcin, body);

            // Header value is supplied by the user.
            // Reject line breaks to prevent header injection.
            if (contentType.find_first_of(L"\r\n")
                != std::wstring::npos) {
                std::wcout << L"Invalid Content-Type.\n";
                continue;
            }
        }

        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = (DWORD)-1;
        uc.dwHostNameLength = (DWORD)-1;
        uc.dwUrlPathLength = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) ||
            (uc.nScheme != INTERNET_SCHEME_HTTP &&
             uc.nScheme != INTERNET_SCHEME_HTTPS)) {
            std::wcout << L"Invalid URL. Use HTTP or HTTPS.\n";
            continue;
        }

        std::wstring host(
            uc.lpszHostName, uc.dwHostNameLength);

        std::wstring path(
            uc.lpszUrlPath, uc.dwUrlPathLength);

        if (path.empty()) path = L"/";

        if (uc.dwExtraInfoLength)
            path.append(
                uc.lpszExtraInfo, uc.dwExtraInfoLength);

        DWORD flags =
            uc.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE : 0;

        HINTERNET session = WinHttpOpen(
            L"RecoveryTextBrowser/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);

        if (!session) {
            error("WinHttpOpen failed");
            continue;
        }

        WinHttpSetTimeouts(session, 10000, 10000,
                           15000, 15000);

        HINTERNET connection = WinHttpConnect(
            session, host.c_str(), uc.nPort, 0);

        if (!connection) {
            error("Connection setup failed");
            WinHttpCloseHandle(session);
            continue;
        }

        HINTERNET request = WinHttpOpenRequest(
            connection, method.c_str(), path.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

        if (!request) {
            error("Request setup failed");
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            continue;
        }

        std::string data = toUTF8(body);
        std::wstring headers;

        if (method == L"POST") {
            std::wstring ct;
            std::wcout << L"Using form content type by default "
                       << L"unless customized in source.\n";
            ct = L"application/x-www-form-urlencoded";
            headers = L"Content-Type: " + ct + L"\r\n";
        }

        BOOL sent = WinHttpSendRequest(
            request,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                            : headers.c_str(),
            headers.empty() ? 0 : (DWORD)-1,
            method == L"POST" && !data.empty()
                ? (LPVOID)data.data()
                : WINHTTP_NO_REQUEST_DATA,
            method == L"POST" ? (DWORD)data.size() : 0,
            method == L"POST" ? (DWORD)data.size() : 0,
            0);

        if (!sent || !WinHttpReceiveResponse(
                         request, nullptr)) {
            error("HTTP request failed");
        } else {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);

            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                    WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status, &statusSize,
                WINHTTP_NO_HEADER_INDEX);

            std::wcout << L"\nHTTP status: "
                       << status << L"\n";
            std::wcout << L"----- RESPONSE BODY -----\n";

            DWORD available = 0;

            while (WinHttpQueryDataAvailable(
                       request, &available) &&
                   available > 0) {
                std::vector<char> buffer(available);
                DWORD received = 0;

                if (!WinHttpReadData(
                        request, buffer.data(),
                        available, &received))
                    break;

                std::cout.write(
                    buffer.data(), received);
                std::cout.flush();
            }

            std::cout << "\n\n";
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
    }

    return 0;
}
