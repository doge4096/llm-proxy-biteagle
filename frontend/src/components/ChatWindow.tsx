import { useState, useRef, useEffect, useCallback, type FormEvent } from 'react'
import { useSSE } from '../hooks/useSSE'
import type { Message } from '../hooks/useConversations'
import ReasoningBlock from './ReasoningBlock'

interface ChatWindowProps {
  messages: Message[]
  onUpdateMessages: (messages: Message[]) => void
}

export default function ChatWindow({ messages, onUpdateMessages }: ChatWindowProps) {
  const [input, setInput] = useState('')
  const bottomRef = useRef<HTMLDivElement>(null)
  const textareaRef = useRef<HTMLTextAreaElement>(null)
  const messagesRef = useRef(messages)
  messagesRef.current = messages
  const { streaming, error, sendMessage, abort } = useSSE()

  // 自动滚动到底部
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  const handleSend = useCallback(
    (e?: FormEvent) => {
      e?.preventDefault()
      const text = input.trim()
      if (!text || streaming) return

      const uid = Date.now()
      const userMsg: Message = { id: uid, role: 'user', content: text, reasoning: '', streaming: false }
      const aid = uid + 1
      const assistantMsg: Message = { id: aid, role: 'assistant', content: '', reasoning: '', streaming: true }

      onUpdateMessages([...messages, userMsg, assistantMsg])
      setInput('')

      let contentAcc = ''
      let reasoningAcc = ''

      sendMessage(text, {
        onChunk(chunk) {
          if (chunk.reasoningChunk) reasoningAcc += chunk.reasoningChunk
          if (chunk.contentChunk) contentAcc += chunk.contentChunk

          onUpdateMessages(
            messagesRef.current.map((m) =>
              m.id === aid
                ? { ...m, content: contentAcc, reasoning: reasoningAcc || undefined as unknown as string }
                : m,
            ),
          )
        },
        onDone() {
          onUpdateMessages(
            messagesRef.current.map((m) =>
              m.id === aid
                ? { ...m, content: contentAcc, reasoning: reasoningAcc || undefined as unknown as string, streaming: false }
                : m,
            ),
          )
        },
        onError(msg) {
          onUpdateMessages(
            messagesRef.current.map((m) =>
              m.id === aid
                ? {
                    ...m,
                    content: contentAcc || `(请求失败: ${msg})`,
                    reasoning: reasoningAcc || undefined as unknown as string,
                    streaming: false,
                  }
                : m,
            ),
          )
        },
      })
    },
    [input, streaming, sendMessage, messages, onUpdateMessages],
  )

  // Enter 发送，Shift+Enter 换行
  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        handleSend()
      }
    },
    [handleSend],
  )

  // 自动调整输入框高度
  useEffect(() => {
    const ta = textareaRef.current
    if (!ta) return
    ta.style.height = 'auto'
    ta.style.height = `${Math.min(ta.scrollHeight, 300)}px`
  }, [input])

  return (
    <div className="flex flex-col h-screen max-w-3xl mx-auto">
      {/* === 标题栏 === */}
      <header className="shrink-0 py-4 px-6 border-b border-gray-800 bg-gray-950/80 backdrop-blur sticky top-0 z-10">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-lg font-bold text-white tracking-tight">
              LLM Proxy Playground
            </h1>
            <p className="text-xs text-gray-500 mt-0.5">
              deepseek-v4-pro · thinking: enabled · SSE stream
            </p>
          </div>
          {streaming && (
            <button
              onClick={abort}
              className="text-xs text-gray-400 hover:text-red-400 border border-gray-700 hover:border-red-500/50 rounded-lg px-3 py-1.5 transition-colors cursor-pointer"
            >
              停止
            </button>
          )}
        </div>
      </header>

      {/* === 消息列表 === */}
      <main className="flex-1 overflow-y-auto px-4 py-6 space-y-5">
        {messages.length === 0 && (
          <div className="flex items-center justify-center h-full">
            <p className="text-gray-600 text-sm">发送一条消息开始对话</p>
          </div>
        )}

        {messages.map((msg) => (
          <div
            key={msg.id}
            className={`flex ${msg.role === 'user' ? 'justify-end' : 'justify-start'}`}
          >
            <div
              className={`max-w-[80%] rounded-2xl px-4 py-3 text-sm leading-relaxed ${
                msg.role === 'user'
                  ? 'bg-blue-600 text-white rounded-br-md'
                  : 'bg-gray-800 text-gray-100 rounded-bl-md'
              }`}
            >
              {/* 思维链 — 仅 assistant */}
              {msg.role === 'assistant' && msg.reasoning && (
                <ReasoningBlock content={msg.reasoning} />
              )}

              {/* 正文 */}
              {msg.content ? (
                <div className="whitespace-pre-wrap break-words">
                  {msg.content}
                  {msg.streaming && (
                    <span className="inline-block w-2 h-4 bg-gray-400 ml-0.5 animate-pulse align-text-bottom" />
                  )}
                </div>
              ) : msg.streaming ? (
                <span className="inline-block w-2 h-4 bg-gray-400 animate-pulse align-text-bottom" />
              ) : null}

              {/* 错误提示 */}
              {msg.role === 'assistant' && !msg.streaming && error && (
                <p className="text-red-400 text-xs mt-2 border-t border-red-500/20 pt-2">
                  {error}
                </p>
              )}
            </div>
          </div>
        ))}

        <div ref={bottomRef} />
      </main>

      {/* === 输入区 === */}
      <footer className="shrink-0 px-4 py-4 border-t border-gray-800 bg-gray-950/80 backdrop-blur">
        <form onSubmit={handleSend} className="flex gap-3 items-end">
          <textarea
            ref={textareaRef}
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="输入消息… (Enter 发送 / Shift+Enter 换行)"
            rows={2}
            disabled={streaming}
            className="flex-1 resize-none rounded-xl bg-gray-800 border border-gray-700 px-4 py-3 text-sm text-gray-100 placeholder-gray-500 focus:outline-none focus:ring-2 focus:ring-blue-500/50 focus:border-blue-500 disabled:opacity-50 transition-colors"
          />
          <button
            type="submit"
            disabled={!input.trim() || streaming}
            className="shrink-0 rounded-xl bg-blue-600 hover:bg-blue-500 disabled:bg-gray-700 disabled:text-gray-500 text-white px-6 py-3 text-sm font-medium transition-colors cursor-pointer disabled:cursor-not-allowed"
          >
            {streaming ? '…' : '发送'}
          </button>
        </form>
      </footer>
    </div>
  )
}
