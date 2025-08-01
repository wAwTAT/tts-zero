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

    // ɨ����ɺ�Ļص���������
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

        // �ȴ������߳����
        while (activeThreads > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

} // namespace NetworkScanner

// �ص����������ڴ�����Ӧ������Ҫ���ݣ�
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

// ���ָ��IP�Ƿ�ɴ�
static bool checkConnection(const std::string& ip) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = "http://" + ip + ":7890/";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // ֻ����ͷ
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L); // ���ӳ�ʱ5��

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    return (res == CURLE_OK && http_code == 200);
}

// ���������̺߳���
static void keyListenerThread(std::atomic<bool>& exitRequested) {
    printf("�� 'q' ����ʱ�˳�����...\n");
    while (!exitRequested) {
        if (_kbhit() && _getch() == 'q') {
            exitRequested = true;
            printf("\n�˳������ѽ��գ���������...\n");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {

    std::atomic<bool> exitRequested(false);

    // �������������߳�
    std::thread keyListener(keyListenerThread, std::ref(exitRequested));

    // ��ʼɨ��ǰ����˳�����
    if (exitRequested) {
        keyListener.join();
        return 0;
    }

    std::cout << "��ʼɨ�� 192.168.168.0/24 ����..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::atomic<int> reachableCount(0);
    std::mutex outputMutex;

    NetworkScanner::scanNetwork("192.168.168.", 1, 254,
        [&](const std::string& ip, bool reachable) {
            if (exitRequested) return; // ����˳�����

            if (reachable) {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << ip << " �ɴ�" << std::endl;
                reachableCount++;
            }
        });

    // ɨ����ɺ����˳�����
    if (exitRequested) {
        keyListener.join();
        return 0;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    std::cout << "ɨ����ɣ���ʱ: " << duration << " ��" << std::endl;
    std::cout << "�ɴ�IP����: " << reachableCount << std::endl;

    // ��ʼ�� libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string targetIP;
    std::cout << "������Ŀ��IP��ַ��";
    std::cin >> targetIP;

    // ����û��Ƿ�����q�����˳�
    if (targetIP == "q") {
        std::cout << "�û������˳�����..." << std::endl;
        exitRequested = true;
    }

    // ѭ��������ӣ�ֱ���ɹ����û������˳�
    while (!exitRequested && !checkConnection(targetIP)) {
        printf("�޷����ӵ� %s��20s������...\n", targetIP.c_str());
        for (int i = 0; i < 20 && !exitRequested; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // ������ӳɹ����û�û�������˳�����ִ��TTS����
    if (!exitRequested) {
        CURL* curl = nullptr;
        CURLcode res = CURLE_OK;

        curl = curl_easy_init();

        if (curl) {
            // �����ı�����������
            std::string text = "�����㶯������������";
            int volume = 80;

            // �����Ľ��� URL ����
            char* encoded_text = curl_easy_escape(curl, text.c_str(), static_cast<int>(text.length()));

            if (!encoded_text) {
                fprintf(stderr, "URL ����ʧ��\n");
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                keyListener.join();
                return 1;
            }

            // �������� URL
            std::string url = "http://" + targetIP + ":7890/control/tts?str=" +
                std::string(encoded_text) +
                "&volume=" +
                std::to_string(volume);

            // �ͷű������ַ���
            curl_free(encoded_text);

            // ���� cURL ѡ��
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

            // ���ó�ʱʱ�䣨5�룩
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

            // ִ�� HTTP GET ����
            res = curl_easy_perform(curl);

            // �����
            if (res != CURLE_OK) {
                fprintf(stderr, "����ʧ��: %s\n", curl_easy_strerror(res));
            }
            else {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                if (http_code == 200) {
                    printf("TTS ���������ѳɹ�����\n");
                }
                else {
                    fprintf(stderr, "HTTP �������: %ld\n", http_code);
                }
            }

            // ������Դ
            curl_easy_cleanup(curl);
        }
        else {
            fprintf(stderr, "�޷���ʼ�� cURL\n");
        }
    }

    // �����ȴ����������߳̽���
    curl_global_cleanup();
    keyListener.join();

    return 0;
}

