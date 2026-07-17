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
#include <winsock2.h>
#include <windows.h>

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

// ==================== 编码转换 ====================
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

// ==================== 配置 & 日志 ====================
std::string get_api_key() {
    const char* key = std::getenv("DEEPSEEK_API_KEY");
    if (!key) throw std::runtime_error("API Key not set");
    return std::string(key);
}

const std::string DEEPSEEK_BASE_URL = "https://api.deepseek.com";
const int SERVER_PORT = 8080;
std::mutex g_log_mutex;

void log_info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm; localtime_s(&local_tm, &time);
    std::cout << "[" << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << "] [INFO] " << msg << std::endl;
}

void log_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm; localtime_s(&local_tm, &time);
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

// ==================== 客户端流式（适配 0.50.1，上版已对）====================
bool forward_to_llm_stream(
    const std::string& prompt,
    std::string* full_reply = nullptr,
    std::function<void(const std::string&, const std::string&)> on_chunk = nullptr
) {
    std::mutex cb_mutex;
    std::string buffer;

    httplib::Client client(DEEPSEEK_BASE_URL.c_str());
    client.set_connection_timeout(15, 0);
    client.set_read_timeout(300, 0);
    client.enable_server_certificate_verification(false);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + get_api_key()},
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"}
    };

    json request_body = {
		    {"model", "deepseek-v4-pro"},
		    {"messages", {
		        {{"role", "user"}, {"content", prompt}}
		    }},
		    {"stream", true},
		    {"thinking", {{"type", "enabled"}}},   // v4-pro 才支持
		    {"reasoning_effort", "high"}
		};

    log_info("Start streaming request. Prompt length: " + std::to_string(prompt.length()));

    // 0.50.1 的 Post 6 参数：path, headers, body(string), content_type, ContentReceiver, DownloadProgress
    // ContentReceiver = function<bool(const char* data, size_t len)>
    auto res = client.Post(
        "/chat/completions",
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
                    log_info("Stream finished.");
                    return false;
                }
                try {
				    json j = json::parse(json_str);
				    if (j.contains("choices") && !j["choices"].empty()) {
				        auto& delta = j["choices"][0]["delta"];

				        // 思维链
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
            // 防 buffer 膨胀
            if (buffer.length() > 1024 * 1024) { log_error("Buffer overflow, reset"); buffer.clear(); }
            return true;
        },
        nullptr
    );

    // ? 只要 HTTP 状态码是 200，就认为成功（无视其他错误状态）
		if (!res) {
		    return true;
		}
		
		else {
			// ? HTTP 错误状态码
					log_error("HTTP " + std::to_string(res->status));
					if (full_reply) *full_reply = R"({"error":"HTTP )" + std::to_string(res->status) + R"("})";
					return false;
		}
		
		
	
}

	std::string forward_to_llm_sync(const std::string& prompt) {
    std::string r; forward_to_llm_stream(prompt, &r); return r;
}

// ==================== main ====================
int main() {
    try { get_api_key(); log_info("API Key loaded"); }
    catch (const std::exception& e) { log_error(e.what()); return 1; }

    httplib::Server svr;
    ThreadPool pool(4);

    // --- 同步接口 ---
    svr.Post("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        log_info("Sync request from " + req.remote_addr);
        pool.enqueue([&, req, &res]() {
            try {
                auto jreq = json::parse(req.body);
                std::string p = jreq.value("prompt", "");
                if (p.empty()) { res.status = 400; res.set_content(R"({"error":"empty"})","application/json"); return; }
                res.set_content(json{{"reply", forward_to_llm_sync(p)}}.dump(), "application/json; charset=utf-8");
            } catch (...) { res.status = 500; res.set_content(R"({"error":"internal"})","application/json"); }
        });
    });

    // --- SSE 流式接口 ---
    svr.Post("/chat/stream", [&](const httplib::Request& req, httplib::Response& res) {
        log_info("SSE request from " + req.remote_addr);

        // SSE 头
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");

        // 共享状态：handler 设 content_provider，worker 线程收 DeepSeek 流
        struct StreamState {
            std::mutex mtx;
            std::condition_variable cv;
            std::vector<std::pair<std::string, std::string>> chunks;  // {reasoning, content}
            bool done = false;
        };
        auto state = std::make_shared<StreamState>();

        // ★ 必须在 handler 返回前设置，httplib 才会保持长连接
        res.set_content_provider(
            "text/event-stream",
            [state](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                std::string reasoning;
                std::string content;
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

        // 异步收 DeepSeek 流，喂入 state
        std::string prompt_body = req.body;
        pool.enqueue([state, prompt_body]() {
            try {
                auto jreq = json::parse(prompt_body);
                std::string prompt = jreq.value("prompt", "");
                if (prompt.empty()) {
                    std::lock_guard<std::mutex> l(state->mtx);
                    state->done = true;
                    state->cv.notify_one();
                    return;
                }

                forward_to_llm_stream(prompt, nullptr, [state](const std::string& reasoning, const std::string& content) {
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

    // --- 健康检查 ---
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("LLM Proxy (httplib 0.50.1, SSE OK)", "text/plain");
    });

    // ==================== CLI ====================
    enum class Mode { STREAM, SYNC };
    std::cout << "===================================\n";
    std::cout << "  LLM Proxy - DeepSeek (httplib 0.50.1)\n";
    std::cout << "===================================\n";
    std::cout << "1. Stream (typing)\n2. Sync (full)\nChoice: ";
    Mode mode = Mode::STREAM;
    std::string c; std::getline(std::cin, c);
    if (c == "2") mode = Mode::SYNC;
    std::cout << "Mode: " << (mode == Mode::STREAM ? "Stream" : "Sync") << "\n\n";
    std::cout << "/mode stream | /mode sync | exit\n\n";

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

        std::string up = gbk_to_utf8(prompt);
        if (mode == Mode::STREAM) {
            std::cout << "\nAssistant: " << std::flush;
            forward_to_llm_stream(up, nullptr, [](const std::string& /*reasoning*/, const std::string& content) {
                if (!content.empty()) std::cout << utf8_to_gbk(content) << std::flush;
            });
            std::cout << "\n-------------------------\n";
        } else {
            std::string ur = forward_to_llm_sync(up);
            std::cout << "\n--- Reply ---\n" << utf8_to_gbk(ur) << "\n-------------------------\n";
        }
    }

    log_info("Shutdown...");
    svr.stop();
    srv_thr.join();
    log_info("Bye");
    return 0;
}
