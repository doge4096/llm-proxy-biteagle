#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <sstream>
#include <functional>
#include <queue>
#include <condition_variable>
#include <stdexcept>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

// ==================== 编码转换 ====================
// Windows 中文终端需要 GBK ↔ UTF-8，Linux 终端原生 UTF-8
#ifdef _WIN32
std::string gbk_to_utf8(const std::string& gbk_str) {
    int len = MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, nullptr, 0);
    wchar_t* wstr = new wchar_t[len + 1];
    MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, wstr, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    char* utf8_str = new char[len + 1];
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8_str, len, nullptr, nullptr);
    std::string result(utf8_str);
    delete[] wstr; delete[] utf8_str;
    return result;
}

std::string utf8_to_gbk(const std::string& utf8_str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    wchar_t* wstr = new wchar_t[len + 1];
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wstr, len);
    len = WideCharToMultiByte(CP_ACP, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    char* gbk_str = new char[len + 1];
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, gbk_str, len, nullptr, nullptr);
    std::string result(gbk_str);
    delete[] wstr; delete[] gbk_str;
    return result;
}
#else
// Linux / macOS：终端已是 UTF-8，直接透传
std::string gbk_to_utf8(const std::string& s) { return s; }
std::string utf8_to_gbk(const std::string& s) { return s; }
#endif

// ==================== Provider 抽象 ====================
struct ProviderConfig {
    std::string name;             // "deepseek" | "ollama"
    std::string display_name;     // 前端显示名
    std::string base_url;         // host:port，不含路径
    std::string model;            // 模型名
    std::string api_key;          // API Key，本地模型为空
    std::string path_prefix;      // 路径前缀，如 "/v1"（Ollama 需要）
    bool use_ssl;                 // 是否用 HTTPS
    bool supports_thinking;       // 是否支持思维链
    std::string reasoning_effort; // "high" | "medium" | "low" | ""
};

// Provider 注册表
std::vector<ProviderConfig> build_providers() {
    std::vector<ProviderConfig> providers;

    // DeepSeek
    ProviderConfig deepseek;
    deepseek.name = "deepseek";
    deepseek.display_name = "DeepSeek v4-pro";
    deepseek.base_url = "https://api.deepseek.com";
    deepseek.model = "deepseek-v4-pro";
    const char* key = std::getenv("DEEPSEEK_API_KEY");
    deepseek.api_key = key ? std::string(key) : "";
    deepseek.path_prefix = "";
    deepseek.use_ssl = true;
    deepseek.supports_thinking = true;
    deepseek.reasoning_effort = "high";
    providers.push_back(deepseek);

    // Ollama 本地模型
    ProviderConfig ollama;
    ollama.name = "ollama";
    ollama.display_name = "Ollama (qwen2.5:3b)";
    ollama.base_url = "http://localhost:11434";
    ollama.model = "qwen2.5:3b";
    ollama.api_key = "";
    ollama.path_prefix = "/v1";
    ollama.use_ssl = false;
    ollama.supports_thinking = false;
    ollama.reasoning_effort = "";
    providers.push_back(ollama);

    return providers;
}

const std::vector<ProviderConfig> PROVIDERS = build_providers();

// 按 name 查找 provider，找不到返回默认（第一个）
const ProviderConfig& get_provider(const std::string& name) {
    for (auto& p : PROVIDERS)
        if (p.name == name) return p;
    return PROVIDERS[0];
}

// ==================== 日志 ====================
const int SERVER_PORT = 8080;
std::mutex g_log_mutex;

void log_info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::cout << "[" << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << "] [INFO] " << msg << std::endl;
}

void log_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::cerr << "[" << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << "] [ERROR] " << msg << std::endl;
}

// ==================== 线程池 ====================
class ThreadPool {
public:
    explicit ThreadPool(size_t n) : stop(false) {
        for (size_t i = 0; i < n; ++i)
            workers.emplace_back([this]() {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this]() { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front()); tasks.pop();
                    }
                    task();
                }
            });
    }
    void enqueue(std::function<void()> task) {
        std::lock_guard<std::mutex> l(queue_mutex); tasks.push(task);
        condition.notify_one();
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> l(queue_mutex); stop = true; }
        condition.notify_all();
        for (auto& w : workers) w.join();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// ==================== 客户端流式（多 Provider）====================
