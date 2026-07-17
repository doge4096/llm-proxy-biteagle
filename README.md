# LLM Proxy<br>
<br>
C++ 实现的 LLM 请求转发代理，支持并发 + SSE 流式输出。<br>
<br>
## 技术栈<br>
<br>
- **后端**：C++14、[cpp-httplib](https://github.com/yhirose/cpp-httplib) 0.50.1、[nlohmann/json](https://github.com/nlohmann/json)、OpenSSL、线程池<br>
- **前端**：Vite + React 19 + TypeScript + Tailwind CSS 4<br>
- **API**：DeepSeek（deepseek-v4-pro，启用 thinking）<br>
<br>
## 快速开始<br>
<br>
### 1. 编译后端<br>
<br>
在 MSYS2 MinGW64 终端中：<br>
<br>
```bash<br>
g++ -D_WIN32_WINNT=0x0A00 -std=c++14 -Iinclude \<br>
    -static-libgcc -static-libstdc++ \<br>
    src/main.cpp \<br>
    -lssl -lcrypto \<br>
    -o proxy.exe \<br>
    -lws2_32 -lcrypt32 -lgdi32 -ladvapi32 -luser32 -lpthread<br>
```<br>
<br>
### 2. 设置 API Key<br>
<br>
```powershell<br>
$env:DEEPSEEK_API_KEY = "sk-your-key-here"<br>
```<br>
<br>
### 3. 启动后端<br>
<br>
```bash<br>
./proxy.exe<br>
```<br>
<br>
选择模式 `1`（Stream），服务器监听 `http://localhost:8080`。<br>
<br>
终端内可直接输入消息与 DeepSeek 对话：<br>
<br>
```<br>
>>> 你好<br>
Assistant: 你好！有什么可以帮你的？<br>
```<br>
<br>
- 输入 `/mode stream` 切换流式模式<br>
- 输入 `/mode sync` 切换同步模式<br>
- 输入 `exit` 退出<br>
后端界面：<br>
(assets/back.png)<br>

### 4. 启动前端（可选）<br>
<br>
```bash<br>
cd frontend<br>
npm install<br>
npm run dev<br>
```<br>
<br>
浏览器打开 `http://localhost:5173`，通过图形界面与 LLM 对话。<br>
演示视频：<br>
![demo](assets/demo.gif)<br>
## 项目结构<br>
<br>
```<br>
├── src/main.cpp              # 后端主程序<br>
├── include/<br>
│   ├── httplib.h             # cpp-httplib（header-only）<br>
│   └── json.hpp              # nlohmann/json（header-only）<br>
├── frontend/                 # React 前端<br>
│   └── src/<br>
│       ├── hooks/<br>
│       │   ├── useSSE.ts           # SSE 流式消费<br>
│       │   └── useConversations.ts # 多轮对话管理<br>
│       └── components/<br>
│           ├── ChatWindow.tsx      # 聊天窗口<br>
│           ├── Sidebar.tsx         # 侧边栏<br>
│           └── ReasoningBlock.tsx  # 思维链展示<br>
└── README.md<br>
```<br>
<br>
## 待优化<br>
<br>
- [ ] Token 限流<br>
- [ ] 请求重试 + 熔断<br>
- [ ] 配置文件（端口、模型参数等）<br>
