import { useState, useRef, useCallback } from 'react'

export interface SSEChunk {
  reasoningChunk: string | null
  contentChunk: string | null
}

export interface SSECallbacks {
  onChunk: (chunk: SSEChunk) => void
  onDone: () => void
  onError: (message: string) => void
}

interface UseSSEReturn {
  streaming: boolean
  error: string | null
  sendMessage: (prompt: string, callbacks: SSECallbacks) => void
  abort: () => void
}

const SSE_URL = 'http://localhost:8080/chat/stream'

export function useSSE(): UseSSEReturn {
  const [streaming, setStreaming] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const abortRef = useRef<AbortController | null>(null)

  const abort = useCallback(() => {
    abortRef.current?.abort()
    abortRef.current = null
    setStreaming(false)
  }, [])

  const sendMessage = useCallback(
    (prompt: string, callbacks: SSECallbacks) => {
      const controller = new AbortController()
      abortRef.current = controller

      setStreaming(true)
      setError(null)

      let aborted = false
      let doneFired = false

      const fireDone = () => {
        if (!doneFired) {
          doneFired = true
          callbacks.onDone()
        }
        setStreaming(false)
        abortRef.current = null
      }

      fetch(SSE_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ prompt }),
        signal: controller.signal,
      })
        .then(async (response) => {
          if (!response.ok) {
            const msg = `HTTP ${response.status}: ${response.statusText}`
            setError(msg)
            callbacks.onError(msg)
            setStreaming(false)
            abortRef.current = null
            return
          }
          if (!response.body) {
            const msg = 'Response body is null — streaming not supported'
            setError(msg)
            callbacks.onError(msg)
            setStreaming(false)
            abortRef.current = null
            return
          }

          const reader = response.body.getReader()
          const decoder = new TextDecoder('utf-8')
          let buffer = ''

          try {
            while (true) {
              const { done, value } = await reader.read()
              if (done) break

              buffer += decoder.decode(value, { stream: true })

              const parts = buffer.split('\n\n')
              buffer = parts.pop() ?? ''

              for (const part of parts) {
                const line = part.trim()
                if (!line) continue
                if (!line.startsWith('data: ')) continue

                const payload = line.slice(6).trim()

                if (payload === '[DONE]') {
                  continue
                }

                try {
                  const obj: Record<string, unknown> = JSON.parse(payload)
                  const r = extractString(obj, 'reasoning_content')
                  const c = extractString(obj, 'content')

                  if (r || c) {
                    callbacks.onChunk({
                      reasoningChunk: r,
                      contentChunk: c,
                    })
                  }
                } catch {
                  // 非 JSON payload — 静默跳过
                }
              }
            }
          } finally {
            reader.releaseLock()
          }

          if (!aborted) fireDone()
        })
        .catch((err: unknown) => {
          if (err instanceof DOMException && err.name === 'AbortError') {
            aborted = true
            fireDone()
            return
          }
          const msg = err instanceof Error ? err.message : String(err)
          setError(msg)
          callbacks.onError(msg)
          setStreaming(false)
          abortRef.current = null
        })
    },
    [],
  )

  return { streaming, error, sendMessage, abort }
}

/**
 * 从 JSON 对象中提取指定 key 的字符串值。
 * 先查顶层，再查 delta 内部（兼容 OpenAI / DeepSeek delta 格式）。
 */
function extractString(
  obj: Record<string, unknown>,
  key: string,
): string | null {
  if (typeof obj[key] === 'string' && (obj[key] as string).length > 0) {
    return obj[key] as string
  }

  const delta = obj['delta']
  if (delta && typeof delta === 'object' && !Array.isArray(delta)) {
    const d = delta as Record<string, unknown>
    if (typeof d[key] === 'string' && (d[key] as string).length > 0) {
      return d[key] as string
    }
  }

  return null
}