bool forward_to_llm_stream(
    const ProviderConfig& provider,
    const std::string& prompt,
    std::string* full_reply = nullptr,
    std::function<void(const std::string&, const std::string&)> on_chunk = nullptr
) {
    std::mutex cb_mutex;
    std::string buffer;

    httplib::Client client(provider.base_url.c_str());
    client.set_connection_timeout(15, 0);
    client.set_read_timeout(300, 0);
    if (!provider.use_ssl)
        client.enable_server_certificate_verification(false);

    // 构建请求头
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"}
    };
    if (!provider.api_key.empty())
        headers.emplace("Authorization", "Bearer " + provider.api_key);

    // 构建请求体
    json request_body = {
        {"model", provider.model},
        {"messages", {
            {{"role", "user"}, {"content", prompt}}
        }},
        {"stream", true}
    };
    if (provider.supports_thinking) {
        request_body["thinking"] = {{"type", "enabled"}};
        request_body["reasoning_effort"] = provider.reasoning_effort;
    }

    log_info("[" + provider.name + "] Start streaming. Prompt length: " + std::to_string(prompt.length()));

    std::string api_path = provider.path_prefix + "/chat/completions";
    auto res = client.Post(
        api_path.c_str(),
        headers,
        request_body.dump(),
        "application/json",
        [&](const char* data, size_t len) -> bool {
            std::lock_guard<std::mutex> lock(cb_mutex);
            buffer.append(data, len);
            size_t pos;
            while ((pos = buffer.find("\n\n")) != std::string::npos) {
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);
                if (chunk.find("data: ") != 0) continue;
                std::string json_str = chunk.substr(6);
                if (json_str == "[DONE]") {
                    log_info("[" + provider.name + "] Stream finished.");
                    return false;
                }
                try {
                    json j = json::parse(json_str);
                    if (j.contains("choices") && !j["choices"].empty()) {
                        auto& delta = j["choices"][0]["delta"];

                        // 思维链（仅 DeepSeek 等支持 thinking 的模型有）
                        std::string reasoning_chunk;
                        if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                            reasoning_chunk = delta["reasoning_content"].get<std::string>();
                        }

                        // 正式回复
                        std::string content_chunk;
                        if (delta.contains("content") && delta["content"].is_string()) {
                            content_chunk = delta["content"].get<std::string>();
                            if (full_reply) *full_reply += content_chunk;
                        }

                        if (on_chunk && (!reasoning_chunk.empty() || !content_chunk.empty())) {
                            on_chunk(reasoning_chunk, content_chunk);
                        }
                    }
                } catch (const json::parse_error& e) {
                    log_error("JSON parse error in chunk: " + std::string(e.what()));
                    continue;
                }
            }
            if (buffer.length() > 1024 * 1024) { log_error("Buffer overflow, reset"); buffer.clear(); }
            return true;
        },
        nullptr
    );

    if (!res) {
        return true;
    } else {
        log_error("[" + provider.name + "] HTTP " + std::to_string(res->status));
        if (full_reply) *full_reply = R"({"error":"HTTP )" + std::to_string(res->status) + R"("})";
        return false;
    }
}

// 默认 provider 同步接口
std::string forward_to_llm_sync(const std::string& prompt) {
    std::string r;
    forward_to_llm_stream(PROVIDERS[0], prompt, &r);
    return r;
}

