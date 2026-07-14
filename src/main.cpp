#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <queue>          
#include <condition_variable> 
#include <stdexcept>     
#include <cstdlib>  
#include <chrono>     
#include <iomanip>
#include <winsock2.h>
#include <windows.h>

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

// GBK转UTF-8（用于命令行输入）
std::string gbk_to_utf8(const std::string& gbk_str) {
    int len = MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, nullptr, 0);
    wchar_t* wstr = new wchar_t[len + 1];
    MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, wstr, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    char* utf8_str = new char[len + 1];
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8_str, len, nullptr, nullptr);
    std::string result(utf8_str);
    delete[] wstr;
    delete[] utf8_str;
    return result;
}

// UTF-8转GBK（用于命令行输出）
std::string utf8_to_gbk(const std::string& utf8_str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    wchar_t* wstr = new wchar_t[len + 1];
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wstr, len);
    len = WideCharToMultiByte(CP_ACP, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    char* gbk_str = new char[len + 1];
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, gbk_str, len, nullptr, nullptr);
    std::string result(gbk_str);
    delete[] wstr;
    delete[] gbk_str;
    return result;
}

// 环境变量配置：从系统环境变量读取API Key
std::string get_api_key() {
    const char* key = std::getenv("DEEPSEEK_API_KEY");
    if (!key) {
        throw std::runtime_error("API Key not set");
    }
    return std::string(key);
}

// 配置常量
const std::string DEEPSEEK_BASE_URL = "https://api.deepseek.com";
const int SERVER_PORT = 8080;

std::mutex g_log_mutex; // 全局互斥锁，保护多线程下的控制台输出

// 结构化日志：信息级
void log_info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &time);

    std::cout << "["
              << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
              << "] [INFO] "
              << msg
              << std::endl;
}

// 结构化日志：错误级
void log_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &time);

    std::cerr << "["
              << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
              << "] [ERROR] "
              << msg
              << std::endl;
}

// 简易线程池：隔离IO等待，避免单请求阻塞服务
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount) : stop(false) {
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this]() {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this]() { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    // 将任务加入线程池队列
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.push(task);
        }
        condition.notify_one();
    }

    // 析构时优雅停止所有线程，避免资源泄漏
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// 转发请求到LLM API
std::string forward_to_llm(const std::string& prompt) {
    log_info("Forwarding request to LLM. Prompt length: " + std::to_string(prompt.length()));
    
    // 修正：使用变量而非字符串字面量
    httplib::Client client(DEEPSEEK_BASE_URL.c_str()); 
    client.set_connection_timeout(15, 0);
    client.set_read_timeout(60, 0);

    // 构造符合DeepSeek API规范的请求体
    json request_body = {
        {"model", "deepseek-chat"},
        {"messages", {{{"role", "user"}, {"content", prompt}}}},
        {"stream", false}
    };

    // 构造请求头，携带鉴权信息
    httplib::Headers headers = {
        {"Authorization", "Bearer " + get_api_key()},
        {"Content-Type", "application/json"}
    };

    // 发送POST请求到DeepSeek聊天接口
    auto res = client.Post("/chat/completions", headers, request_body.dump(), "application/json");

    if (!res || res->status != 200) {
        std::string err_msg = "LLM API Error: Status " + std::to_string(res ? res->status : -1);
        log_error(err_msg);
        return R"({"error": "Failed to receive LLM response"})";
    }

    try {
        auto json_response = json::parse(res->body);
        std::string reply = json_response["choices"][0]["message"]["content"].get<std::string>();
        log_info("Received reply from LLM. Length: " + std::to_string(reply.length()));
        return reply;
    } catch (...) {
        log_error("Failed to parse LLM JSON response.");
        return R"({"error": "Failed to parse LLM response"})";
    }
}

int main() {
    // ===== 启动检查：确保API Key已正确配置 =====
    try {
        get_api_key();
        log_info("API Key loaded.");
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }

    httplib::Server svr;
    ThreadPool pool(4);

    // ===== HTTP路由：处理聊天请求 =====
    svr.Post("/chat", [&pool](const httplib::Request& req, httplib::Response& res) {
        log_info("HTTP request from: " + req.remote_addr);
        pool.enqueue([req, &res]() {
            try {
                auto client_req = json::parse(req.body);
                std::string prompt = client_req.value("prompt", "");
                if (prompt.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"empty prompt"})", "application/json");
                    return;
                }
                std::string reply = forward_to_llm(prompt);
                res.set_content(json{{"reply", reply}}.dump(), "application/json; charset=utf-8");
            } catch (const json::parse_error& e) {
                log_error(std::string("JSON Parse Error: ") + e.what());
                res.status = 400;
                res.set_content(R"({"error":"invalid json"})", "application/json");
            } catch (const std::exception& e) {
                log_error(std::string("Internal Error: ") + e.what());
                res.status = 500;
                res.set_content(R"({"error":"internal server error"})", "application/json");
            }
        });
    });

    // ===== HTTP路由：健康检查 =====
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("LLM Proxy is running", "text/plain");
    });

    // ===== 启动HTTP服务（后台线程运行，不阻塞主线程）=====
    std::thread server_thread([&]() {
        log_info("HTTP server started at http://0.0.0.0:8080");
        if (!svr.listen("0.0.0.0", 8080)) {
            log_error("Failed to start HTTP server");
        }
    });

    // ===== 主线程：命令行交互模式（调试用）=====
    std::cout << "===================================\n";
    std::cout << "  LLM Proxy - DeepSeek Interactive\n";
    std::cout << "  Type 'exit' to quit\n";
    std::cout << "===================================\n\n";

    std::string prompt;
    while (true) {
        std::cout << ">>> ";
        std::getline(std::cin, prompt);

        if (prompt == "exit") {
            break;
        }
        if (prompt.empty()) {
            continue;
        }

        // 将命令行输入的GBK编码转换为UTF-8，适配API要求
        std::string utf8_prompt = gbk_to_utf8(prompt);
        
        // 直接调用LLM接口获取回复
        std::string utf8_reply = forward_to_llm(utf8_prompt);
        
        std::cout << "\n--- Generating Reply ---\n";
        // 将API返回的UTF-8编码回复转换为GBK，适配Windows命令行显示
        std::cout << utf8_to_gbk(utf8_reply) << "\n";
        std::cout << "-------------------------\n";
    }

    // ===== 优雅退出：停止HTTP服务，回收线程资源 =====
    log_info("Shutting down HTTP server...");
    svr.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    log_info("Goodbye!");
    return 0;
}
