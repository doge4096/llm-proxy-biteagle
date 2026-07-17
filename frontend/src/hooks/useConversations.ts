import { useState, useEffect, useCallback, useMemo } from 'react'

// 复用 ChatWindow 的 Message 类型
export interface Message {
  id: number
  role: 'user' | 'assistant'
  content: string
  reasoning: string
  streaming: boolean
}

export interface Conversation {
  id: string
  title: string
  messages: Message[]
  createdAt: number
}

const STORAGE_KEY = 'llm-proxy-conversations'

/** 从 localStorage 加载对话列表 */
function loadConversations(): Conversation[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (!raw) return []
    const arr: unknown = JSON.parse(raw)
    if (!Array.isArray(arr)) return []
    // 基础校验：每个元素必须有 id 和 messages
    return arr.filter(
      (c): c is Conversation =>
        typeof c === 'object' &&
        c !== null &&
        typeof (c as Conversation).id === 'string' &&
        Array.isArray((c as Conversation).messages),
    )
  } catch {
    return []
  }
}

/** 写入 localStorage */
function saveConversations(list: Conversation[]): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(list))
  } catch {
    // localStorage 满了或不可用，静默失败
  }
}

export interface UseConversationsReturn {
  conversations: Conversation[]
  currentId: string
  currentMessages: Message[]
  createConversation: () => void
  switchConversation: (id: string) => void
  deleteConversation: (id: string) => void
  updateMessages: (id: string, messages: Message[]) => void
}

export function useConversations(): UseConversationsReturn {
  const [conversations, setConversations] = useState<Conversation[]>(loadConversations)
  const [currentId, setCurrentId] = useState<string>(() => {
    const saved = loadConversations()
    return saved.length > 0 ? saved[0]!.id : ''
  })

  // conversations 变化时自动持久化
  useEffect(() => {
    // 只保存有消息的对话
    const nonEmpty = conversations.filter((c) => c.messages.length > 0)
    saveConversations(nonEmpty)
  }, [conversations])

  // 首次加载时若没有对话，自动创建一个
  useEffect(() => {
    if (conversations.length === 0) {
      const id = String(Date.now())
      const newConv: Conversation = {
        id,
        title: '新对话',
        messages: [],
        createdAt: Date.now(),
      }
      setConversations([newConv])
      setCurrentId(id)
    }
  }, [conversations.length])

  const currentMessages = useMemo(
    () => conversations.find((c) => c.id === currentId)?.messages ?? [],
    [conversations, currentId],
  )

  /** 新建空白对话 */
  const createConversation = useCallback(() => {
    const id = String(Date.now())
    const newConv: Conversation = {
      id,
      title: '新对话',
      messages: [],
      createdAt: Date.now(),
    }
    setConversations((prev) => [newConv, ...prev])
    setCurrentId(id)
  }, [])

  /** 切换到指定对话 */
  const switchConversation = useCallback((id: string) => {
    setCurrentId(id)
  }, [])

  /** 删除指定对话 */
  const deleteConversation = useCallback(
    (id: string) => {
      setConversations((prev) => {
        const next = prev.filter((c) => c.id !== id)
        // 如果删除的是当前对话，切换到第一个（若还有剩余）
        if (id === currentId) {
          if (next.length > 0) {
            setCurrentId(next[0]!.id)
          } else {
            // 没有剩余对话，创建一个新的
            const newId = String(Date.now())
            const newConv: Conversation = {
              id: newId,
              title: '新对话',
              messages: [],
              createdAt: Date.now(),
            }
            setCurrentId(newId)
            return [newConv]
          }
        }
        return next
      })
    },
    [currentId],
  )

  /** 更新指定对话的消息列表（同时自动生成标题） */
  const updateMessages = useCallback(
    (id: string, messages: Message[]) => {
      setConversations((prev) =>
        prev.map((c) => {
          if (c.id !== id) return c
          // 自动标题：取第一条用户消息的前 20 字
          let title = c.title
          if (title === '新对话' && messages.length > 0) {
            const firstUser = messages.find((m) => m.role === 'user')
            if (firstUser) {
              title = firstUser.content.slice(0, 20)
            }
          }
          return { ...c, messages, title }
        }),
      )
    },
    [],
  )

  return {
    conversations,
    currentId,
    currentMessages,
    createConversation,
    switchConversation,
    deleteConversation,
    updateMessages,
  }
}