// ==================== main ====================
int main() {
    // 启动时列出可用 provider
    log_info("Available providers:");
    for (auto& p : PROVIDERS)
        log_info("  " + p.name + " → " + p.display_name + " @ " + p.base_url);

    // 检查 DeepSeek API Key（仅警告，不退出，因为有 Ollama）
    if (PROVIDERS[0].api_key.empty())
        log_error("DEEPSEEK_API_KEY not set — DeepSeek provider will fail");
    else
        log_info("DeepSeek API Key loaded");

    httplib::Server svr;
    ThreadPool pool(4);

    // --- 同步接口 ---
    svr.Post("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        pool.enqueue([&, req, &res]() {
            try {
                auto jreq = json::parse(req.body);
                std::string prompt = jreq.value("prompt", "");
                std::string provider_name = jreq.value("provider", "deepseek");
                if (prompt.empty()) { res.status = 400; res.set_content(R"({"error":"empty"})","application/json"); return; }
                auto& p = get_provider(provider_name);
                std::string reply;
                forward_to_llm_stream(p, prompt, &reply);
                res.set_content(json{{"reply", reply}}.dump(), "application/json; charset=utf-8");
            } catch (...) { res.status = 500; res.set_content(R"({"error":"internal"})","application/json"); }
        });
    });

    // --- SSE 流式接口 ---
    svr.Post("/chat/stream", [&](const httplib::Request& req, httplib::Response& res) {
        // SSE 头
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");

        struct StreamState {
            std::mutex mtx;
            std::condition_variable cv;
            std::vector<std::pair<std::string, std::string>> chunks;
            bool done = false;
        };
        auto state = std::make_shared<StreamState>();

        // ★ 必须在 handler 返回前设置，httplib 才会保持长连接
        res.set_content_provider(
            "text/event-stream",
            [state](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                std::string reasoning, content;
                bool is_done = false;

                {
                    std::unique_lock<std::mutex> l(state->mtx);
                    state->cv.wait(l, [&]() { return !state->chunks.empty() || state->done; });

                    if (!state->chunks.empty()) {
                        reasoning = std::move(state->chunks[0].first);
                        content = std::move(state->chunks[0].second);
                        state->chunks.erase(state->chunks.begin());
                    }
                    is_done = state->done && state->chunks.empty();
                }

                if (!reasoning.empty() || !content.empty()) {
                    json j;
                    j["delta"] = json::object();
                    if (!reasoning.empty()) j["delta"]["reasoning_content"] = reasoning;
                    if (!content.empty()) j["delta"]["content"] = content;
                    std::string sse = "data: " + j.dump() + "\n\n";
                    sink.write(sse.data(), sse.size());
                }

                if (is_done) {
                    sink.write("data: [DONE]\n\n", strlen("data: [DONE]\n\n"));
                    return false;
                }
                return true;
            },
            nullptr
        );

        // 异步收 LLM 流，喂入 state
        std::string prompt_body = req.body;
        pool.enqueue([state, prompt_body]() {
            try {
                auto jreq = json::parse(prompt_body);
                std::string prompt = jreq.value("prompt", "");
                std::string provider_name = jreq.value("provider", "deepseek");
                if (prompt.empty()) {
                    std::lock_guard<std::mutex> l(state->mtx);
                    state->done = true;
                    state->cv.notify_one();
                    return;
                }

                auto& p = get_provider(provider_name);
                log_info("SSE request → provider: " + p.name + ", prompt: " + prompt.substr(0, 50) + "...");

                forward_to_llm_stream(p, prompt, nullptr, [state](const std::string& reasoning, const std::string& content) {
                    std::lock_guard<std::mutex> l(state->mtx);
                    state->chunks.push_back({reasoning, content});
                    state->cv.notify_one();
                });

                std::lock_guard<std::mutex> l(state->mtx);
                state->done = true;
                state->cv.notify_one();
            } catch (const std::exception& e) {
                log_error("SSE error: " + std::string(e.what()));
                std::lock_guard<std::mutex> l(state->mtx);
                state->done = true;
                state->cv.notify_one();
            }
        });
    });

    // --- CORS 预检 ---
    svr.Options("/chat/stream", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // --- 模型列表 ---
    svr.Get("/models", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (auto& p : PROVIDERS) {
            json obj;
            obj["id"] = p.name;
            obj["name"] = p.display_name;
            obj["model"] = p.model;
            obj["supports_thinking"] = p.supports_thinking;
            arr.push_back(obj);
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // --- CORS 预检 models ---
    svr.Options("/models", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // --- 健康检查 ---
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("LLM Proxy (httplib 0.50.1, SSE OK)", "text/plain");
    });

    // ==================== CLI ====================
    enum class Mode { STREAM, SYNC };
    std::cout << "===================================\n";
    std::cout << "  LLM Proxy - Multi-Provider\n";
    std::cout << "===================================\n";
    std::cout << "Providers:\n";
    for (auto& p : PROVIDERS)
        std::cout << "  " << p.name << " → " << p.display_name << "\n";
    std::cout << "\n1. Stream (typing)\n2. Sync (full)\nChoice: ";
    Mode mode = Mode::STREAM;
    std::string c; std::getline(std::cin, c);
    if (c == "2") mode = Mode::SYNC;
    std::cout << "Mode: " << (mode == Mode::STREAM ? "Stream" : "Sync") << "\n\n";
    std::cout << "/mode stream | /mode sync | /model deepseek | /model ollama | exit\n\n";

    // 默认用第一个 provider（DeepSeek）
    size_t provider_idx = 0;

    std::thread srv_thr([&]() {
        log_info("HTTP on :" + std::to_string(SERVER_PORT));
        svr.listen("0.0.0.0", SERVER_PORT);
    });

    std::string prompt;
    while (true) {
        std::cout << ">>> "; std::getline(std::cin, prompt);
        if (prompt == "exit") break;
        if (prompt.empty()) continue;
        if (prompt == "/mode stream") { mode = Mode::STREAM; std::cout << "→ Stream\n"; continue; }
        if (prompt == "/mode sync")   { mode = Mode::SYNC;  std::cout << "→ Sync\n";  continue; }

        // /model 切换
        if (prompt.substr(0, 7) == "/model ") {
            std::string target = prompt.substr(7);
            for (size_t i = 0; i < PROVIDERS.size(); ++i) {
                if (PROVIDERS[i].name == target) {
                    provider_idx = i;
                    std::cout << "→ Model: " << PROVIDERS[i].display_name << "\n";
                    break;
                }
            }
            continue;
        }

        std::string up = gbk_to_utf8(prompt);
        auto& p = PROVIDERS[provider_idx];

        if (mode == Mode::STREAM) {
            std::cout << "\nAssistant (" << p.name << "): " << std::flush;
            forward_to_llm_stream(p, up, nullptr, [](const std::string& /*reasoning*/, const std::string& content) {
                if (!content.empty()) std::cout << utf8_to_gbk(content) << std::flush;
            });
            std::cout << "\n-------------------------\n";
        } else {
            std::string reply;
            forward_to_llm_stream(p, up, &reply);
            std::cout << "\n--- Reply ---\n" << utf8_to_gbk(reply) << "\n-------------------------\n";
        }
    }

    log_info("Shutdown...");
    svr.stop();
    srv_thr.join();
    log_info("Bye");
    return 0;
}
