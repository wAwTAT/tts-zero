#include <iostream>
#include <curl/curl.h>
#include <string>
#include <windows.h>
#include <thread>
#include <atomic>
#include <conio.h>
#include <chrono>
#include <mutex>
#include <functional>
#include <vector>


namespace NetworkScanner {

    // 扫描完成后的回调函数类型
    using ScanCallback = std::function<void(const std::string& ip, bool reachable)>;

    std::mutex outputMutex;
    std::atomic<int> activeThreads(0);

    static void pingHost(const std::string& ip, const ScanCallback& callback) {
        activeThreads++;
        std::string command = "ping -n 1 -w 1000 " + ip + " > nul 2>&1";
        int result = std::system(command.c_str());

        callback(ip, result == 0);
        activeThreads--;
    }

    static void scanNetwork(const std::string& baseIP, int start, int end, const ScanCallback& callback) {
        std::vector<std::thread> threads;

        for (int i = start; i <= end; ++i) {
            std::string ip = baseIP + std::to_string(i);
            threads.emplace_back(pingHost, ip, callback);
        }

        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        // 等待所有线程完成
        while (activeThreads > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

} // namespace NetworkScanner

// 回调函数，用于处理响应（不需要内容）
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

// 检测指定IP是否可达
static bool checkConnection(const std::string& ip) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = "http://" + ip + ":7890/";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // 只请求头
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L); // 连接超时5秒

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    return (res == CURLE_OK && http_code == 200);
}

// 按键监听线程函数
static void keyListenerThread(std::atomic<bool>& exitRequested) {
    printf("按 'q' 键随时退出程序...\n");
    while (!exitRequested) {
        if (_kbhit() && _getch() == 'q') {
            exitRequested = true;
            printf("\n退出请求已接收，正在清理...\n");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {

    std::atomic<bool> exitRequested(false);

    // 启动按键监听线程
    std::thread keyListener(keyListenerThread, std::ref(exitRequested));

    // 开始扫描前检查退出请求
    if (exitRequested) {
        keyListener.join();
        return 0;
    }

    std::cout << "开始扫描 192.168.168.0/24 网段..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::atomic<int> reachableCount(0);
    std::mutex outputMutex;

    NetworkScanner::scanNetwork("192.168.168.", 1, 254,
        [&](const std::string& ip, bool reachable) {
            if (exitRequested) return; // 检查退出请求

            if (reachable) {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << ip << " 可达" << std::endl;
                reachableCount++;
            }
        });

    // 扫描完成后检查退出请求
    if (exitRequested) {
        keyListener.join();
        return 0;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    std::cout << "扫描完成，耗时: " << duration << " 秒" << std::endl;
    std::cout << "可达IP数量: " << reachableCount << std::endl;

    // 初始化 libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string targetIP;
    std::cout << "请输入目标IP地址：";
    std::cin >> targetIP;

    // 检查用户是否输入q请求退出
    if (targetIP == "q") {
        std::cout << "用户请求退出程序..." << std::endl;
        exitRequested = true;
    }

    // 循环检测连接，直到成功或用户请求退出
    while (!exitRequested && !checkConnection(targetIP)) {
        printf("无法连接到 %s，20s后重试...\n", targetIP.c_str());
        for (int i = 0; i < 20 && !exitRequested; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // 如果连接成功且用户没有请求退出，则执行TTS请求
    if (!exitRequested) {
        CURL* curl = nullptr;
        CURLcode res = CURLE_OK;

        curl = curl_easy_init();

        if (curl) {
            // 中文文本和音量参数
            std::string text = "这是零动的语音播放器";
            int volume = 80;

            // 对中文进行 URL 编码
            char* encoded_text = curl_easy_escape(curl, text.c_str(), static_cast<int>(text.length()));

            if (!encoded_text) {
                fprintf(stderr, "URL 编码失败\n");
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                keyListener.join();
                return 1;
            }

            // 构造完整 URL
            std::string url = "http://" + targetIP + ":7890/control/tts?str=" +
                std::string(encoded_text) +
                "&volume=" +
                std::to_string(volume);

            // 释放编码后的字符串
            curl_free(encoded_text);

            // 设置 cURL 选项
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

            // 设置超时时间（5秒）
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

            // 执行 HTTP GET 请求
            res = curl_easy_perform(curl);

            // 检查结果
            if (res != CURLE_OK) {
                fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
            }
            else {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                if (http_code == 200) {
                    printf("TTS 播放请求已成功发送\n");
                }
                else {
                    fprintf(stderr, "HTTP 错误代码: %ld\n", http_code);
                }
            }

            // 清理资源
            curl_easy_cleanup(curl);
        }
        else {
            fprintf(stderr, "无法初始化 cURL\n");
        }
    }

    // 清理并等待按键监听线程结束
    curl_global_cleanup();
    keyListener.join();

    return 0;
}

